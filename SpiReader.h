#ifndef SPIREADER_H
#define SPIREADER_H

#include <QThread>
#include <cstdint>

/* Snapshot delivered to the Qt main thread on every new sample. Mirrors
 * shm_snapshot_t/motor_row_t from motor_shm.h but declared here so Qt's
 * meta-object system can register it (Q_DECLARE_METATYPE below) without
 * pulling the whole motor_shm.h into every Qt include site.               */
/* PA0..PA7 assignment. Nothing in motor_wire.h names these -- it only
 * promises "scan order" -- so the mapping lives here and must match the
 * hardware. Getting it wrong silently produces plausible-looking nonsense.  */
enum MotorChannel {
    CH_CURRENT_0    = 0,
    CH_CURRENT_1    = 1,
    CH_CURRENT_2    = 2,
    CH_SPEED_CMD    = 3,   /* analog speed command, the speed source        */
    CH_VOLT_0       = 4,
    CH_VOLT_1       = 5,
    CH_VOLT_2       = 6,
    CH_DC_BUS_VOLT  = 7,
};

struct MotorSnapshot {
    uint16_t current[8];  /* raw 12-bit ADC counts, 8 channels PA0..PA7   */
                          /* (v4 wire: was 3 phases in v3)                */
    int16_t  vib_x;       /* MPU6050 accel, ±2g -> 16384 counts/g         */
    int16_t  vib_y;
    int16_t  vib_z;
    uint16_t rpm;         /* dead on this hardware -- no tach fitted      */
    uint32_t seq;         /* monotonic per block                          */
    uint64_t timestamp;   /* microseconds (STM ticks)                     */
    uint16_t flags;

    /* ---- Computed over a whole window of rows, not from one sample ----
     * The currents are sine and the phase voltages are square, so a single
     * instantaneous reading is a random point on the waveform and cannot be
     * scaled into anything meaningful. These come from the block ring: the
     * AC RMS about each channel's own mean, so they do not depend on knowing
     * where the sensor's zero sits.                                        */
    float i_rms[3];       /* raw ADC counts RMS, phases 0..2              */
    float v_rms[3];       /* raw ADC counts RMS, phases 0..2              */
    float v_dc;           /* DC bus, mean raw counts                      */
    float speed_cmd;      /* PA3, mean raw counts                         */
    uint32_t win_rows;    /* rows that went into the window (0 = no data) */
};
Q_DECLARE_METATYPE(MotorSnapshot)

/* Background thread that polls the shared-memory region produced by
 * motor_controller (MOTOR_SHM_NAME). Every time the snapshot's seq changes,
 * it emits newData() carrying a copy of the snapshot. */
class SpiReader : public QThread {
    Q_OBJECT
public:
    explicit SpiReader(QObject *parent = nullptr);
    void stop();

signals:
    void newData(MotorSnapshot data);

protected:
    void run() override;

    /* ---- Sampling of the 20 kHz stream --------------------------------
     * kRowStride: take every Nth row. 1 processes all 20 kHz samples.
     * kWindowBlocks: how many 10 ms blocks to accumulate before emitting.
     *
     * Defaults give 20000/4 = 5 kHz effective, averaged over 10 blocks =
     * 100 ms, so the readouts update at 10 Hz off 500 samples per channel.
     * At 100 ms the window spans several cycles of anything above ~50 Hz,
     * which is what stops the RMS wobbling with where the block happened
     * to start.
     *
     * NOTE on raising kRowStride: decimating without a low-pass filter
     * aliases everything above half the new rate. The phase voltages are
     * square waves, so they carry strong harmonics well above their
     * fundamental, and those fold back into the RMS. Cost is not the reason
     * to decimate here -- 200 rows x 8 channels at 100 Hz is nothing -- so
     * set this to 1 if the power reading looks unstable.                   */
    static constexpr uint32_t kRowStride    = 4;
    static constexpr uint32_t kWindowBlocks = 10;

private:
    volatile bool m_running = true;
    void *m_shm             = nullptr;
    size_t m_shm_size       = 0;
};

#endif // SPIREADER_H