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

/* Collect this sample into the 500ms display window, and republish the numerals
 * when it closes.
 *
 * The mean of the window, not a sample from it. Sampling twice a second would
 * hold the digit still just as well, but it would throw away 49 readings out of
 * 50 and hand the driver whichever one happened to land on the boundary. The
 * mean uses all of them, so the number is steadier *and* better.
 *
 * Fed the per-sample values, deliberately upstream of the low pass -- averaging
 * an already-averaged signal would smear the digit across two windows and make
 * it lag the ring by more than the half second the eye can forgive. */
void VehicleBackend::accumulateDisplay(float speedKmh, float powerW, float dt)
{
    m_winSpeedSum += speedKmh;
    m_winPowerSum += powerW;
    ++m_winCount;
    /* A run of unusable timestamps must not stall the digits, so the window
     * advances at the nominal block rate when dt is unavailable. */
    m_winElapsed += (dt > 0.f) ? dt : DT_NOMINAL_S;

    if (m_winElapsed < DISPLAY_PERIOD_S || m_winCount == 0)
        return;

    const float meanSpeed = static_cast<float>(m_winSpeedSum / m_winCount);
    const float meanPower = static_cast<float>(m_winPowerSum / m_winCount);

    float s = qnx_clamp(meanSpeed, 0.f, SPEED_MAX_KMH);
    if (s < SPEED_DEADBAND) s = 0.f;
    float p = qnx_clamp(meanPower, -POWER_MAX_W, POWER_MAX_W);
    if (std::abs(p) < POWER_DEADBAND) p = 0.f;

    m_winSpeedSum = 0.0;
    m_winPowerSum = 0.0;
    m_winCount = 0;
    m_winElapsed = 0.f;

    if (s != m_speedDisplay || p != m_powerDisplay) {
        m_speedDisplay = s;
        m_powerDisplay = p;
        emit displayChanged();
    }
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
     * A note on what this actually sees. Voltage and current are sampled at
     * 20kHz and arrive in blocks of 200 rows at 100Hz, but the snapshot holds
     * only the newest row -- so one sample in 200 reaches here, and which one
     * is arbitrary with respect to both the electrical cycle and the inverter's
     * switching.
     *
     * That is survivable for power only because the three-phase sum below is
     * steady through the cycle rather than pulsating, so it does not matter
     * where in the cycle the sample lands. What remains is switching noise,
     * which the 0.4s low pass averages over roughly forty samples.
     *
     * If this ever needs to be better -- true RMS, harmonics, anything
     * per-cycle -- the block ring is the place to get it. It carries every row
     * and motor_ai_client already reads it; the snapshot cannot be made to give
     * what it does not contain.
     *
     * The IMU is unaffected: it is sampled at 1kHz and zero-order held across
     * the 20kHz rows, so each row repeats a value twenty times and taking the
     * newest one loses nothing.
     *
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

    /* Paired through PhaseVoltageFor, not index-for-index: the voltage
     * channels sit one position rotated from the current channels. */
    const float va = static_cast<float>(snap.current[PhaseVoltageFor[0]]) * VOLTS_PER_COUNT_PHASE;
    const float vb = static_cast<float>(snap.current[PhaseVoltageFor[1]]) * VOLTS_PER_COUNT_PHASE;
    const float vc = static_cast<float>(snap.current[PhaseVoltageFor[2]]) * VOLTS_PER_COUNT_PHASE;

    m_busVoltage = static_cast<float>(snap.current[VoltageDcBus]) * VOLTS_PER_COUNT_BUS;

    /* Clarke magnitude: for a balanced set this is the phase amplitude and is
     * steady through the cycle, which a single |i| sample is not. Over root 2
     * for RMS. Drives the overcurrent warning and nothing else.              */
    const float iPeak = std::sqrt(2.f / 3.f * (ia * ia + ib * ib + ic * ic));
    m_currentRms = snap.derivedValid ? snap.currentRmsA : (iPeak / 1.41421356f);

    /* The ADC railing is its own fault, not an overcurrent: while it rails,
     * current and power both read LOW, so the overcurrent warning is exactly
     * the thing that will not fire. It reached 20% of samples under load on
     * the bench capture. */
    if (snap.currentClipping != m_currentClipping) {
        m_currentClipping = snap.currentClipping;
        emit currentWarningChanged();
    }

    /* The justification above is wrong in its reasoning but survivable in its
     * conclusion, and it is worth being precise about which.
     *
     * "The three-phase sum is steady through the cycle" holds for a balanced
     * set of SINUSOIDS. These phase voltages are not sinusoids: they are raw
     * PWM, each sample at either ~130 or ~2071 counts, and the 20kHz row rate
     * undersamples a carrier smeared across 3-6kHz. Per row the sum is not
     * steady at all -- at steady full throttle it ranges -20W to +815W.
     *
     * What rescues it is the averaging, not the steadiness: the samples are
     * uncorrelated with the switching, so the 0.4s low pass converges on the
     * true mean anyway. Measured against the block ring at full throttle:
     * 523W displayed against 512W actual, rippling +/-22W. Good enough for a
     * gauge. It only works with the phase pairing and the two voltage scales
     * corrected -- with the old index-for-index pairing it converged on about
     * a quarter of the true power.
     *
     * SPEED IS DIFFERENT and cannot be rescued this way. The snapshot is one
     * row in 200, so this path runs at 100Hz while the electrical fundamental
     * reaches 340Hz. It is aliased past Nyquist -- 340.2Hz folds to 40.2Hz and
     * cannot be told from a real 40Hz. No filter recovers it. Speed has to be
     * computed over the whole block: see MotorBlockAnalyzer.h, which is
     * written and validated but NOT yet wired up, because SpiReader mmaps only
     * the region header and snapshot prefix and the block ring's layout lives
     * in motor_shm.h, which is not in this repo. */
    /* Power now comes from the block ring, computed across all 200 rows by
     * MotorBlockAnalyzer, rather than from this single row. The row product
     * below is kept only as the fallback for a producer that publishes a
     * snapshot but no usable ring -- it converges on roughly the right mean
     * but carries the whole PWM scatter, because these voltages are raw PWM
     * and one row lands wherever the switching happened to be. */
    const float pRowFallback = va * ia + vb * ib + vc * ic;
    const float pInstant = snap.derivedValid ? snap.powerW : pRowFallback;

    /* --- speed -------------------------------------------------------------
     * From the throttle channel, not snap.rpm: no tach is fitted, so the wire's
     * rpm field is 0 on every row. Channel 7 carries the controller's speed
     * command, 1.1V closed to 4.2V full over 0..800 rpm.
     *
     * Clamped at both ends. Below 1.1V is throttle-closed, not negative speed;
     * above 4.2V is out of the controller's range and means a miscalibrated
     * divider rather than a motor over its rating, so it saturates instead of
     * reading something impossible.
     *
     * The median still earns its place. The command line is analogue and sits
     * next to a switching inverter, so it picks up impulses; those alias
     * straight through a low pass but not through a median.                   */
    const float throttleV = static_cast<float>(snap.current[VoltageSpeed]) * SPEED_V_PER_COUNT;
    m_throttle = qnx_clamp((throttleV - THROTTLE_V_MIN)
                           / (THROTTLE_V_MAX - THROTTLE_V_MIN), 0.f, 1.f);

    /* MEASURED shaft speed, from the electrical frequency of the phase
     * currents divided by the 26 pole pairs -- not the throttle command, which
     * is what this used to be and which is off by up to 636 rpm and biased in
     * opposite directions by regime (-130 accelerating, +121 loaded). The
     * command is still published as `throttle` for anyone who wants it; it is
     * simply no longer pretending to be a speed.
     *
     * When the block ring has nothing to measure -- motor stopped, or spinning
     * up too slowly for the angle tracker to lock -- this reads 0 rather than
     * falling back to the command, because a wrong speed presented as a
     * measurement is worse than an obviously absent one. */
    const float rpmRaw = medianRpm(snap.derivedValid ? snap.rpmMeasured : 0.f);

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
     * at zero and swallow a real climb out of it. Same for the saturation --
     * clamping the state would wind the filter up against its limit and make
     * it slow to come back down.
     *
     * Both gauges saturate at their dial's top rather than running past the
     * last mark, which the ring's glow would otherwise extrapolate off the end
     * of the artwork. */
    float speedKmh = qnx_clamp(m_rpm * KMH_PER_RPM, 0.f, SPEED_MAX_KMH);
    if (speedKmh < SPEED_DEADBAND)
        speedKmh = 0.f;

    /* Signed: regen is real and the sign is worth keeping in the property even
     * though the dial only sweeps positive. */
    float powerW = qnx_clamp(m_powerFiltered, -POWER_MAX_W, POWER_MAX_W);
    if (std::abs(powerW) < POWER_DEADBAND)
        powerW = 0.f;

    /* --- vibration --------------------------------------------------------- */
    const float vx = static_cast<float>(snap.vib_x) / MPU_COUNTS_PER_G;
    const float vy = static_cast<float>(snap.vib_y) / MPU_COUNTS_PER_G;
    const float vz = static_cast<float>(snap.vib_z) / MPU_COUNTS_PER_G;
    /* Gravity removed before the magnitude, which is what makes the warning
     * thresholds reachable at all. The raw vector is dominated by a constant
     * 1g (measured 0.968g, almost all of it on Z), so |v| sat at ~0.97g at
     * every speed and the old 2g/4g thresholds could never fire. Real AC
     * vibration on this rig runs 0.010g at standstill to 0.100g under load.
     *
     * The DC estimate is a slow one-pole rather than a fixed 1g subtraction:
     * it tracks whatever orientation the IMU is actually mounted at, so the
     * rig can be remounted without recalibrating. TAU_GRAVITY_S is far longer
     * than any vibration period and far shorter than a session. */
    const float aG = (dt > 0.f) ? dt / (TAU_GRAVITY_S + dt) : 0.f;
    if (!m_gravityPrimed) {
        m_gX = vx; m_gY = vy; m_gZ = vz; m_gravityPrimed = true;
    } else if (aG > 0.f) {
        m_gX += (vx - m_gX) * aG;
        m_gY += (vy - m_gY) * aG;
        m_gZ += (vz - m_gZ) * aG;
    }
    const float ax = vx - m_gX, ay = vy - m_gY, az = vz - m_gZ;
    const float vTotal = std::sqrt(ax * ax + ay * ay + az * az);

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

    /* Pre-filter values: the window does its own averaging, and feeding it the
     * low-passed ones would smooth twice. */
    accumulateDisplay(rpmRaw * KMH_PER_RPM, pInstant, dt);

    evaluateWarnings();
    evaluateHealth();
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
    evaluateHealth();
}

