#ifndef MOTORBLOCKANALYZER_H
#define MOTORBLOCKANALYZER_H

#include <cmath>
#include <cstdint>
#include <cstddef>

/* MotorBlockAnalyzer -- shaft speed and real power from a block of raw rows.
 *
 * WHY THIS EXISTS
 * ---------------
 * VehicleBackend::onSpiData sees one row per block (100 Hz). The electrical
 * fundamental of this motor runs to 340 Hz at full throttle, so at 100 Hz it is
 * aliased past Nyquist -- 340.2 Hz folds to 40.2 Hz and is indistinguishable
 * from a real 40 Hz. No amount of filtering downstream recovers it.
 *
 * Both speed and power therefore have to be computed over the whole 20 kHz row
 * block, which is what this class does. Feed it every row of a block; read the
 * results once per block.
 *
 * Measured against a 36 s / 728800-row bench capture (motor_find_speed.csv):
 *   speed  RMS error 7.1 rpm vs an FFT reference, no FFT used here
 *   power  stable to ~18% over 0.2 s windows, versus +/-240% per-row scatter
 *          for the instantaneous product this replaces
 */
class MotorBlockAnalyzer {
public:
    /* ---- machine constants ------------------------------------------------
     * POLE_PAIRS was measured, not assumed: the vibration 1x rotational order
     * was tracked against the integrated electrical angle over the whole
     * capture. 26 wins by 4.7x over the next candidate, and the 2x mechanical
     * order agrees, which a coincidence would not do. 52 poles.             */
    static constexpr int   POLE_PAIRS   = 26;

    /* Sampling rate of the rows inside a block (config.sample_rate_hz).
     * Only the default -- setRowRateHz() takes the real one, derived from the
     * per-row timestamps the producer publishes alongside the rows, so a
     * producer reconfigured to a different sample rate does not silently
     * rescale every speed reading. */
    static constexpr float ROW_RATE_HZ_DEFAULT = 20000.f;

    void setRowRateHz(float hz) { if (hz > 1.f) m_rowRateHz = hz; }
    float rowRateHz() const { return m_rowRateHz; }

    /* ---- ADC scaling ------------------------------------------------------
     * The phase-voltage channels and the DC-bus channel DO NOT share a divider.
     * A phase pinned to the positive rail reads 2071 counts while the bus reads
     * 2633, consistently across every throttle setting -- so they need separate
     * constants. Scaling the phases with the bus constant under-reads them by
     * 21%.
     *
     * VBUS_VOLTS is the one number to re-measure if the pack changes; both
     * voltage constants derive from it.                                      */
    static constexpr float VBUS_VOLTS       = 48.0f;   /* nameplate */
    static constexpr float BUS_COUNTS       = 2633.f;  /* measured, resting */
    static constexpr float PHASE_RAIL_COUNTS= 2071.f;  /* measured, phase at rail */
    static constexpr float VOLTS_PER_COUNT_BUS   = VBUS_VOLTS / BUS_COUNTS;
    static constexpr float VOLTS_PER_COUNT_PHASE = VBUS_VOLTS / PHASE_RAIL_COUNTS;

    /* CALIBRATE ME. This is the only constant not pinned by the capture: it
     * sets the absolute power scale and nothing in the data determines it.
     * 0.0085 makes full throttle come out at 454 W against a 450 W nameplate,
     * which is self-consistent but NOT an independent confirmation -- it was
     * derived from that nameplate. To fix it properly, put a clamp meter on one
     * phase at steady full throttle and solve
     *     AMPS_PER_COUNT = I_phase_peak_measured / 1790
     * (1790 counts is the measured space-vector amplitude there).            */
    static constexpr float AMPS_PER_COUNT   = 0.0085f;

    static constexpr float ADC_MIDSCALE     = 2047.f;  /* measured at rest */

    /* Below this the machine is not turning usefully and the angle tracker has
     * nothing to lock to; speed reads 0 rather than noise. Counts, space-vector
     * amplitude. Idle measures ~9. */
    static constexpr float I_RUNNING_COUNTS = 250.f;

    /* The current channel rails at 0/4095. It reached 20% of samples during
     * loaded running in the capture, so this is a live condition, not a
     * theoretical one -- above it, current and power both read low. */
    static constexpr float CLIP_WARN_FRACTION = 0.02f;

    /* Corner frequency of the pre-filter that feeds the angle tracker. Must
     * sit above the highest electrical frequency (340 Hz here) and well below
     * the switching ripple. Its phase lag is constant and so cancels out of a
     * frequency measurement entirely. */
    static constexpr float ANGLE_LP_HZ = 900.f;

    /* Clears the per-block accumulators ONLY. The pre-filter and the previous
     * sample deliberately survive: the filter needs continuity to stay settled,
     * and carrying the last sample across the boundary means no block loses the
     * rotation that happened across its first row. */
    void reset() { m_n = 0; m_nAng = 0; m_sumDth = 0.f;
                   m_accP = 0.f; m_accI2 = 0.f; m_clip = 0; }

    /* Drop the cross-block continuity too. Called when the ring lapped or a
     * slot tore: the previous sample is then from an unknown distance in the
     * past, and differencing against it would fabricate a rotation that never
     * happened. */
    void resetHistory() { m_have = false; m_fAl = 0.f; m_fBe = 0.f;
                          m_pAl = 0.f; m_pBe = 0.f; }

