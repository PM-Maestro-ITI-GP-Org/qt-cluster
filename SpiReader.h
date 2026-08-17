#ifndef SPIREADER_H
#define SPIREADER_H

#include <QThread>
#include <cstdint>

/* Snapshot delivered to the Qt main thread on every new sample. Mirrors
 * shm_snapshot_t/motor_row_t from motor_shm.h but declared here so Qt's
 * meta-object system can register it (Q_DECLARE_METATYPE below) without
 * pulling the whole motor_shm.h into every Qt include site.               */
struct MotorSnapshot {
    uint16_t current[8];  /* raw 12-bit ADC counts, 8 channels PA0..PA7   */
                          /* (v4 wire: was 3 phases in v3)                */
    int16_t  vib_x;       /* MPU6050 accel, ±2g -> 16384 counts/g         */
    int16_t  vib_y;
    int16_t  vib_z;
    uint16_t rpm;         /* already in RPM units from timer capture      */
    uint32_t seq;         /* monotonic per block                          */
    uint64_t timestamp;   /* microseconds (STM ticks)                     */
    uint16_t flags;

    /* ---- derived from the block ring, not from this row -------------------
     * Computed over all 200 rows of the block by MotorBlockAnalyzer, because
     * neither number can be got from a single row: speed is aliased past
     * Nyquist at the snapshot rate, and power needs averaging across the PWM.
     * Valid only when derivedValid is true -- during spin-up, or with the
     * motor stopped, the angle tracker has nothing to lock to. */
    bool  derivedValid = false;
    float rpmMeasured  = 0.f;   /* shaft rpm, from electrical frequency / 26 */
    float powerW       = 0.f;   /* real power, signed; negative is regen     */
    float currentRmsA  = 0.f;   /* phase current, amps RMS                   */
    bool  currentClipping = false;  /* ADC railing -- readings under-report  */
    uint32_t blocksDropped = 0; /* ring laps since the last emit             */
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

private:
    volatile bool m_running = true;
    void *m_shm             = nullptr;
    size_t m_shm_size       = 0;
};

#endif // SPIREADER_H