/* Recomputed whenever any of its inputs move -- vibration, current, or the
 * model's verdict. */
void VehicleBackend::evaluateHealth()
{
    const float vibFrac = qnx_clamp(std::abs(m_vibTotal - 1.f) / VIB_CRIT_G, 0.f, 1.f);
    const float curFrac = qnx_clamp(m_currentRms / (CURRENT_WARN_A * 1.5f), 0.f, 1.f);

    float h = 1.f - std::max(vibFrac, curFrac);
    if (m_aiAlert)
        h = std::min(h, AI_ALERT_HEALTH);
    h = qnx_clamp(h, 0.f, 1.f);

    if (h != m_health) {
        m_health = h;
        emit healthChanged();
    }
}

void VehicleBackend::evaluateWarnings()
{
    /* Vibration warning uses (total - 1g) so gravity does not trip it. */
    /* m_vibTotal is ALREADY gravity-free -- the accelerometer's DC vector is
     * tracked and subtracted where it is computed. This used to read
     * abs(m_vibTotal - 1.f), which was the right correction back when
     * m_vibTotal carried gravity, and became a double subtraction the moment
     * that changed: 0.03g of real vibration came out as abs(0.03 - 1.0) =
     * 0.97g, which pinned the vibration warning on permanently and showed
     * E-31 on a healthy machine. */
    const float vibDynamic = m_vibTotal;

    /* Against rpm, not the converted speed: the thresholds are a property of
     * the motor, and KMH_PER_RPM is a property of the rig it is bolted to. */
    const bool sw = m_rpm         >= SPEED_WARN_RPM;
    const bool vw = vibDynamic    >= VIB_WARN_G;
    const bool cw = m_currentRms  >= CURRENT_WARN_A;
    /* Speed is absent from this on purpose -- see SPEED_WARN_RPM. */
    const bool crit = vibDynamic   >= VIB_CRIT_G
                   || m_currentRms >= CURRENT_WARN_A * 1.5f;

    if (sw   != m_speedWarning)   { m_speedWarning   = sw;   emit speedWarningChanged();   }
    if (vw   != m_vibWarning)     { m_vibWarning     = vw;   emit vibWarningChanged();     }
    if (cw   != m_currentWarning) { m_currentWarning = cw;   emit currentWarningChanged(); }
    if (crit != m_criticalAlert)  { m_criticalAlert  = crit; emit criticalAlertChanged();  }
}
