#include "SpiReader.h"
#include "MotorBlockAnalyzer.h"
#include "cluster.h"   /* VehicleBackend::Channel + PhaseVoltageFor: one channel map */

/* --- Shared-memory contract -------------------------------------------
 *
 * The real layout lives in motor-data-producer's motor_shm.h (shm_region_t /
 * shm_snapshot_t), but that header uses C11 _Atomic / <stdatomic.h>. On
 * this toolchain <stdatomic.h>'s C++ compat shim only activates for C++23
 * (see .../usr/include/c++/12.2.0/stdatomic.h: "#if __cplusplus > 202002L"),
 * and this project builds C++14 on QCC (CMakeLists.txt: "safer than 17 on
 * QCC") -- so motor_shm.h cannot be #included from a .cpp here.
 *
 * Instead, the structs below mirror shm_region_t's header + shm_snapshot_t
 * byte-for-byte, using std::atomic<uint32_t> in place of the producer's
 * C11 _Atomic uint32_t seqlock (same size/alignment for a lock-free 4-byte
 * type, so the layout matches). We never touch the block ring, so we only
 * mirror the region header + snapshot and mmap just that prefix.
 *
 * MOTOR_SHM_NAME and the cache-line size below MUST stay in sync with
 * motor-data-producer's motor_shm.h if that file changes; the static_asserts
 * after the structs are what catch it if they do not.
 * -------------------------------------------------------------------- */
#define _Static_assert static_assert   /* motor_wire.h uses C11 _Static_assert */
#include "motor_wire.h"                 /* motor_row_t -- pure data layout, no atomics.
                                         * Vendored copy of the authoritative header from
                                         * the producer tree (motor-data-producer/motor_wire.h);
                                         * keep in sync -- ideally a git submodule.       */
#undef _Static_assert

#include <fcntl.h>
#include <sys/mman.h>
#include <unistd.h>
#include <cstddef>
#include <cstring>
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

struct ShmRegionHeader {
    uint32_t magic;
    uint16_t version;
    uint16_t _pad0;
    uint32_t row_size;
    uint32_t reserved;
    alignas(kMotorCacheline) MotorSnapshotShm snapshot;
};

/* Checked against motor_shm.h compiled as C11 -- every offset below was read
 * off the real shm_region_t, not derived from this one. The mirror is only the
 * prefix; the real region continues into the block ring at 128 and runs to
 * 103616 bytes, which is why the sizeof check is against the prefix and the
 * mmap length is sizeof(ShmRegionHeader).
 *
 * A drift here means reading a field out of the middle of another one, so it
 * fails the build rather than the run. */
static_assert(sizeof(ShmRegionHeader) == 128, "prefix size drifted from shm_region_t");
static_assert(offsetof(ShmRegionHeader, magic) == 0, "magic moved");
static_assert(offsetof(ShmRegionHeader, version) == 4, "version moved");
static_assert(offsetof(ShmRegionHeader, row_size) == 8, "row_size moved");
static_assert(offsetof(ShmRegionHeader, reserved) == 12, "reserved moved");
static_assert(offsetof(ShmRegionHeader, snapshot) == 64, "snapshot moved");
static_assert(offsetof(ShmRegionHeader, snapshot.producer_seq) == 68, "producer_seq moved");
static_assert(offsetof(ShmRegionHeader, snapshot.timestamp) == 72, "timestamp moved");
static_assert(offsetof(ShmRegionHeader, snapshot.flags) == 80, "flags moved");
static_assert(offsetof(ShmRegionHeader, snapshot.row) == 84, "row moved");

/* ---- the block ring ---------------------------------------------------
 * The snapshot is one row in 200. Speed cannot be recovered from it at all --
 * the electrical fundamental reaches 340Hz while the snapshot path runs at
 * 100Hz, so it is aliased past Nyquist. The ring carries every row, which is
 * why this mirror had to grow past the prefix.
 *
 * Offsets again read off the real shm_block_t / shm_block_ring_t compiled as
 * C11, not derived here. */
constexpr uint32_t kMotorRingDepth = 16;   // motor_shm.h: MOTOR_RING_DEPTH

