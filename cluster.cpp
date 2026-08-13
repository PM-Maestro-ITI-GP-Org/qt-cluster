#include "cluster.h"
#include <QDebug>
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
    SLOG_INFO("SpiReader thread started (motor_controller shm consumer)");
}

VehicleBackend::~VehicleBackend()
{
    if (m_spiReader) {
        m_spiReader->stop();
        m_spiReader->wait(1000);
    }
}

/* Slot: called every block from the reader thread via QueuedConnection.
 * Runs on the Qt main thread, safe to touch QML-visible state.        */
void VehicleBackend::onSpiData(MotorSnapshot snap)
{
    /* --- Currents (8 ADC channels PA0..PA7) ---
     * Raw ADC is 0..4095. Convert to amps assuming a bipolar current-sense
     * amp with mid-rail zero (adjust ADC_MIDSCALE/AMPS_PER_COUNT in the
     * header for your hardware). All 8 channels are scaled uniformly; if some
     * channels carry a different quantity, split the scaling here.          */
    QVariantList amps;
    amps.reserve(8);
    float sum = 0.f, maxAbs = 0.f;
    bool changed = (m_currents.size() != 8);
    for (int i = 0; i < 8; ++i) {
        const float a = (static_cast<float>(snap.current[i]) - ADC_MIDSCALE) * AMPS_PER_COUNT;
        amps.append(a);
        sum += a;
        if (std::abs(a) > maxAbs) maxAbs = std::abs(a);
        if (!changed && m_currents[i].toFloat() != a) changed = true;
    }
    const float iMean = sum / 8.f;

    /* --- Speed, from the PA3 analog command ---
     * The tach input is not fitted, so snap.rpm stays 0 and cannot drive
     * anything. PA3 carries the speed command as a voltage instead, averaged
     * over the window by the reader.                                        */
    const float speed = (snap.speed_cmd - SPEED_CMD_ZERO_COUNTS) * KMH_PER_COUNT;
    const float speedClamped = speed < 0.f ? 0.f : speed;

    /* --- Power, from the phase RMS values ---
     * Per phase V_rms * I_rms, summed over the three phases. Both RMS values
     * come from a whole window of rows -- a single sample of a sine or a
     * square is a random point on the waveform and cannot be scaled into
     * anything.
     *
     * This is apparent power, not real: multiplying the two RMS values
     * separately drops the phase angle between them, so it reads high by the
     * power factor (on a motor, always). Add a fixed POWER_FACTOR below, or
     * average the instantaneous v*i product in the reader, if that matters.  */
    float watts = 0.f;
    for (int p = 0; p < 3; ++p) {
        const float irms = snap.i_rms[p] * AMPS_PER_COUNT;
        const float vrms = snap.v_rms[p] * VOLTS_PER_COUNT;
        watts += vrms * irms;
    }
    watts *= POWER_FACTOR;

    if (changed) {
        m_currents    = amps;
        m_currentMean = iMean;
        m_currentMax  = maxAbs;
        emit currentsChanged();
    }

    /* --- Vibration (three axes + magnitude) ---
     * MPU6050 at ±2g: 16384 counts/g. Total = vector magnitude in g.
     * Note: at rest, |vec| ≈ 1g (from gravity), not 0.                  */
    const float vx = static_cast<float>(snap.vib_x) / MPU_COUNTS_PER_G;
    const float vy = static_cast<float>(snap.vib_y) / MPU_COUNTS_PER_G;
    const float vz = static_cast<float>(snap.vib_z) / MPU_COUNTS_PER_G;
    const float vTotal = std::sqrt(vx*vx + vy*vy + vz*vz);

    if (vx != m_vibX || vy != m_vibY || vz != m_vibZ) {
        m_vibX = vx;
        m_vibY = vy;
        m_vibZ = vz;
        m_vibTotal = vTotal;
        emit vibChanged();
    }

    if (speedClamped != m_rpm) {
        m_rpm = speedClamped;
        emit rpmChanged();
        emit speedChanged();
    }

    if (watts != m_power) {
        m_power = watts;
        emit powerChanged();
    }

    evaluateWarnings();
}

void VehicleBackend::evaluateWarnings()
{
    /* Vibration warning uses (total - 1g) so gravity doesn't trip it. */
    const float vibDynamic = std::abs(m_vibTotal - 1.f);

    const bool sw = m_rpm         >= SPEED_WARN;
    const bool vw = vibDynamic    >= VIB_WARN_G;
    const bool cw = m_currentMax  >= CURRENT_WARN_A;
    const bool crit = m_rpm       >= SPEED_CRIT
                   || vibDynamic  >= VIB_CRIT_G
                   || m_currentMax >= CURRENT_WARN_A * 1.5f;

    if (sw   != m_speedWarning)   { m_speedWarning   = sw;   emit speedWarningChanged();   }
    if (vw   != m_vibWarning)     { m_vibWarning     = vw;   emit vibWarningChanged();     }
    if (cw   != m_currentWarning) { m_currentWarning = cw;   emit currentWarningChanged(); }
    if (crit != m_criticalAlert)  { m_criticalAlert  = crit; emit criticalAlertChanged();  }
}