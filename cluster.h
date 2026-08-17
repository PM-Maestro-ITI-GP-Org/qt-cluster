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

    /* --- the two the cluster displays ------------------------------------
     * Each comes in two forms, and they are not interchangeable:
     *
     *   speed / power                continuous, every block. For the ring's
     *                                glow, which should move smoothly.
     *   speedDisplay / powerDisplay  the mean over a 500ms window, republished
     *                                at 2 Hz. For the numerals, which should
     *                                hold still long enough to be read.
     *
     * See DISPLAY_PERIOD_S for why. Binding the digits to the continuous one
     * is the mistake this pair exists to prevent.                            */
    Q_PROPERTY(float speed           READ speed           NOTIFY speedChanged)   /* km/h, filtered */
    Q_PROPERTY(float power           READ power           NOTIFY powerChanged)   /* watts, filtered, signed */
    Q_PROPERTY(float speedDisplay    READ speedDisplay    NOTIFY displayChanged) /* km/h, 2 Hz */
    Q_PROPERTY(float powerDisplay    READ powerDisplay    NOTIFY displayChanged) /* watts, 2 Hz */
    Q_PROPERTY(float displayHz       READ displayHz       CONSTANT)
    Q_PROPERTY(float rpm             READ rpm             NOTIFY rpmChanged)     /* filtered, pre-conversion */

    /* --- derived electricals, not displayed but useful to bind ----------- */
    Q_PROPERTY(float currentRms      READ currentRms      NOTIFY electricalChanged)
    Q_PROPERTY(float busVoltage      READ busVoltage      NOTIFY electricalChanged)
    Q_PROPERTY(float throttle        READ throttle        NOTIFY speedChanged)   /* 0..1 */

    /* Full-scale values, so the QML generates its dial labels from the same
     * numbers the readings are clamped to and the two cannot disagree. */
    Q_PROPERTY(float speedMax        READ speedMax        CONSTANT)
    Q_PROPERTY(float powerMax        READ powerMax        CONSTANT)

    /* 0..1. What the right-hand band shows. */
    Q_PROPERTY(float health          READ health          NOTIFY healthChanged)

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
    /* CONFLICT, RESOLVED FROM THE DATA -- read this before trusting either.
     *
     * The map below used to read VoltageA=3, VoltageB=4, VoltageC=5,
     * VoltageDcBus=6, VoltageSpeed=7, attributed to MotorDataService.fidl.
     * A 728800-row bench capture disagrees, and the raw signals settle it:
     *
     *   ch3  smooth, 1056 counts closed to 4095 full, no switching content
     *        -> the speed/throttle channel, NOT phase A
     *   ch4..6  bimodal 130/2071 counts, switching, 120 deg apart, fundamental
     *        at the electrical frequency -> the three phase voltages
     *   ch7  2633 counts, std 11, immovable under load -> the DC bus
     *
     * So the fidl-derived map had the speed channel and the bus swapped in and
     * shifted the three phase voltages by one. With the old map the cluster
     * read the throttle as phase A and the bus as the throttle.
     *
     * Confirm against the STM32's actual ADC1 scan order (PA0..PA7) before
     * shipping -- one of the two sources is stale and it is worth knowing
     * which. */
    enum Channel {
        CurrentA = 0, CurrentB = 1, CurrentC = 2,
        VoltageSpeed = 3,
        VoltageA = 4, VoltageB = 5, VoltageC = 6,
        VoltageDcBus = 7,
    };

    /* Phase-voltage channels are rotated one position relative to the current
     * channels: VoltageB pairs with CurrentA, VoltageC with CurrentB,
     * VoltageA with CurrentC. Pairing them index-for-index puts the current
     * 80 deg out from its own phase voltage and drops the apparent power
     * factor to 0.16; with this rotation it is 0.77 lagging, which is what a
     * loaded PMSM should show. */
    static constexpr int PhaseVoltageFor[3] = { VoltageB, VoltageC, VoltageA };

    /* ---- ADC scaling ------------------------------------------------------
     * CALIBRATE THESE. The producer carries current_scale/rpm_scale in its
     * config.json but does not put them in shm, and they all ship at 1.0, so
     * what arrives here is raw counts and the conversion has to live here.
     *
     * 12-bit ADC on a 3.3V reference. Currents are bipolar through a sense amp
     * with mid-rail zero; voltages come off a divider and are unipolar.       */
    static constexpr float ADC_MIDSCALE     = 2047.f;  /* measured at rest */

    /* The phase channels and the bus channel DO NOT share a divider, so one
     * VOLTS_PER_COUNT cannot serve both. With the bus at rest reading 2633
     * counts, a phase clamped to the rail reads 2071 -- consistently, at every
     * throttle setting. Using the bus constant for the phases under-reads them
     * by 21%. Both derive from VBUS_VOLTS, which is the only one to re-measure
     * if the pack changes. */
    static constexpr float VBUS_VOLTS            = 48.f;
    static constexpr float VOLTS_PER_COUNT_BUS   = VBUS_VOLTS / 2633.f;
    static constexpr float VOLTS_PER_COUNT_PHASE = VBUS_VOLTS / 2071.f;

    /* CALIBRATE ME -- the only constant the capture cannot pin down, and the
     * one that sets the absolute power scale. 0.05 was 6x too high: it put the
     * phase current at 68 A rms and full-throttle power at 2.3 kW on a 450 W
     * machine. 0.0085 lands full throttle at ~450-510 W, but that was derived
     * from the nameplate, so it is self-consistent rather than independently
     * confirmed. Clamp-meter one phase at steady full throttle and solve
     *     AMPS_PER_COUNT = I_phase_peak / 1790
     * (1790 counts being the measured space-vector amplitude there). */
    static constexpr float AMPS_PER_COUNT   = 0.0085f;

    /* ---- speed, from the throttle channel ---------------------------------
     * The wire's `rpm` field is dead -- no tach is fitted, so it reads 0 on
     * every row. Speed is reconstructed from channel 7, the controller's speed
     * command, which swings 1.1V (closed) to 4.2V (full) across the motor's
     * 0..800 rpm range.
     *
     * This is a COMMAND, not a measurement. It says what speed is being asked
     * for, not what the shaft is doing: under load the motor sits below it, and
     * a stalled motor still reads full throttle. Nothing here can tell the
     * difference. Wire a tach into the rpm field and this whole path should go.
     *
     * 4.2V is above the 3.3V ADC reference, so the signal must reach the pin
     * through a divider. SPEED_DIVIDER is that ratio and is a guess -- 2:1 puts
     * the range at 0.55..2.1V, comfortably inside the ADC. CALIBRATE IT: read
     * raw counts at closed and full throttle and solve for the two ends.      */
    /* 52 poles. Measured, not assumed: the vibration 1x rotational order was
     * tracked against the integrated electrical angle across the whole capture
     * and 26 pole pairs wins by 4.7x over the next candidate, with the 2x
     * order agreeing. rpm = electrical_Hz * 60 / 26. */
    static constexpr int   POLE_PAIRS       = 26;

    /* Measured ceiling: full throttle settles at 340.2 Hz electrical = 786 rpm.
     * Rounded up so the real maximum sits just inside the dial rather than
     * pinning it. */
    static constexpr float RPM_MAX          = 790.f;
    static constexpr float THROTTLE_V_MIN   = 1.1f;
    static constexpr float THROTTLE_V_MAX   = 4.2f;
    static constexpr float ADC_VREF         = 3.3f;
    static constexpr float ADC_COUNTS       = 4095.f;
    static constexpr float SPEED_DIVIDER    = 2.0f;
    static constexpr float SPEED_V_PER_COUNT = ADC_VREF / ADC_COUNTS * SPEED_DIVIDER;

    /* Dial top, and the only number to change to rescale the speed gauge --
     * the labels are generated from it. 800 rpm maps here exactly, so the
     * needle uses the full sweep and saturates rather than running past the
     * last mark. Equivalent to a 1.25m wheel driven direct; adjust for the
     * rig's real wheel and reduction.                                         */
    static constexpr float SPEED_MAX_KMH    = 60.f;
    static constexpr float KMH_PER_RPM      = SPEED_MAX_KMH / RPM_MAX;

    /* Power dial top. The motor is rated 450W, so the old 0..6kW scale left the
     * needle in the bottom 7% of its sweep. Watts, not kilowatts -- at this
     * size kW would read 0.4 and never move.                                  */
    /* 450 (the nameplate) is not enough headroom: the machine actually draws
     * ~512W at steady full throttle on the bench, so a 450 dial reads pinned at
     * maximum the entire time it is held there and the needle stops carrying
     * information exactly when it matters. 600 puts the real maximum at ~85% of
     * sweep with room for the load transients above it. */
    static constexpr float POWER_MAX_W      = 600.f;

    /* ---- filtering --------------------------------------------------------
     * Time constants in seconds, applied as a one-pole low pass against the
     * wire's own timestamps rather than a fixed rate, so a stalled or bursty
     * producer does not change how much smoothing happens.
     *
     * Power gets the longer one: it is a product of two noisy channels, so its
     * noise is roughly the sum of both in relative terms.                     */
    /* Gravity tracker for the vibration path: long enough to pass every real
     * vibration through untouched, short enough to settle after a remount. */
    static constexpr float TAU_GRAVITY_S    = 5.0f;

    static constexpr float TAU_SPEED_S      = 0.25f;
    static constexpr float TAU_POWER_S      = 0.40f;

    /* Below these, the reading is snapped to zero. Without it a standstill
     * shows a wandering last digit, which reads as a fault in the sensor.    */
    static constexpr float SPEED_DEADBAND   = 0.5f;    /* km/h */
    static constexpr float POWER_DEADBAND   = 20.f;    /* W    */

    /* ---- how often the NUMERALS change ------------------------------------
     * 2 Hz. Production clusters default to twice a second for digital speed
     * (BMW's is 2 Hz, codeable to 5 or 10), and the human-factors reason is
     * the one that matters here: a digit that changes faster than the eye can
     * fix on it stops being a reading and becomes clutter.
     *
     * This is a convention, not a requirement. ISO 15008 governs legibility --
     * character size, contrast, colour -- and says nothing about update rate,
     * so there is no figure to comply with. 500ms is the industry default and
     * is what this uses.
     *
     * It applies to the NUMERALS ONLY. The ring's glow keeps running off the
     * continuous value at frame rate: the guidance is that the analogue
     * indicator carries the perception and the digits confirm it, and holding
     * a moving graphic at 2 Hz would just make it judder. So the two are
     * deliberately allowed to disagree by up to half a second of travel.
     *
     * The published number is the MEAN over the window, not a sample of it.
     * At a 100Hz block rate that is ~50 samples, which is both a better
     * estimate than any one of them and the thing that makes the digit steady
     * rather than merely slow.                                               */
    static constexpr float DISPLAY_PERIOD_S = 0.5f;
    /* Used to advance the window when a snapshot arrives with an unusable
     * timestamp, so a run of them cannot stall the display. Nominal block
     * rate: 100Hz. */
    static constexpr float DT_NOMINAL_S     = 0.01f;

    /* Guard against a stalled or restarted producer: a dt outside this is not
     * used to advance the filters. */
    static constexpr float DT_MIN_S         = 1e-4f;
    static constexpr float DT_MAX_S         = 0.5f;

    static constexpr int   RPM_MEDIAN_N     = 5;       /* tach glitch rejection */

    /* Raw MPU6050 sensitivity at +/-2g. */
    static constexpr float MPU_COUNTS_PER_G = 16384.f;

    /* ---- thresholds -- tune to the rig -------------------------------------
     * Scaled to this motor. The previous 3000/5000 rpm pair was sized for a
     * machine six times faster and could never have fired.
     *
     * There is no speed CRITICAL any more. Speed is reconstructed from a
     * command that is clamped to full throttle, so it cannot report an
     * overspeed -- a runaway shaft would look identical to full throttle. That
     * needs a tach, not a threshold. What is left warns at near-maximum, which
     * is a real thing to show.                                                */
    static constexpr float SPEED_WARN_RPM   = RPM_MAX * 0.95f;
    /* AC ONLY -- gravity must be removed before these are applied.
     *
     * The old 2g/4g pair could never fire. vibTotal is the magnitude of the
     * raw accelerometer vector, which at rest is 1g of gravity (measured: the
     * Z axis sits at 0.968g) and barely moves: total AC vibration across the
     * whole capture ranged 0.010g at standstill to 0.100g under load. So the
     * band sat at ~0.97g forever and the thresholds were 20x out of reach.
     *
     * Set from the measured floor: 0.10g is normal loaded running, so warn at
     * 3x that and call 0.6g critical. Retune once a genuinely faulty run has
     * been recorded -- these are headroom over a healthy baseline, not
     * observed fault levels. */
    static constexpr float VIB_WARN_G       = 0.30f;
    static constexpr float VIB_CRIT_G       = 0.60f;
    /* ---- health -------------------------------------------------------------
     * The right-hand band. Not a sensor reading -- there is no health sensor --
     * but not invented either: it is the remaining margin to whichever measured
     * limit is closest, which is what "how is it doing" actually means here.
     *
     *     health = 1 - max(vibration/critical, current/critical)
     *
     * Worst-of rather than a weighted blend, so no arbitrary weights decide
     * which fault matters more, and the band answers one question: how much
     * headroom is left before something trips.
     *
     * A model alert caps it instead of subtracting from it. The verdict is a
     * yes or no with no severity attached, so it cannot be scaled into the
     * margin -- what it can do is say the margin is not the whole story.
     *
     * Temperature is absent because nothing measures it. When a thermistor
     * exists, it belongs in the max() alongside the other two.
     *
     * Motor temperature used to drive this band, which read 0 forever on
     * hardware for the same reason.                                          */
    static constexpr float AI_ALERT_HEALTH  = 0.35f;

    /* The bus measures 48V, not 24V, so 450W is ~9.4A DC. Phase current at
     * full throttle measured 10.8A rms (1790 counts peak) at the calibration
     * above. Warn a little over that. Scales with AMPS_PER_COUNT, so recheck
     * this when that is calibrated properly. */
    static constexpr float CURRENT_WARN_A   = 13.f;

    /* The current channel rails at 0/4095 counts. This is not hypothetical:
     * it reached 20% of samples during loaded running in the capture, and
     * while it is railing both current and power read low. Worth its own
     * telltale rather than being folded into the overcurrent warning. */
    static constexpr float CLIP_WARN_FRACTION = 0.02f;

    float speed()         const { return m_speedKmh; }
    float power()         const { return m_powerW; }
    float speedDisplay()  const { return m_speedDisplay; }
    float powerDisplay()  const { return m_powerDisplay; }
    float displayHz()     const { return 1.f / DISPLAY_PERIOD_S; }
    float rpm()           const { return m_rpm; }
    float currentRms()    const { return m_currentRms; }
    float busVoltage()    const { return m_busVoltage; }
    float throttle()      const { return m_throttle; }
    float speedMax()      const { return SPEED_MAX_KMH; }
    float powerMax()      const { return POWER_MAX_W; }

    float health()        const { return m_health; }

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
    void displayChanged();
    void rpmChanged();
    void electricalChanged();
    void vibChanged();
    void aiChanged();
    void healthChanged();

    void speedWarningChanged();
    void vibWarningChanged();
    void currentWarningChanged();
    void criticalAlertChanged();

