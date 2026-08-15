#include "AiReader.h"

/* --- Shared-memory contract -------------------------------------------
 *
 * Mirrors ai_result_region_t / ai_result_snapshot_t from motor_ai_client's
 * client/src/ai_result_shm.h, for the same reason SpiReader mirrors
 * motor_shm.h rather than including it: that header uses C11 _Atomic and
 * <stdatomic.h>, whose C++ shim only activates for C++23 on this toolchain.
 *
 * std::atomic<uint32_t> stands in for the producer's _Atomic uint32_t --
 * same size and alignment for a lock-free 4-byte type, so the layout matches.
 *
 * These names and sizes MUST stay in sync with ai_result_shm.h.
 * -------------------------------------------------------------------- */

#include <fcntl.h>
#include <sys/mman.h>
#include <unistd.h>
#include <atomic>
#include <cstddef>
#include <cstring>
#include <QDebug>

namespace {

constexpr const char *kAiShmName    = "/motor_ai_result"; // AI_RESULT_SHM_NAME
constexpr uint32_t    kAiShmMagic   = 0x41495200u;        // AI_RESULT_SHM_MAGIC ("AIR\0")
constexpr uint16_t    kAiVersion    = 1u;                 // AI_RESULT_VERSION
constexpr size_t      kAiStrLen     = 256u;               // AI_RESULT_STR_LEN
constexpr size_t      kAiCacheline  = 64u;                // AI_CACHELINE

struct AiSnapshotShm {
    alignas(kAiCacheline) std::atomic<uint32_t> seqlock;
    uint64_t timestamp;
    uint32_t producer_seq;
    uint16_t flags;
    uint16_t _pad;
    char     anomaly_result[kAiStrLen];
    char     fault_class_result[kAiStrLen];
    char     pred_maint_result[kAiStrLen];
};

struct AiRegion {
    uint32_t magic;
    uint16_t version;
    uint16_t _pad0;
    alignas(kAiCacheline) AiSnapshotShm snapshot;
};

/* Checked against ai_result_shm.h compiled as C11 -- every offset below was
 * read off the real struct, not derived from this one. A mismatch here means
 * the two have drifted, and the cost of not noticing is reading a verdict out
 * of the middle of another field, so it fails the build rather than the run. */
static_assert(sizeof(AiRegion) == 896, "AiRegion size drifted from ai_result_region_t");
static_assert(offsetof(AiRegion, magic) == 0, "magic moved");
static_assert(offsetof(AiRegion, version) == 4, "version moved");
static_assert(offsetof(AiRegion, snapshot) == 64, "snapshot moved");
static_assert(offsetof(AiRegion, snapshot.timestamp) == 72, "timestamp moved");
static_assert(offsetof(AiRegion, snapshot.producer_seq) == 80, "producer_seq moved");
static_assert(offsetof(AiRegion, snapshot.anomaly_result) == 88, "anomaly_result moved");
static_assert(offsetof(AiRegion, snapshot.fault_class_result) == 344, "fault_class_result moved");
static_assert(offsetof(AiRegion, snapshot.pred_maint_result) == 600, "pred_maint_result moved");

/* Raw copy taken between the two seqlock reads. Kept as plain char buffers --
 * building QStrings inside the retry loop would allocate while the producer
 * may be overwriting the source, and the copy could be torn anyway.       */
struct RawResults {
    char anomaly[kAiStrLen];
    char faultClass[kAiStrLen];
    char predMaint[kAiStrLen];
    uint32_t seq;
    uint64_t timestamp;
};

void copy_snapshot(const AiSnapshotShm *src, RawResults *dst)
{
    std::memcpy(dst->anomaly, src->anomaly_result, kAiStrLen);
    std::memcpy(dst->faultClass, src->fault_class_result, kAiStrLen);
    std::memcpy(dst->predMaint, src->pred_maint_result, kAiStrLen);
    dst->seq = src->producer_seq;
    dst->timestamp = src->timestamp;
}

/* The producer null-terminates, but a torn read might not, so bound it. */
QString to_string(const char *buf)
{
    return QString::fromUtf8(buf, static_cast<int>(strnlen(buf, kAiStrLen)));
}

} // namespace

AiReader::AiReader(QObject *parent) : QThread(parent)
{
    qRegisterMetaType<AiResults>("AiResults");
}

void AiReader::run()
{
    /* Retry for the same 5s SpiReader allows. motor_ai_client starts after the
     * producer and only creates this region once it has a verdict to write, so
     * a miss here is common and not an error -- the cluster runs without it. */
    int fd = -1;
    for (int attempt = 0; attempt < 20 && m_running; ++attempt) {
        fd = shm_open(kAiShmName, O_RDONLY, 0);
        if (fd != -1) break;
        usleep(250000);
    }
    if (fd == -1) {
        qWarning("AiReader: shm_open(%s) failed -- is motor_ai_client running? "
                 "Gauges are unaffected; the AI verdicts stay blank.", kAiShmName);
        return;
    }

    m_shm_size = sizeof(AiRegion);
    m_shm = mmap(nullptr, m_shm_size, PROT_READ, MAP_SHARED, fd, 0);
    close(fd);
    if (m_shm == MAP_FAILED) {
        qWarning("AiReader: mmap failed");
        m_shm = nullptr;
        return;
    }

    const AiRegion *region = static_cast<const AiRegion *>(m_shm);

    if (region->magic != kAiShmMagic || region->version != kAiVersion) {
        qWarning("AiReader: shm contract mismatch (magic=0x%08x ver=%u; "
                 "expected 0x%08x ver=%u) -- not reading",
                 region->magic, region->version, kAiShmMagic, kAiVersion);
        munmap(m_shm, m_shm_size);
        m_shm = nullptr;
        return;
    }

    const AiSnapshotShm *snap = &region->snapshot;
    uint32_t last_seq = UINT32_MAX;

    while (m_running) {
        RawResults local;
        std::memset(&local, 0, sizeof(local));

        int tries = 0;
        for (;; ++tries) {
            uint32_t s1 = snap->seqlock.load(std::memory_order_acquire);
            if (s1 & 1u) { if (tries > 100) break; continue; }
            copy_snapshot(snap, &local);
            uint32_t s2 = snap->seqlock.load(std::memory_order_acquire);
            if (s1 == s2) break;
            if (tries > 100) break;
        }

        if (local.seq != last_seq) {
            last_seq = local.seq;
            AiResults r;
            r.anomaly    = to_string(local.anomaly);
            r.faultClass = to_string(local.faultClass);
            r.predMaint  = to_string(local.predMaint);
            r.seq        = local.seq;
            r.timestamp  = local.timestamp;
            emit newResults(r);
        }

        /* Far slower than SpiReader's 10ms. A verdict covers a whole window of
         * rows and lands at the AI server's pace, not the sensor's; polling it
         * at the sensor rate would be a hundred wasted wakeups per new value. */
        usleep(200000);
    }

    munmap(m_shm, m_shm_size);
    m_shm = nullptr;
}

void AiReader::stop()
{
    m_running = false;
}
