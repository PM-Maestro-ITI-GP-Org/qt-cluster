#ifndef CLUSTER_H
#define CLUSTER_H

#pragma once
#include <QObject>
#include <QString>
#include <QVariant>
#include <qqml.h>
#include "SpiReader.h"    /* MotorSnapshot */
#include "AiReader.h"     /* AiResults     */

/* VehicleBackend: turns the two shared-memory feeds into QML properties.
 *
 *   /motor_ctrl        motor-data-producer, via SpiReader. Sensor rows.
 *   /motor_ai_result   motor_ai_client, via AiReader. Model verdicts.
 *
 * ---------------------------------------------------------------------------
 * What the eight ADC channels actually are
 *
 * motor_wire.h calls the array `current[8]` and says only "eight ADC1 channels,
 * PA0..PA7 (scan order)". They are NOT eight currents. motor_ai_client's
 * interface/MotorDataService.fidl names them, and MotorDataClient.cpp maps them
 * index by index:
 *
 *     [0] currentA      [3] voltageA        [6] voltageDcBus
 *     [1] currentB      [4] voltageB        [7] voltageSpeed
 *     [2] currentC      [5] voltageC
 *
 * The previous version of this file scaled all eight as currents and averaged
 * them, so `currentMean` was three phase currents averaged with five voltages
 * and meant nothing. Anything reading the old currents[] array wants checking.
 *
 * This matters most for power: with phase voltages measured as well as phase
 * currents, real power is a direct product of the two and needs no assumed bus
 * voltage or power factor.
 * ------------------------------------------------------------------------- */
class VehicleBackend : public QObject
{
    Q_OBJECT
    QML_ELEMENT

    /* --- the two the cluster displays ------------------------------------ */
    Q_PROPERTY(float speed           READ speed           NOTIFY speedChanged)   /* km/h, filtered */
    Q_PROPERTY(float power           READ power           NOTIFY powerChanged)   /* watts, filtered, signed */
    Q_PROPERTY(float rpm             READ rpm             NOTIFY rpmChanged)     /* filtered, pre-conversion */

    /* --- derived electricals, not displayed but useful to bind ----------- */
    Q_PROPERTY(float currentRms      READ currentRms      NOTIFY electricalChanged)
    Q_PROPERTY(float busVoltage      READ busVoltage      NOTIFY electricalChanged)

    Q_PROPERTY(float vibX            READ vibX            NOTIFY vibChanged)
    Q_PROPERTY(float vibY            READ vibY            NOTIFY vibChanged)
    Q_PROPERTY(float vibZ            READ vibZ            NOTIFY vibChanged)
    Q_PROPERTY(float vibTotal        READ vibTotal        NOTIFY vibChanged)

    /* --- AI verdicts ------------------------------------------------------ */
    Q_PROPERTY(QString aiAnomaly     READ aiAnomaly       NOTIFY aiChanged)
    Q_PROPERTY(QString aiFaultClass  READ aiFaultClass    NOTIFY aiChanged)
    Q_PROPERTY(QString aiPredMaint   READ aiPredMaint     NOTIFY aiChanged)
    Q_PROPERTY(bool aiAlert          READ aiAlert         NOTIFY aiChanged)
    Q_PROPERTY(bool aiConnected      READ aiConnected     NOTIFY aiChanged)

    /* --- still unmeasured. No sensor exists for either on the v4 wire, so
     * both read 0 and the widgets bound to them sit empty on hardware.     */
    Q_PROPERTY(float temp            READ temp            CONSTANT)
    Q_PROPERTY(float battery         READ battery         CONSTANT)

    /* --- warnings --------------------------------------------------------- */
    Q_PROPERTY(bool speedWarning     READ speedWarning    NOTIFY speedWarningChanged)
    Q_PROPERTY(bool vibWarning       READ vibWarning      NOTIFY vibWarningChanged)
    Q_PROPERTY(bool currentWarning   READ currentWarning  NOTIFY currentWarningChanged)
    Q_PROPERTY(bool criticalAlert    READ criticalAlert   NOTIFY criticalAlertChanged)

public:
    explicit VehicleBackend(QObject *parent = nullptr);
    ~VehicleBackend();

    /* ---- channel map, from MotorDataService.fidl ------------------------- */
    enum Channel {
        CurrentA = 0, CurrentB = 1, CurrentC = 2,
        VoltageA = 3, VoltageB = 4, VoltageC = 5,
        VoltageDcBus = 6, VoltageSpeed = 7,
    };

    /* ---- ADC scaling ------------------------------------------------------
     * CALIBRATE THESE. The producer carries current_scale/rpm_scale in its
     * config.json but does not put them in shm, and they all ship at 1.0, so
     * what arrives here is raw counts and the conversion has to live here.
     *
     * 12-bit ADC on a 3.3V reference. Currents are bipolar through a sense amp
     * with mid-rail zero; voltages come off a divider and are unipolar.       */
    static constexpr float ADC_MIDSCALE     = 2048.f;
    static constexpr float AMPS_PER_COUNT   = 0.05f;   /* 100 A full scale     */
    static constexpr float VOLTS_PER_COUNT  = 0.1f;    /* ~410 V full scale    */