    /* Feed one row. `ch` is the raw 8-channel ADC array, in WIRE order.
     * Caller supplies the channel indices so the mapping lives in one place.  */
    void addRow(const uint16_t *ch, int iA, int iB, int iC,
                                    int vA, int vB, int vC)
    {
        const float ia = float(ch[iA]) - ADC_MIDSCALE;
        const float ib = float(ch[iB]) - ADC_MIDSCALE;
        const float ic = float(ch[iC]) - ADC_MIDSCALE;

        if (ch[iA] >= 4090 || ch[iA] <= 5 ||
            ch[iB] >= 4090 || ch[iB] <= 5 ||
            ch[iC] >= 4090 || ch[iC] <= 5) ++m_clip;

        /* Force sum(i)=0. The three sensors carry a small common-mode term and
         * the phase voltages carry a large common-mode one; their product is a
         * pure artefact, so remove the current side before it can appear. */
        const float cm = (ia + ib + ic) * (1.f / 3.f);
        const float a  = ia - cm, b = ib - cm, c = ic - cm;

        /* Clarke. For a balanced set |(al,be)| is the phase amplitude and is
         * steady through the cycle, unlike any single phase sample. */
        const float ial = (2.f/3.f) * (a - 0.5f*b - 0.5f*c);
        const float ibe = (2.f/3.f) * 0.8660254f * (b - c);

        const float val = (2.f/3.f) * (float(ch[vA]) - 0.5f*float(ch[vB]) - 0.5f*float(ch[vC]));
        const float vbe = (2.f/3.f) * 0.8660254f * (float(ch[vB]) - float(ch[vC]));

        /* Real power, averaged over the block. P = 3/2 * Re(v . conj(i)) in the
         * stationary frame. The phase voltages are raw PWM -- 0 or rail, never
         * the sinusoid they represent -- so this is only meaningful averaged
         * over the block, never per row. Zero-sequence drops out of Clarke, so
         * the divider offset cancels here the same way it would in sum(v*i). */
        m_accP  += 1.5f * (val * ial + vbe * ibe);
        m_accI2 += ial*ial + ibe*ibe;

        /* One-pole low pass ahead of the angle tracker, and ONLY ahead of it --
         * power and RMS want the unfiltered signal. Without this the tracker
         * does not work at all: switching ripple moves the space vector further
         * between consecutive samples than the fundamental does (6 deg at 340 Hz
         * and 20 kHz), so the per-sample increments come out random-signed and
         * average to zero. Measured 1 rpm instead of 780 before this was added. */
        const float k = 1.f - std::exp(-2.f * 3.14159265f * ANGLE_LP_HZ / m_rowRateHz);
        m_fAl += k * (ial - m_fAl);
        m_fBe += k * (ibe - m_fBe);

        /* Angle rate by cross product -- the FFT-free speed measurement.
         * dTheta ~ (al[n-1]*be[n] - al[n]*be[n-1]) / |i|^2, summed over the
         * block. No atan2, no unwrap, and it cannot slip a cycle as long as the
         * rotation is under half a turn per sample (340 Hz at 20 kHz is 6
         * degrees, so there is a 30x margin). */
        const float mag2 = m_fAl*m_fAl + m_fBe*m_fBe;
        if (m_have && mag2 > (I_RUNNING_COUNTS*I_RUNNING_COUNTS)) {
            const float cross = m_pAl * m_fBe - m_fAl * m_pBe;
            const float dot   = m_pAl * m_fAl + m_pBe * m_fBe;
            m_sumDth += std::atan2(cross, dot);   /* small-angle, well conditioned */
            ++m_nAng;
        }
        m_pAl = m_fAl; m_pBe = m_fBe; m_have = true;
        ++m_n;
    }

    /* Require most of the block to have cleared the running-current gate, not
     * just one row. A block caught mid spin-up can have a handful of qualifying
     * samples whose angle sum is meaningless; that produced a single 1404 rpm
     * spike on the bench capture before this was tightened. */
    bool valid() const { return m_n > 1 && m_nAng > (m_n * 4) / 5; }

    /* Signed electrical frequency, Hz. Sign is direction of rotation. */
    float electricalHz() const
    {
        if (!valid()) return 0.f;
        return (m_sumDth / (2.f * 3.14159265f)) * (m_rowRateHz / float(m_nAng));
    }

    /* Shaft speed, rpm. This is a measurement of the shaft, not a restatement
     * of the throttle: it stays correct while accelerating and under load,
     * where the throttle command is wrong by up to 636 rpm. */
    float rpm() const { return std::fabs(electricalHz()) * 60.f / float(POLE_PAIRS); }

    /* Real electrical power, watts. Signed -- negative is regeneration. */
    float watts() const
    {
        if (m_n == 0) return 0.f;
        return (m_accP / float(m_n)) * VOLTS_PER_COUNT_PHASE * AMPS_PER_COUNT;
    }

    /* Phase current, amps RMS. */
    float currentRms() const
    {
        if (m_n == 0) return 0.f;
        return std::sqrt(m_accI2 / float(m_n)) * AMPS_PER_COUNT * 0.70710678f;
    }

    bool currentClipping() const
    {
        return m_n > 0 && float(m_clip) / float(m_n) > CLIP_WARN_FRACTION;
    }

private:
    float m_rowRateHz = ROW_RATE_HZ_DEFAULT;
    bool  m_have  = false;
    float m_fAl = 0.f, m_fBe = 0.f;   /* pre-filter state, survives reset() */
    int   m_n     = 0;
    int   m_nAng  = 0;
    int   m_clip  = 0;
    float m_pAl = 0.f, m_pBe = 0.f;
    float m_sumDth = 0.f;
    float m_accP   = 0.f;
    float m_accI2  = 0.f;
};

#endif /* MOTORBLOCKANALYZER_H */
