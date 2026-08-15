#include "cluster.h"
#include <QDebug>
#include <algorithm>
#include <cmath>

#define SLOG_ERR(msg)  qWarning() << "[backend]" << msg
#define SLOG_INFO(msg) qDebug()   << "[backend]" << msg

template<typename T>
static inline T qnx_clamp(T val, T lo, T hi)
{
    return val < lo ? lo : (val > hi ? hi : val);
}

VehicleBackend::VehicleBackend(QObject *parent) : QObject(parent)
{
    m_spiReader = new SpiReader(this);
    connect(m_spiReader, &SpiReader::newData,
            this,         &VehicleBackend::onSpiData,
            Qt::QueuedConnection);
    m_spiReader->start();

    m_aiReader = new AiReader(this);
    connect(m_aiReader, &AiReader::newResults,
            this,        &VehicleBackend::onAiResults,
            Qt::QueuedConnection);
    m_aiReader->start();

    SLOG_INFO("readers started: /motor_ctrl (sensors), /motor_ai_result (verdicts)");
}

VehicleBackend::~VehicleBackend()
{
    if (m_spiReader) {
        m_spiReader->stop();
        m_spiReader->wait(1000);
    }
    if (m_aiReader) {
        m_aiReader->stop();
        /* Longer than SpiReader's: this one sleeps 200ms a cycle, so it can be
         * most of that away from noticing the stop flag. */
        m_aiReader->wait(1000);
    }
}

/* Seconds since the previous snapshot, from the producer's own timestamps.
 * Returns 0 when the gap is unusable -- first sample, a restart that moved the
 * clock backwards, or a stall -- and the caller then leaves the filters alone
 * rather than applying a step of unknown size.                              */
float VehicleBackend::dtFrom(quint64 timestampUs)
{
    if (m_lastTimestamp == 0 || timestampUs <= m_lastTimestamp) {
        m_lastTimestamp = timestampUs;
        return 0.f;
    }
    const float dt = static_cast<float>(timestampUs - m_lastTimestamp) * 1e-6f;
    m_lastTimestamp = timestampUs;
    if (dt < DT_MIN_S || dt > DT_MAX_S)
        return 0.f;
    return dt;
}

/* Median of the last RPM_MEDIAN_N samples.
 *
 * Ahead of the low pass, not instead of it. rpm comes from counting tach edges
 * in a fixed window, so a missed or doubled edge is a single-sample step to
 * half or double the true value -- an impulse, which an averaging filter
 * smears across its whole time constant instead of removing. A median drops it
 * outright and leaves everything else untouched.                            */
float VehicleBackend::medianRpm(float sample)
{
    m_rpmWindow[m_rpmWindowPos] = sample;
    m_rpmWindowPos = (m_rpmWindowPos + 1) % RPM_MEDIAN_N;
    if (m_rpmWindowCount < RPM_MEDIAN_N)
        ++m_rpmWindowCount;

    float sorted[RPM_MEDIAN_N];
    std::copy(m_rpmWindow, m_rpmWindow + m_rpmWindowCount, sorted);
    std::sort(sorted, sorted + m_rpmWindowCount);
    return sorted[m_rpmWindowCount / 2];
}