    /* rpm -> km/h. rpm is already RPM (timer input capture, rpm_scale 1.0).
     * Wheel circumference in metres over the reduction between motor and
     * wheel; both are rig properties, not measurements.                       */
    static constexpr float WHEEL_CIRCUM_M   = 1.90f;
    static constexpr float GEAR_RATIO       = 8.0f;
    static constexpr float KMH_PER_RPM      = WHEEL_CIRCUM_M * 60.f / 1000.f / GEAR_RATIO;

    /* ---- filtering --------------------------------------------------------
     * Time constants in seconds, applied as a one-pole low pass against the
     * wire's own timestamps rather than a fixed rate, so a stalled or bursty
     * producer does not change how much smoothing happens.
     *
     * Power gets the longer one: it is a product of two noisy channels, so its
     * noise is roughly the sum of both in relative terms.                     */
    static constexpr float TAU_SPEED_S      = 0.25f;
    static constexpr float TAU_POWER_S      = 0.40f;

    /* Below these, the reading is snapped to zero. Without it a standstill
     * shows a wandering last digit, which reads as a fault in the sensor.    */
    static constexpr float SPEED_DEADBAND   = 0.5f;    /* km/h */
    static constexpr float POWER_DEADBAND   = 20.f;    /* W    */

    /* Guard against a stalled or restarted producer: a dt outside this is not
     * used to advance the filters. */
    static constexpr float DT_MIN_S         = 1e-4f;
    static constexpr float DT_MAX_S         = 0.5f;

    static constexpr int   RPM_MEDIAN_N     = 5;       /* tach glitch rejection */

    /* Raw MPU6050 sensitivity at +/-2g. */
    static constexpr float MPU_COUNTS_PER_G = 16384.f;

    /* ---- thresholds -- tune to the rig ----------------------------------- */
    static constexpr float SPEED_WARN_RPM   = 3000.f;
    static constexpr float SPEED_CRIT_RPM   = 5000.f;
    static constexpr float VIB_WARN_G       = 2.f;
    static constexpr float VIB_CRIT_G       = 4.f;
    static constexpr float CURRENT_WARN_A   = 50.f;

    float speed()         const { return m_speedKmh; }
    float power()         const { return m_powerW; }
    float rpm()           const { return m_rpm; }
    float currentRms()    const { return m_currentRms; }
    float busVoltage()    const { return m_busVoltage; }

    float vibX()          const { return m_vibX; }
    float vibY()          const { return m_vibY; }
    float vibZ()          const { return m_vibZ; }
    float vibTotal()      const { return m_vibTotal; }

    QString aiAnomaly()    const { return m_ai.anomaly; }
    QString aiFaultClass() const { return m_ai.faultClass; }
    QString aiPredMaint()  const { return m_ai.predMaint; }
    bool    aiAlert()      const { return m_aiAlert; }
    bool    aiConnected()  const { return m_aiConnected; }

    float temp()          const { return 0.f; }
    float battery()       const { return 0.f; }

    bool speedWarning()   const { return m_speedWarning; }
    bool vibWarning()     const { return m_vibWarning; }
    bool currentWarning() const { return m_currentWarning; }
    bool criticalAlert()  const { return m_criticalAlert; }

public slots:
    void onSpiData(MotorSnapshot snap);
    void onAiResults(AiResults results);

signals:
    void speedChanged();
    void powerChanged();
    void rpmChanged();
    void electricalChanged();
    void vibChanged();
    void aiChanged();

    void speedWarningChanged();
    void vibWarningChanged();
    void currentWarningChanged();
    void criticalAlertChanged();

private:
    void evaluateWarnings();
    float dtFrom(quint64 timestampUs);
    float medianRpm(float sample);

    SpiReader *m_spiReader = nullptr;
    AiReader  *m_aiReader  = nullptr;

    float m_rpm         = 0.f;   /* filter state */
    float m_speedKmh    = 0.f;   /* published, deadbanded */
    float m_powerFiltered = 0.f; /* filter state */
    float m_powerW      = 0.f;   /* published, deadbanded */
    bool  m_primed      = false; /* first snapshot seen */
    float m_currentRms  = 0.f;
    float m_busVoltage  = 0.f;
    float m_vibX        = 0.f;
    float m_vibY        = 0.f;
    float m_vibZ        = 0.f;
    float m_vibTotal    = 0.f;

    quint64 m_lastTimestamp = 0;
    float   m_rpmWindow[RPM_MEDIAN_N] = {0};
    int     m_rpmWindowCount = 0;
    int     m_rpmWindowPos = 0;

    AiResults m_ai;
    bool m_aiAlert     = false;
    bool m_aiConnected = false;

    bool m_speedWarning   = false;
    bool m_vibWarning     = false;
    bool m_currentWarning = false;
    bool m_criticalAlert  = false;
};

#endif // CLUSTER_H
