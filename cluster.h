#ifndef CLUSTER_H
#define CLUSTER_H

#pragma once
#include <QObject>
#include <QString>
#include <QVariant>
#include <qqml.h>
#include "SpiReader.h"    /* MotorSnapshot */

/* VehicleBackend: presents the motor sensor snapshot as QML properties.
 *
 * Sensor set (matches motor_wire.h v4):
 *   - 8 ADC channels (PA0..PA7, raw counts) -> currents[] + currentMean/currentMax
 *     (currentPhaseA/B/C kept as aliases for channels 0/1/2)
 *   - 3-axis vibration (MPU6050, ±2g, raw counts) -> vibX/Y/Z + vibTotal (g)
 *   - rpm (from tach input capture) -> rpm + speed (rpm alias for QML naming)
 *
 * Legacy properties still declared for QML compatibility (temp, voltage,
 * battery, power, and the temp/voltage warning flags) -- they stay at 0/false
 * since our v4 hardware doesn't measure them. If you're editing the QML, drop
 * the widgets for those; otherwise they'll show as zero/inactive. */
class VehicleBackend : public QObject
{
    Q_OBJECT
    QML_ELEMENT

    /* Real sensor properties. */
    Q_PROPERTY(float rpm             READ rpm             NOTIFY rpmChanged)
    Q_PROPERTY(float speed           READ speed           NOTIFY speedChanged)   /* == rpm, or scaled by wheel */

    Q_PROPERTY(QVariantList currents READ currents       NOTIFY currentsChanged) /* 8 scaled amps */
    Q_PROPERTY(float currentPhaseA   READ currentPhaseA   NOTIFY currentsChanged) /* == currents[0] */
    Q_PROPERTY(float currentPhaseB   READ currentPhaseB   NOTIFY currentsChanged) /* == currents[1] */
    Q_PROPERTY(float currentPhaseC   READ currentPhaseC   NOTIFY currentsChanged) /* == currents[2] */
    Q_PROPERTY(float currentMean     READ currentMean     NOTIFY currentsChanged)
    Q_PROPERTY(float currentMax      READ currentMax      NOTIFY currentsChanged) /* max |channel| */

    Q_PROPERTY(float vibX            READ vibX            NOTIFY vibChanged)
    Q_PROPERTY(float vibY            READ vibY            NOTIFY vibChanged)
    Q_PROPERTY(float vibZ            READ vibZ            NOTIFY vibChanged)
    Q_PROPERTY(float vibTotal        READ vibTotal        NOTIFY vibChanged)

    /* Legacy properties kept so existing QML doesn't need to change.
     * They read as 0 since we don't measure them.                       */
    Q_PROPERTY(float temp            READ temp            NOTIFY tempChanged)
    Q_PROPERTY(float voltage         READ voltage         NOTIFY voltageChanged)
    Q_PROPERTY(float battery         READ battery         NOTIFY batteryChanged)
    Q_PROPERTY(float power           READ power           NOTIFY powerChanged)
    Q_PROPERTY(float current         READ current         NOTIFY currentsChanged) /* == currentMean */

    /* Legacy warning flags referenced by Main.qml; no v4 source, always false. */
    Q_PROPERTY(bool tempWarning      READ tempWarning     NOTIFY tempWarningChanged)
    Q_PROPERTY(bool voltageWarning   READ voltageWarning  NOTIFY voltageWarningChanged)

    /* Warnings. */
    Q_PROPERTY(bool speedWarning     READ speedWarning    NOTIFY speedWarningChanged)
    Q_PROPERTY(bool vibWarning       READ vibWarning      NOTIFY vibWarningChanged)
    Q_PROPERTY(bool currentWarning   READ currentWarning  NOTIFY currentWarningChanged)
    Q_PROPERTY(bool criticalAlert    READ criticalAlert   NOTIFY criticalAlertChanged)

public:
    explicit VehicleBackend(QObject *parent = nullptr);
    ~VehicleBackend();

    /* Thresholds -- tune to your motor / test rig. */
    static constexpr float SPEED_WARN     = 3000.f;   /* RPM                    */
    static constexpr float SPEED_CRIT     = 5000.f;
    static constexpr float VIB_WARN_G     = 2.f;      /* total g magnitude      */
    static constexpr float VIB_CRIT_G     = 4.f;
    static constexpr float CURRENT_WARN_A = 50.f;     /* amps, after scaling    */

    /* Raw MPU6050 sensitivity at ±2g: 16384 counts per g. */
    static constexpr float MPU_COUNTS_PER_G = 16384.f;

    /* Current sensor scaling -- adjust for your INA199 / shunt combo.
     * Assumes bipolar current sense with 3.3V ADC and mid-rail zero. */
    static constexpr float ADC_MIDSCALE   = 2048.f;
    static constexpr float AMPS_PER_COUNT = 0.05f;    /* 100 A full-scale -> 0.05 A / count */

    float rpm()           const { return m_rpm; }
    float speed()         const { return m_rpm; }

    QVariantList  currents()    const { return m_currents; }
    float currentPhaseA() const { return m_currents.size() > 0 ? m_currents[0].toFloat() : 0.f; }
    float currentPhaseB() const { return m_currents.size() > 1 ? m_currents[1].toFloat() : 0.f; }
    float currentPhaseC() const { return m_currents.size() > 2 ? m_currents[2].toFloat() : 0.f; }
    float currentMean()   const { return m_currentMean; }
    float currentMax()    const { return m_currentMax; }
    float current()       const { return m_currentMean; }

    float vibX()          const { return m_vibX; }
    float vibY()          const { return m_vibY; }
    float vibZ()          const { return m_vibZ; }
    float vibTotal()      const { return m_vibTotal; }

    /* legacy no-signal fields */
    float temp()          const { return 0.f; }
    float voltage()       const { return 0.f; }
    float battery()       const { return 0.f; }
    float power()         const { return 0.f; }
    bool  tempWarning()   const { return false; }
    bool  voltageWarning() const { return false; }

    bool speedWarning()   const { return m_speedWarning; }
    bool vibWarning()     const { return m_vibWarning; }
    bool currentWarning() const { return m_currentWarning; }
    bool criticalAlert()  const { return m_criticalAlert; }

public slots:
    void onSpiData(MotorSnapshot snap);

signals:
    void rpmChanged();
    void speedChanged();
    void currentsChanged();
    void vibChanged();
    void tempChanged();
    void voltageChanged();
    void batteryChanged();
    void powerChanged();
    void tempWarningChanged();
    void voltageWarningChanged();

    void speedWarningChanged();
    void vibWarningChanged();
    void currentWarningChanged();
    void criticalAlertChanged();

private:
    void evaluateWarnings();

    SpiReader *m_spiReader = nullptr;

    float m_rpm         = 0.f;
    QVariantList m_currents;          /* 8 scaled channel currents (amps) */
    float m_currentMean = 0.f;
    float m_currentMax  = 0.f;        /* max |channel|, drives current warning */
    float m_vibX        = 0.f;
    float m_vibY        = 0.f;
    float m_vibZ        = 0.f;
    float m_vibTotal    = 0.f;

    bool m_speedWarning   = false;
    bool m_vibWarning     = false;
    bool m_currentWarning = false;
    bool m_criticalAlert  = false;
};

#endif // CLUSTER_H