struct MotorBlockShm {
    alignas(kMotorCacheline) std::atomic<uint32_t> seq;
    uint32_t     producer_seq;
    uint64_t     timestamp;
    uint16_t     n_rows;
    uint16_t     flags;
    uint64_t     row_ts[MOTOR_MAX_ROWS_PER_BLOCK];
    motor_row_t  rows[MOTOR_MAX_ROWS_PER_BLOCK];
};

struct MotorRingShm {
    alignas(kMotorCacheline) std::atomic<uint64_t> write_pos;
    uint32_t      depth;
    uint32_t      _pad;
    /* Named `blocks`, not `slots` as in shm_block_ring_t: Qt defines `slots`
     * as a macro for the signals/slots syntax, so the member declaration
     * expanded to nothing and the struct silently lost its array. Only the
     * LAYOUT has to match the producer, not the field names, and the
     * static_asserts below are what actually hold the two together. */
    MotorBlockShm blocks[kMotorRingDepth];
};

struct ShmRegionFull {
    ShmRegionHeader header;
    alignas(kMotorCacheline) MotorRingShm ring;
};

static_assert(sizeof(MotorBlockShm) == 6464, "shm_block_t size drifted");
static_assert(offsetof(MotorBlockShm, producer_seq) == 4, "block producer_seq moved");
static_assert(offsetof(MotorBlockShm, timestamp) == 8, "block timestamp moved");
static_assert(offsetof(MotorBlockShm, n_rows) == 16, "block n_rows moved");
static_assert(offsetof(MotorBlockShm, flags) == 18, "block flags moved");
static_assert(offsetof(MotorBlockShm, row_ts) == 24, "block row_ts moved");
static_assert(offsetof(MotorBlockShm, rows) == 1624, "block rows moved");
static_assert(sizeof(MotorRingShm) == 103488, "shm_block_ring_t size drifted");
static_assert(offsetof(MotorRingShm, depth) == 8, "ring depth moved");
static_assert(offsetof(MotorRingShm, blocks) == 64, "ring slots moved");
static_assert(sizeof(ShmRegionFull) == 103616, "shm_region_t size drifted");
static_assert(offsetof(ShmRegionFull, ring) == 128, "ring moved");

} // namespace

SpiReader::SpiReader(QObject *parent) : QThread(parent)
{
    qRegisterMetaType<MotorSnapshot>("MotorSnapshot");
}

/* Copy the snapshot fields into our MotorSnapshot. Called between two
 * seqlock reads by the retry loop below.                                  */
static void copy_snapshot(const MotorSnapshotShm *src, MotorSnapshot *dst)
{
    for (int i = 0; i < 8; ++i)
        dst->current[i] = src->row.current[i];
    dst->vib_x      = src->row.vib_x;
    dst->vib_y      = src->row.vib_y;
    dst->vib_z      = src->row.vib_z;
    dst->rpm        = src->row.rpm;
    dst->seq        = src->producer_seq;
    dst->timestamp  = src->timestamp;
    dst->flags      = src->flags;
}

