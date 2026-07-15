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

    /* --- RPM ---
     * Already in RPM units from the STM's timer input-capture math.    */
    const float r = static_cast<float>(snap.rpm);
    if (r != m_rpm) {
        m_rpm = r;
        emit rpmChanged();
        emit speedChanged();
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