private:
    void evaluateWarnings();
    void evaluateHealth();
    float dtFrom(quint64 timestampUs);
    float medianRpm(float sample);
    void accumulateDisplay(float speedKmh, float powerW, float dt);

    SpiReader *m_spiReader = nullptr;
    AiReader  *m_aiReader  = nullptr;

    float m_rpm         = 0.f;   /* filter state */
    float m_speedKmh    = 0.f;   /* published, deadbanded */
    float m_powerFiltered = 0.f; /* filter state */
    float m_powerW      = 0.f;   /* published, deadbanded */
    bool  m_primed      = false; /* first snapshot seen */

    /* 2 Hz display window: running sums of the per-sample values, and the
     * elapsed time in the window so far. Summed, not sampled -- the published
     * number is their mean. */
    float m_speedDisplay = 0.f;
    float m_powerDisplay = 0.f;
    double m_winSpeedSum = 0.0;
    double m_winPowerSum = 0.0;
    int    m_winCount    = 0;
    float  m_winElapsed  = 0.f;
    float m_currentRms  = 0.f;
    float m_busVoltage  = 0.f;
    float m_throttle    = 0.f;   /* 0..1, from the speed command channel */
    float m_health      = 1.f;   /* 0..1, margin to the nearest limit */
    /* Slow-tracked gravity vector, subtracted from the accelerometer before
     * the vibration magnitude is taken. */
    float m_gX = 0.f, m_gY = 0.f, m_gZ = 0.f;
    bool  m_gravityPrimed = false;

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