void SpiReader::run()
{
    /* Retry shm_open for up to 5s in case motor_data_producer is starting
     * slightly after us.                                                 */
    int fd = -1;
    for (int attempt = 0; attempt < 20 && m_running; ++attempt) {
        fd = shm_open(kMotorShmName, O_RDONLY, 0);
        if (fd != -1) break;
        usleep(250000);
    }
    if (fd == -1) {
        qWarning("SpiReader: shm_open(%s) failed -- is motor_data_producer running?",
                 kMotorShmName);
        return;
    }

    m_shm_size = sizeof(ShmRegionHeader);
    /* The whole region now, not just the prefix: the ring starts at 128 and
     * the block data is the only place speed can come from. */
    m_shm_size = sizeof(ShmRegionFull);
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

    const MotorSnapshotShm *snap = &region->snapshot;
    const MotorRingShm *ring =
        &static_cast<const ShmRegionFull *>(m_shm)->ring;

    /* Start from what the producer writes NEXT. The ring holds ~160ms and has
     * certainly lapped since it was created, so beginning at slot 0 would
     * report every stale slot as a drop -- which reads as a performance fault
     * and is really just the gap before we started looking. Same reasoning as
     * shm_reader_resync() in motor_ai_client. */
    uint64_t readPos = ring->write_pos.load(std::memory_order_acquire);

    MotorBlockAnalyzer analyzer;
    bool  haveDerived    = false;
    float derivedRpm     = 0.f;
    float derivedPower   = 0.f;
    float derivedCurrent = 0.f;
    bool  derivedClip    = false;
    uint32_t dropped     = 0;

    uint32_t last_seq = UINT32_MAX;

    while (m_running) {
        MotorSnapshot local;

        /* Seqlock read: the acquire load itself provides the ordering
         * (everything after it in program order can't be reordered before
         * it), so no separate fence calls are needed.                     */
        int tries = 0;
        for (;; ++tries) {
            uint32_t s1 = snap->seqlock.load(std::memory_order_acquire);
            if (s1 & 1u) { /* writer in progress */ if (tries > 100) break; continue; }
            copy_snapshot(snap, &local);
            uint32_t s2 = snap->seqlock.load(std::memory_order_acquire);
            if (s1 == s2) break;               /* clean read              */
            if (tries > 100) break;            /* pathological: give up   */
        }
        /* ------------------------------------------------------------- */

        /* ---- drain the block ring -------------------------------------
         * Every block since the last poll, in order. The analyzer's filter
         * state deliberately carries across blocks (reset() clears only the
         * accumulators), so the boundary is not a discontinuity. */
        const uint64_t writePos = ring->write_pos.load(std::memory_order_acquire);

        if (writePos - readPos > kMotorRingDepth) {
            /* Lapped: the producer overran us. Skip to the oldest slot still
             * intact rather than reading torn ones, and count what was lost.
             * Then the analyzer's history is stale, so drop its continuity. */
            dropped += static_cast<uint32_t>(writePos - readPos - kMotorRingDepth);
            readPos = writePos - kMotorRingDepth;
            analyzer.resetHistory();
        }

        for (; readPos < writePos; ++readPos) {
            const MotorBlockShm *slot = &ring->blocks[readPos % kMotorRingDepth];

            /* Per-slot seqlock: the producer may overwrite this slot while we
             * copy it. Read the rows, then re-check; on a torn read the block
             * is skipped rather than fed to the analyzer as garbage. */
            const uint32_t b1 = slot->seq.load(std::memory_order_acquire);
            if (b1 & 1u) break;              /* being written -- come back later */

            uint16_t n = slot->n_rows;
            if (n > MOTOR_MAX_ROWS_PER_BLOCK) n = MOTOR_MAX_ROWS_PER_BLOCK;
            if (n < 2) continue;

            /* Row rate from the producer's own per-row timestamps, so a
             * reconfigured sample rate cannot silently rescale every speed. */
            const uint64_t dtUs = slot->row_ts[1] - slot->row_ts[0];
            if (dtUs > 0)
                analyzer.setRowRateHz(1e6f / static_cast<float>(dtUs));

            analyzer.reset();
            for (uint16_t i = 0; i < n; ++i)
                analyzer.addRow(slot->rows[i].current,
                                VehicleBackend::CurrentA,
                                VehicleBackend::CurrentB,
                                VehicleBackend::CurrentC,
                                VehicleBackend::PhaseVoltageFor[0],
                                VehicleBackend::PhaseVoltageFor[1],
                                VehicleBackend::PhaseVoltageFor[2]);

            const uint32_t b2 = slot->seq.load(std::memory_order_acquire);
            if (b1 != b2) { ++dropped; analyzer.resetHistory(); continue; }

            derivedPower   = analyzer.watts();
            derivedCurrent = analyzer.currentRms();
            derivedClip    = analyzer.currentClipping();
            if (analyzer.valid()) {
                derivedRpm  = analyzer.rpm();
                haveDerived = true;
            } else {
                /* Not turning, or not turning steadily enough to measure.
                 * Report zero rather than the last good value, which would
                 * leave the dial reading a speed the shaft no longer has. */
                derivedRpm  = 0.f;
                haveDerived = true;
            }
        }

        if (local.seq != last_seq) {
            last_seq = local.seq;
            local.derivedValid    = haveDerived;
            local.rpmMeasured     = derivedRpm;
            local.powerW          = derivedPower;
            local.currentRmsA     = derivedCurrent;
            local.currentClipping = derivedClip;
            local.blocksDropped   = dropped;
            dropped = 0;
            emit newData(local);
        }

        usleep(10000);  /* 10 ms poll; well above the 10 ms block cadence */
    }

    munmap(m_shm, m_shm_size);
    m_shm = nullptr;
}

void SpiReader::stop()
{
    m_running = false;
}