#include "SpiReader.h"

/* --- Shared-memory contract -------------------------------------------
 *
 * The real layout lives in giga_spi/motor_shm.h (shm_region_t /
 * shm_snapshot_t), but that header uses C11 _Atomic / <stdatomic.h>. On
 * this toolchain <stdatomic.h>'s C++ compat shim only activates for C++23
 * (see .../usr/include/c++/12.2.0/stdatomic.h: "#if __cplusplus > 202002L"),
 * and this project builds C++14 on QCC (CMakeLists.txt: "safer than 17 on
 * QCC") -- so motor_shm.h cannot be #included from a .cpp here.
 *
 * Instead, the structs below mirror shm_region_t byte-for-byte, using
 * std::atomic in place of the producer's C11 _Atomic (same size/alignment for
 * lock-free 4- and 8-byte types, so the layout matches). The whole region is
 * mirrored and mapped, ring included: the waveform only exists there, and the
 * snapshot's single row is one arbitrary point on a 20 kHz sine.
 *
 * MOTOR_SHM_NAME and the cache-line size below MUST stay in sync with
 * giga_spi/motor_shm.h if that file changes.
 * -------------------------------------------------------------------- */
#define _Static_assert static_assert   /* motor_wire.h uses C11 _Static_assert */
#include "motor_wire.h"                 /* motor_row_t -- pure data layout, no atomics.
                                         * Vendored copy of the authoritative header from
                                         * the producer tree (giga_spi_8adc/motor_wire.h);
                                         * keep in sync -- ideally a git submodule.       */
#undef _Static_assert

#include <fcntl.h>
#include <sys/mman.h>
#include <unistd.h>
#include <cstring>
#include <cmath>
#include <climits>
#include <atomic>
#include <QDebug>

namespace {

constexpr const char *kMotorShmName  = "/motor_ctrl"; // motor_shm.h: MOTOR_SHM_NAME
constexpr size_t      kMotorCacheline = 64;            // motor_shm.h: MOTOR_CACHELINE
constexpr uint32_t    kMotorShmMagic  = 0x4D435452u;   // motor_shm.h: MOTOR_SHM_MAGIC ("MCTR")

struct MotorSnapshotShm {
    alignas(kMotorCacheline) std::atomic<uint32_t> seqlock;
    uint32_t     producer_seq;
    uint64_t     timestamp;
    uint16_t     flags;
    uint16_t     _pad;
    motor_row_t  row;
};

/* motor_shm.h: MOTOR_RING_DEPTH. That header cannot be included here (C11
 * _Atomic), so this mirrors it and must be kept in step.                   */
constexpr uint32_t kMotorRingDepth = 16;

struct MotorBlockShm {
    alignas(kMotorCacheline) std::atomic<uint32_t> seq;
    uint32_t    producer_seq;
    uint64_t    timestamp;
    uint16_t    n_rows;
    uint16_t    flags;
    uint64_t    row_ts[MOTOR_MAX_ROWS_PER_BLOCK];
    motor_row_t rows[MOTOR_MAX_ROWS_PER_BLOCK];
};

struct MotorRingShm {
    alignas(kMotorCacheline) std::atomic<uint64_t> write_pos;
    uint32_t      depth;
    uint32_t      _pad;
    /* Named `blocks`, not `slots`: Qt defines `slots` as a keyword macro
     * that expands to nothing, so a member of that name will not compile. */
    MotorBlockShm blocks[kMotorRingDepth];
};

/* The whole region now, not just the header+snapshot prefix: the block ring
 * is the only place the waveform exists. The snapshot carries a single row,
 * which is one arbitrary point on a 20 kHz sine and cannot be averaged.    */
struct ShmRegionHeader {
    uint32_t magic;
    uint16_t version;
    uint16_t _pad0;
    uint32_t row_size;
    uint32_t reserved;
    alignas(kMotorCacheline) MotorSnapshotShm snapshot;
    alignas(kMotorCacheline) MotorRingShm     ring;
};

/* Running sum/sum-of-squares per channel, reset every window. */
struct ChannelAccum {
    double sum[8]   = {0};
    double sumsq[8] = {0};
    uint32_t n      = 0;

    void add(const motor_row_t &r)
    {
        for (int c = 0; c < 8; ++c) {
            const double v = static_cast<double>(r.current[c]);
            sum[c]   += v;
            sumsq[c] += v * v;
        }
        ++n;
    }
    void reset() { *this = ChannelAccum{}; }

    double mean(int c) const { return n ? sum[c] / n : 0.0; }

    /* AC RMS about the channel's own mean. Using the measured mean rather
     * than an assumed mid-rail zero means this works whether the sensor is
     * bipolar around 2048 or unipolar, with no per-channel offset to set. */
    double acRms(int c) const
    {
        if (!n) return 0.0;
        const double m  = sum[c] / n;
        const double ms = sumsq[c] / n - m * m;
        return ms > 0.0 ? std::sqrt(ms) : 0.0;
    }
};

} // namespace

SpiReader::SpiReader(QObject *parent) : QThread(parent)
{
    qRegisterMetaType<MotorSnapshot>("MotorSnapshot");
}