/* Called on the Qt main thread for every new snapshot, via QueuedConnection. */
void VehicleBackend::onSpiData(MotorSnapshot snap)
{
    const float dt = dtFrom(snap.timestamp);
    /* dt/(tau+dt): a one-pole low pass whose smoothing is set in seconds, so
     * the block rate can change without changing how the gauges behave. */
    const float aSpeed = dt > 0.f ? dt / (TAU_SPEED_S + dt) : 0.f;
    const float aPower = dt > 0.f ? dt / (TAU_POWER_S + dt) : 0.f;

    /* --- electricals -------------------------------------------------------
     * Three phase currents and three phase voltages, so instantaneous power is
     * just the sum of their products. For a balanced three-phase set that sum
     * is constant over the cycle rather than pulsating, so one snapshot gives
     * real power -- no RMS window, and no assumed bus voltage or power factor.
     *
     * The phase voltages are single-ended off a divider and so carry a common
     * offset. It cancels: with a floating neutral the currents sum to zero, so
     * sum(v_common * i_k) = v_common * sum(i_k) = 0. Only the voltage *scale*
     * has to be right, not its zero -- which is fortunate, since nothing
     * documents where the divider sits.                                       */
    const float ia = (static_cast<float>(snap.current[CurrentA]) - ADC_MIDSCALE) * AMPS_PER_COUNT;
    const float ib = (static_cast<float>(snap.current[CurrentB]) - ADC_MIDSCALE) * AMPS_PER_COUNT;
    const float ic = (static_cast<float>(snap.current[CurrentC]) - ADC_MIDSCALE) * AMPS_PER_COUNT;

    const float va = static_cast<float>(snap.current[VoltageA]) * VOLTS_PER_COUNT;
    const float vb = static_cast<float>(snap.current[VoltageB]) * VOLTS_PER_COUNT;
    const float vc = static_cast<float>(snap.current[VoltageC]) * VOLTS_PER_COUNT;

    m_busVoltage = static_cast<float>(snap.current[VoltageDcBus]) * VOLTS_PER_COUNT;

    /* Clarke magnitude: for a balanced set this is the phase amplitude and is
     * steady through the cycle, which a single |i| sample is not. Over root 2
     * for RMS. Drives the overcurrent warning and nothing else.              */
    const float iPeak = std::sqrt(2.f / 3.f * (ia * ia + ib * ib + ic * ic));
    m_currentRms = iPeak / 1.41421356f;

    const float pInstant = va * ia + vb * ib + vc * ic;

    /* --- speed -------------------------------------------------------------
     * Median first to drop tach glitches, then the low pass. */
    const float rpmRaw = medianRpm(static_cast<float>(snap.rpm));

    if (!m_primed) {
        /* Adopt the first sample rather than ramping to it from zero, which
         * would otherwise show a phantom acceleration on every start. */
        m_rpm = rpmRaw;
        m_powerFiltered = pInstant;
        m_primed = true;
    } else {
        if (aSpeed > 0.f) m_rpm          += (rpmRaw   - m_rpm)          * aSpeed;
        if (aPower > 0.f) m_powerFiltered += (pInstant - m_powerFiltered) * aPower;
    }

    /* Deadbands are applied on the way out, never back into the filter state:
     * folding them in would let a reading below the threshold hold the filter
     * at zero and swallow a real climb out of it. */
    float speedKmh = m_rpm * KMH_PER_RPM;
    if (std::abs(speedKmh) < SPEED_DEADBAND)
        speedKmh = 0.f;

    float powerW = m_powerFiltered;
    if (std::abs(powerW) < POWER_DEADBAND)
        powerW = 0.f;

    /* --- vibration --------------------------------------------------------- */
    const float vx = static_cast<float>(snap.vib_x) / MPU_COUNTS_PER_G;
    const float vy = static_cast<float>(snap.vib_y) / MPU_COUNTS_PER_G;
    const float vz = static_cast<float>(snap.vib_z) / MPU_COUNTS_PER_G;
    const float vTotal = std::sqrt(vx * vx + vy * vy + vz * vz);

    if (vx != m_vibX || vy != m_vibY || vz != m_vibZ) {
        m_vibX = vx; m_vibY = vy; m_vibZ = vz;
        m_vibTotal = vTotal;
        emit vibChanged();
    }

    /* Guarded on change even though these are filtered floats that rarely
     * repeat: at a standstill both sit in their deadbands at exactly zero, and
     * that is precisely when a signal per snapshot would be 100 pointless
     * QML re-evaluations a second. */
    if (speedKmh != m_speedKmh) {
        m_speedKmh = speedKmh;
        emit speedChanged();
        emit rpmChanged();
    }
    if (powerW != m_powerW) {
        m_powerW = powerW;
        emit powerChanged();
    }
    emit electricalChanged();

    evaluateWarnings();
}

void VehicleBackend::onAiResults(AiResults results)
{
    m_ai = results;
    m_aiConnected = true;

    /* The verdicts are free text from the model -- nothing in either repo
     * defines their vocabulary -- so this decides only whether a verdict is a
     * complaint, by ruling out the ways of saying "fine". Anything
     * unrecognised counts as a complaint: a cluster that stays quiet on a
     * string it does not know is the wrong way round.
     *
     * Correct this list once the server's actual outputs are known. */
    auto benign = [](const QString &s) {
        if (s.isEmpty())
            return true;
        static const char *ok[] = { "normal", "healthy", "none", "ok", "no fault",
                                    "no anomaly", "nominal", "good", "pass" };
        const QString t = s.trimmed().toLower();
        for (const char *k : ok)
            if (t == QLatin1String(k))
                return true;
        return false;
    };

    m_aiAlert = !benign(m_ai.anomaly) || !benign(m_ai.faultClass);
    emit aiChanged();
}

void VehicleBackend::evaluateWarnings()
{
    /* Vibration warning uses (total - 1g) so gravity does not trip it. */
    const float vibDynamic = std::abs(m_vibTotal - 1.f);

    /* Against rpm, not the converted speed: the thresholds are a property of
     * the motor, and KMH_PER_RPM is a property of the rig it is bolted to. */
    const bool sw = m_rpm         >= SPEED_WARN_RPM;
    const bool vw = vibDynamic    >= VIB_WARN_G;
    const bool cw = m_currentRms  >= CURRENT_WARN_A;
    const bool crit = m_rpm        >= SPEED_CRIT_RPM
                   || vibDynamic   >= VIB_CRIT_G
                   || m_currentRms >= CURRENT_WARN_A * 1.5f;

    if (sw   != m_speedWarning)   { m_speedWarning   = sw;   emit speedWarningChanged();   }
    if (vw   != m_vibWarning)     { m_vibWarning     = vw;   emit vibWarningChanged();     }
    if (cw   != m_currentWarning) { m_currentWarning = cw;   emit currentWarningChanged(); }
    if (crit != m_criticalAlert)  { m_criticalAlert  = crit; emit criticalAlertChanged();  }
}
