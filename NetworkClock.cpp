#include "NetworkClock.h"

#include <QDateTime>
#include <QDebug>
#include <QLocale>

#include <arpa/inet.h>
#include <netdb.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <unistd.h>
#include <cstring>

namespace {

/* Seconds between the NTP epoch (1900-01-01) and the Unix epoch (1970-01-01).
 * Valid until the NTP era rolls over in 2036, which is well past the point
 * where this whole stack gets rewritten. */
constexpr quint64 kNtpToUnix = 2208988800ULL;
constexpr int     kNtpPort   = 123;
constexpr int     kPacketLen = 48;

/* LI = 0 (no warning), VN = 4, Mode = 3 (client). */
constexpr unsigned char kClientHeader = 0x23;

/* Fixed-point NTP timestamp -> milliseconds since the Unix epoch. */
qint64 ntpToUnixMs(quint32 seconds, quint32 fraction)
{
    const qint64 secs = static_cast<qint64>(seconds) - static_cast<qint64>(kNtpToUnix);
    /* fraction is in units of 2^-32 s; scale to ms without overflowing. */
    const qint64 ms = (static_cast<qint64>(fraction) * 1000LL) >> 32;
    return secs * 1000LL + ms;
}

quint32 rd32(const unsigned char *p)
{
    return (quint32(p[0]) << 24) | (quint32(p[1]) << 16)
         | (quint32(p[2]) << 8)  |  quint32(p[3]);
}

void wr32(unsigned char *p, quint32 v)
{
    p[0] = (v >> 24) & 0xFF; p[1] = (v >> 16) & 0xFF;
    p[2] = (v >> 8)  & 0xFF; p[3] =  v        & 0xFF;
}

/* One SNTP exchange. Returns true and sets offsetMs on success.
 *
 * The offset is the full four-timestamp NTP calculation, not just "server said
 * X". Using the transmit timestamp alone bakes the whole network round trip
 * into the answer; on a slow link that is tens of milliseconds of pure error.
 *
 *   offset = ((T2 - T1) + (T3 - T4)) / 2
 *
 * with T1 our send, T2 server receive, T3 server transmit, T4 our receive. The
 * two halves have the path delay in opposite signs, so averaging cancels it as
 * long as the path is roughly symmetric.                                     */
bool querySntp(const QString &host, int timeoutMs, qint64 *offsetMs)
{
    struct addrinfo hints;
    std::memset(&hints, 0, sizeof hints);
    hints.ai_family   = AF_INET;
    hints.ai_socktype = SOCK_DGRAM;

    struct addrinfo *res = nullptr;
    const QByteArray hostUtf8 = host.toUtf8();
    if (::getaddrinfo(hostUtf8.constData(),
                      QByteArray::number(kNtpPort).constData(),
                      &hints, &res) != 0 || !res)
        return false;

    const int fd = ::socket(res->ai_family, res->ai_socktype, res->ai_protocol);
    if (fd < 0) { ::freeaddrinfo(res); return false; }

    struct timeval tv;
    tv.tv_sec  = timeoutMs / 1000;
    tv.tv_usec = (timeoutMs % 1000) * 1000;
    ::setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof tv);

    unsigned char pkt[kPacketLen];
    std::memset(pkt, 0, sizeof pkt);
    pkt[0] = kClientHeader;

    /* Stamp our send time into the transmit field. A well-behaved server
     * echoes it back in the originate field, which is both how T1 comes back
     * to us and a cheap check that the reply belongs to this request. */
    const qint64 t1 = QDateTime::currentMSecsSinceEpoch();
    const quint64 t1Ntp = static_cast<quint64>(t1) + kNtpToUnix * 1000ULL;
    wr32(pkt + 40, static_cast<quint32>(t1Ntp / 1000ULL));
    wr32(pkt + 44, static_cast<quint32>(((t1Ntp % 1000ULL) << 32) / 1000ULL));

    bool ok = false;
    if (::sendto(fd, pkt, sizeof pkt, 0, res->ai_addr, res->ai_addrlen)
            == static_cast<ssize_t>(sizeof pkt)) {
        unsigned char rep[kPacketLen];
        const ssize_t n = ::recv(fd, rep, sizeof rep, 0);
        const qint64 t4 = QDateTime::currentMSecsSinceEpoch();

        if (n == kPacketLen) {
            const int mode    = rep[0] & 0x07;
            const int stratum = rep[1];
            /* Mode 4 is "server". Stratum 0 is a kiss-o'-death packet, which
             * carries no time and must never be treated as one. */
            if (mode == 4 && stratum > 0 && stratum < 16) {
                const qint64 t2 = ntpToUnixMs(rd32(rep + 32), rd32(rep + 36));
                const qint64 t3 = ntpToUnixMs(rd32(rep + 40), rd32(rep + 44));
                *offsetMs = ((t2 - t1) + (t3 - t4)) / 2;
                ok = true;
            }
        }
    }

    ::close(fd);
    ::freeaddrinfo(res);
    return ok;
}

/* Polls the configured servers on a period and reports the offset.
 *
 * Its own thread because getaddrinfo and recv both block, and doing either on
 * the Qt main thread would stall the gauges -- a DNS lookup against an
 * unreachable server can sit there for seconds. */
class SntpWorker : public QThread
{
    Q_OBJECT
public:
    SntpWorker(QStringList servers, int periodS, QObject *parent = nullptr)
        : QThread(parent), m_servers(std::move(servers)), m_periodS(periodS) {}