void SpiReader::run()
{
    /* Retry shm_open for up to 5s in case motor_controller is starting
     * slightly after us.                                                 */
    int fd = -1;
    for (int attempt = 0; attempt < 20 && m_running; ++attempt) {
        fd = shm_open(kMotorShmName, O_RDONLY, 0);
        if (fd != -1) break;
        usleep(250000);
    }
    if (fd == -1) {
        qWarning("SpiReader: shm_open(%s) failed -- is motor_controller running?",
                 kMotorShmName);
        return;
    }

    m_shm_size = sizeof(ShmRegionHeader);   /* header + snapshot + full ring */
    m_shm = mmap(nullptr, m_shm_size, PROT_READ, MAP_SHARED, fd, 0);
    close(fd);
    if (m_shm == MAP_FAILED) {
        qWarning("SpiReader: mmap failed");
        m_shm = nullptr;
        return;
    }

    const ShmRegionHeader *region = static_cast<const ShmRegionHeader *>(m_shm);

    /* Contract guard: refuse to read a region written by a producer with a
     * different wire/shm layout, otherwise we'd emit garbage. Mirrors the
     * checks in motor_shm_region_valid().                                    */
    if (region->magic != kMotorShmMagic
        || region->version != static_cast<uint16_t>(MOTOR_CONTRACT_VERSION)
        || region->row_size != static_cast<uint32_t>(sizeof(motor_row_t))) {
        qWarning("SpiReader: shm contract mismatch "
                 "(magic=0x%08x ver=%u row=%u; expected 0x%08x ver=%u row=%zu) -- not reading",
                 region->magic, region->version, region->row_size,
                 kMotorShmMagic, (unsigned)MOTOR_CONTRACT_VERSION, sizeof(motor_row_t));
        munmap(m_shm, m_shm_size);
        m_shm = nullptr;
        return;
    }

    const MotorRingShm *ring = &region->ring;

    /* Start at the newest block rather than 0: the ring holds 160 ms of
     * history we have no reason to replay, and starting behind would just
     * make us permanently lapped.                                          */
    uint64_t cursor = ring->write_pos.load(std::memory_order_acquire);

    ChannelAccum acc;
    uint32_t     blocks_in_window = 0;
    uint64_t     dropped          = 0;
    MotorBlockShm blk;

    while (m_running) {
        const uint64_t head = ring->write_pos.load(std::memory_order_acquire);

        /* Lapped: the producer wrote more than the ring holds while we were
         * away. Skip to the oldest slot still intact instead of reading
         * torn ones.                                                        */
        if (head - cursor > kMotorRingDepth) {
            dropped += (head - cursor) - kMotorRingDepth;
            cursor = head - kMotorRingDepth;
        }

        while (cursor < head) {
            const MotorBlockShm *slot = &ring->blocks[cursor % kMotorRingDepth];

            /* Per-slot seqlock, same shape as the snapshot's: odd means the
             * producer is mid-write, and an unchanged even value across the
             * copy means it did not overwrite us.                           */
            bool ok = false;
            for (int tries = 0; tries < 100; ++tries) {
                const uint32_t s1 = slot->seq.load(std::memory_order_acquire);
                if (s1 & 1u) continue;
                uint16_t n = slot->n_rows;
                if (n > MOTOR_MAX_ROWS_PER_BLOCK) n = MOTOR_MAX_ROWS_PER_BLOCK;
                blk.n_rows       = n;
                blk.producer_seq = slot->producer_seq;
                blk.timestamp    = slot->timestamp;
                blk.flags        = slot->flags;
                std::memcpy(blk.rows, slot->rows, size_t(n) * sizeof(motor_row_t));
                const uint32_t s2 = slot->seq.load(std::memory_order_acquire);
                if (s1 == s2) { ok = true; break; }
            }
            ++cursor;
            if (!ok) { ++dropped; continue; }

            for (uint32_t i = 0; i < blk.n_rows; i += kRowStride)
                acc.add(blk.rows[i]);
            ++blocks_in_window;

            if (blocks_in_window >= kWindowBlocks && acc.n > 0) {
                MotorSnapshot out{};

                /* Latest raw row, for vibration and for anything that wants
                 * an instantaneous value.                                   */
                const motor_row_t &last = blk.rows[blk.n_rows ? blk.n_rows - 1 : 0];
                for (int c = 0; c < 8; ++c) out.current[c] = last.current[c];
                out.vib_x     = last.vib_x;
                out.vib_y     = last.vib_y;
                out.vib_z     = last.vib_z;
                out.rpm       = last.rpm;
                out.seq       = blk.producer_seq;
                out.timestamp = blk.timestamp;
                out.flags     = blk.flags;

                for (int p = 0; p < 3; ++p) {
                    out.i_rms[p] = float(acc.acRms(CH_CURRENT_0 + p));
                    out.v_rms[p] = float(acc.acRms(CH_VOLT_0 + p));
                }
                out.v_dc      = float(acc.mean(CH_DC_BUS_VOLT));
                out.speed_cmd = float(acc.mean(CH_SPEED_CMD));
                out.win_rows  = acc.n;

                emit newData(out);

                acc.reset();
                blocks_in_window = 0;
            }
        }

        /* One window is kWindowBlocks * 10 ms; poll a few times inside it so
         * a window boundary is never delayed by a whole poll interval.      */
        usleep(2000);
    }

    if (dropped)
        qWarning("SpiReader: %llu blocks dropped (consumer lapped)",
                 (unsigned long long)dropped);

    munmap(m_shm, m_shm_size);
    m_shm = nullptr;
}

void SpiReader::stop()
{
    m_running = false;
}