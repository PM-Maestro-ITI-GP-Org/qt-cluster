#ifndef NETWORKCLOCK_H
#define NETWORKCLOCK_H

#pragma once
#include <QObject>
#include <QString>
#include <QThread>
#include <QTimeZone>
#include <QTimer>

/* NetworkClock: wall-clock time for the cluster, disciplined over SNTP.
 *
 * WHY NOT JUST new Date()
 * -----------------------
 * The QML clock used to read the guest's system clock directly. On a QNX guest
 * that boots without an RTC battery -- or before any time daemon has run --
 * that clock starts at the epoch, so the cluster confidently displayed the
 * wrong time with no indication anything was amiss. This queries a real time
 * server and, just as importantly, knows when it has NOT managed to.
 *
 * WHY AN OFFSET RATHER THAN settimeofday()
 * ----------------------------------------
 * Setting the system clock needs root and would step time under every other
 * process on the guest. The cluster only needs to DISPLAY the right time, so it
 * keeps the correction to itself: m_offsetMs is added to the system clock on
 * the way to the screen and nothing outside this class is affected. If the
 * platform later runs chrony/ntpd properly, the measured offset simply
 * converges on zero and this keeps working unchanged.
 *
 * WHY RAW POSIX UDP RATHER THAN QUdpSocket
 * ----------------------------------------
 * Qt::Network is not currently in the build's find_package, and adding a Qt
 * module means adding it to the QNX deploy as well. SNTP is one 48-byte
 * datagram; the socket code below is smaller than the deployment change would
 * be, and it matches how SpiReader and AiReader already talk to the platform.
 *
 * CONFIGURATION -- all runtime, no rebuild:
 *   CLUSTER_TZ           IANA zone. Default "Africa/Cairo".
 *   CLUSTER_NTP          comma-separated servers. Default "pool.ntp.org".
 *   CLUSTER_NTP_PERIOD_S resync period, default 1800.
 */
class NetworkClock : public QObject
{
    Q_OBJECT

    /* Split rather than one preformatted string: the QML sets the meridiem in a
     * smaller weight beside the digits, and gluing them together here would
     * force it to parse them back apart. */
    Q_PROPERTY(QString time     READ time     NOTIFY timeChanged)   /* "9:41"    */
    Q_PROPERTY(QString meridiem READ meridiem NOTIFY timeChanged)   /* "AM"/"PM" */
    Q_PROPERTY(QString date     READ date     NOTIFY timeChanged)   /* "Sun 17 Aug" */

    /* False until a server has actually answered. The QML dims the clock while
     * this is false, so an unsynced guest reads as "time unknown" rather than
     * as a confident wrong answer. */
    Q_PROPERTY(bool    synced   READ synced   NOTIFY syncedChanged)
    Q_PROPERTY(QString zoneName READ zoneName CONSTANT)

public:
    explicit NetworkClock(QObject *parent = nullptr);
    ~NetworkClock() override;

    QString time()     const { return m_time; }
    QString meridiem() const { return m_meridiem; }
    QString date()     const { return m_date; }
    bool    synced()   const { return m_synced; }
    QString zoneName() const { return QString::fromUtf8(m_zone.id()); }

signals:
    void timeChanged();
    void syncedChanged();

private slots:
    void onOffset(qint64 offsetMs);   /* from the SNTP thread */
    void tick();

private:
    void scheduleNextTick();

    QTimeZone m_zone;
    QTimer    m_tick;
    QThread  *m_worker = nullptr;

    qint64  m_offsetMs = 0;
    bool    m_synced   = false;
    QString m_time     = QStringLiteral("--:--");
    QString m_meridiem;
    QString m_date;
};

#endif /* NETWORKCLOCK_H */