    void stop() { m_running = false; }

signals:
    void offset(qint64 offsetMs);

protected:
    void run() override
    {
        /* Retry fast until the first success, then settle to the configured
         * period. The guest's network is often not up yet at launch, and a
         * cluster that waits half an hour to show the right time is no better
         * than one that never syncs. */
        int backoffS = 2;
        while (m_running) {
            qint64 off = 0;
            bool got = false;
            for (const QString &s : m_servers) {
                if (!m_running) return;
                if (querySntp(s, 2000, &off)) { got = true; break; }
            }

            int sleepS;
            if (got) {
                emit offset(off);
                backoffS = 2;
                sleepS = m_periodS;
            } else {
                sleepS = backoffS;
                backoffS = qMin(backoffS * 2, 300);   /* cap at 5 min */
            }

            /* Sleep in short slices so stop() is honoured promptly instead of
             * after a full resync period. */
            for (int i = 0; i < sleepS * 10 && m_running; ++i)
                QThread::msleep(100);
        }
    }

private:
    QStringList m_servers;
    int  m_periodS;
    volatile bool m_running = true;
};

} // namespace

NetworkClock::NetworkClock(QObject *parent) : QObject(parent)
{
    const QString zoneId =
        qEnvironmentVariableIsSet("CLUSTER_TZ")
            ? qEnvironmentVariable("CLUSTER_TZ")
            : QStringLiteral("Africa/Cairo");

    m_zone = QTimeZone(zoneId.toUtf8());
    if (!m_zone.isValid()) {
        /* A bad CLUSTER_TZ must not leave the clock showing UTC while claiming
         * otherwise -- fall back explicitly and say so. */
        qWarning() << "[clock] unknown timezone" << zoneId << "- falling back to Africa/Cairo";
        m_zone = QTimeZone("Africa/Cairo");
    }

    const QString serverList =
        qEnvironmentVariableIsSet("CLUSTER_NTP")
            ? qEnvironmentVariable("CLUSTER_NTP")
            : QStringLiteral("pool.ntp.org");
    QStringList servers;
    for (const QString &s : serverList.split(QLatin1Char(','), Qt::SkipEmptyParts))
        servers << s.trimmed();

    bool periodOk = false;
    const int period = qEnvironmentVariable("CLUSTER_NTP_PERIOD_S").toInt(&periodOk);

    auto *w = new SntpWorker(servers, periodOk && period > 0 ? period : 1800, this);
    connect(w, &SntpWorker::offset, this, &NetworkClock::onOffset, Qt::QueuedConnection);
    m_worker = w;
    w->start();

    m_tick.setSingleShot(true);
    connect(&m_tick, &QTimer::timeout, this, &NetworkClock::tick);

    tick();   /* paint something immediately rather than after the first minute */
    qInfo() << "[clock] zone" << zoneName() << "servers" << servers;
}

NetworkClock::~NetworkClock()
{
    if (auto *w = qobject_cast<SntpWorker *>(m_worker)) {
        w->stop();
        w->wait(1500);
    }
}

void NetworkClock::onOffset(qint64 offsetMs)
{
    m_offsetMs = offsetMs;
    if (!m_synced) {
        m_synced = true;
        emit syncedChanged();
    }
    qInfo() << "[clock] synced, offset" << offsetMs << "ms";
    tick();
}

void NetworkClock::tick()
{
    const QDateTime now =
        QDateTime::fromMSecsSinceEpoch(QDateTime::currentMSecsSinceEpoch() + m_offsetMs)
            .toTimeZone(m_zone);

    /* The 12-hour hour is computed, not formatted, and the trap is worth
     * recording: Qt's "h" only means 12-hour if the SAME format string also
     * contains "AP". Formatting the digits and the meridiem in two separate
     * toString calls -- which is what the split properties invite -- silently
     * yields 24-hour digits next to a PM marker, and the display read
     * "22:27 PM". Doing the arithmetic here cannot regress that way, and it
     * keeps the marker as literal AM/PM rather than whatever the locale
     * happens to substitute.
     *
     * No leading zero on the hour: that is a 24-hour convention and looks
     * wrong beside a meridiem. Minutes always take one. */
    const int h24 = now.time().hour();
    const int h12 = (h24 % 12 == 0) ? 12 : h24 % 12;
    const QString t = QStringLiteral("%1:%2")
                          .arg(h12)
                          .arg(now.time().minute(), 2, 10, QLatin1Char('0'));
    const QString ap = (h24 < 12) ? QStringLiteral("AM") : QStringLiteral("PM");
    const QString d  = now.toString(QStringLiteral("ddd d MMM"));

    if (t != m_time || ap != m_meridiem || d != m_date) {
        m_time = t; m_meridiem = ap; m_date = d;
        emit timeChanged();
    }
    scheduleNextTick();
}

void NetworkClock::scheduleNextTick()
{
    /* Fire just after the next minute boundary rather than once a second. The
     * display has minute resolution, so a 1 Hz timer was 59 wakeups out of 60
     * that changed nothing -- and it could still show a stale minute for up to
     * a second, because it was not aligned to the boundary at all. The 250ms
     * cushion covers timer jitter so the tick lands after the rollover, never
     * a hair before it. */
    const qint64 nowMs = QDateTime::currentMSecsSinceEpoch() + m_offsetMs;
    const qint64 intoMinute = nowMs % 60000;
    m_tick.start(static_cast<int>(60000 - intoMinute) + 250);
}

#include "NetworkClock.moc"
