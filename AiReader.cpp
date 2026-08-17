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

/* ---- injected-fault override -----------------------------------------
 * Mirrors fault_override_region_t from fault_tester/src/fault_override_shm.h.
 *
 * Why the cluster reads this at all: fault_tester injects into
 * /motor_fault_override, and motor_diag_service checks that region BEFORE
 * /motor_ai_result on its way to the head unit over SOME/IP. The cluster read
 * only the AI region, so an injected fault appeared on the IVI and not here,
 * and the two displays could not be exercised from one injection. Applying the
 * same precedence in the same order is what makes them agree.
 *
 * Note this is a THIRD copy of a contract that already exists twice (here and
 * in motor_diag_service). The magic and version below are what catch a drift;
 * a shared header, or a submodule, would be better than all three.
 */
constexpr const char *kOvrShmName = "/motor_fault_override"; // FAULT_OVERRIDE_SHM_NAME
constexpr uint32_t    kOvrMagic   = 0x464F5652u;             // "FOVR"
constexpr uint16_t    kOvrVersion = 1u;

struct OverrideRegion {
    uint32_t magic;
    uint16_t version;
    uint16_t _pad0;
    alignas(kAiCacheline) std::atomic<uint32_t> seqlock;
    uint32_t active;
    char anomaly[kAiStrLen];
    char fault_class[kAiStrLen];
    char pred_maint[kAiStrLen];
};

static_assert(sizeof(OverrideRegion) == 896, "OverrideRegion drifted from fault_override_region_t");
static_assert(offsetof(OverrideRegion, version) == 4, "override version moved");
static_assert(offsetof(OverrideRegion, seqlock) == 64, "override seqlock moved");
static_assert(offsetof(OverrideRegion, active) == 68, "override active moved");
static_assert(offsetof(OverrideRegion, anomaly) == 72, "override anomaly moved");
static_assert(offsetof(OverrideRegion, fault_class) == 328, "override fault_class moved");
static_assert(offsetof(OverrideRegion, pred_maint) == 584, "override pred_maint moved");

/* Both regions use the same seqlock protocol, so it is written once here
 * rather than open-coded per region. Returns false if it never settled, and
 * the caller then keeps whatever it had rather than acting on a torn read.
 *
 * A reader only has to reject odd and retry. The self-healing part -- forcing
 * the counter even before a write, so a writer killed mid-update does not
 * leave the region permanently odd -- is the WRITER's job, and fault_tester
 * already does it. */
template <typename CopyFn>
bool seqlockRead(const std::atomic<uint32_t> &seq, CopyFn &&copy)
{
    for (int tries = 0; tries < 100; ++tries) {
        const uint32_t s1 = seq.load(std::memory_order_acquire);
        if (s1 & 1u) continue;                  /* write in progress */
        copy();
        const uint32_t s2 = seq.load(std::memory_order_acquire);
        if (s1 == s2) return true;
    }
    return false;
}

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

/* Open a read-only shm region and validate its header. Returns the mapping, or
 * nullptr if it is absent or the contract does not match. Absence is not an
 * error here: neither region is required, and both may appear later. */
namespace {
void *mapRegion(const char *name, size_t size,
                uint32_t wantMagic, uint16_t wantVersion, const char *who)
{
    const int fd = shm_open(name, O_RDONLY, 0);
    if (fd == -1) return nullptr;

    void *p = mmap(nullptr, size, PROT_READ, MAP_SHARED, fd, 0);
    close(fd);
    if (p == MAP_FAILED) return nullptr;

    /* Both regions begin magic/version, so one check serves both. */
    const uint32_t magic   = *static_cast<const uint32_t *>(p);
    const uint16_t version = *reinterpret_cast<const uint16_t *>(
                                 static_cast<const char *>(p) + 4);
    if (magic != wantMagic || version != wantVersion) {
        qWarning("%s: contract mismatch on %s (magic=0x%08x ver=%u; expected 0x%08x ver=%u)"
                 " -- not reading", who, name, magic, version, wantMagic, wantVersion);
        munmap(p, size);
        return nullptr;
    }
    return p;
}
} // namespace

void AiReader::run()
{
    /* NEITHER region is required and the thread no longer gives up when one is
     * missing. It used to return outright if /motor_ai_result was absent, which
     * meant a fault injected with no AI running was never seen -- and that is
     * exactly the case a fault test wants. Both are retried on a slow cadence
     * so fault_tester can be started long after the cluster, which is how it is
     * actually used. */
    const AiRegion       *ai  = nullptr;
    const OverrideRegion *ovr = nullptr;
    int rescanTicks = 0;
    bool warnedNoAi = false;

    /* What was last published, so a change can be detected without a sequence
     * number -- the override region has none. */
    AiResults last;
    bool haveLast = false;
    bool lastWasOverride = false;

    while (m_running) {
        /* --- (re)attach ------------------------------------------------
         * Every ~2s, not every poll: shm_open on a missing name is cheap but
         * not free, and nothing here is urgent. */
        if ((!ai || !ovr) && rescanTicks-- <= 0) {
            rescanTicks = 10;               /* 10 * 200ms */
            if (!ai) {
                ai = static_cast<const AiRegion *>(
                    mapRegion(kAiShmName, sizeof(AiRegion), kAiShmMagic, kAiVersion, "AiReader"));
                if (ai) qInfo("AiReader: attached to %s", kAiShmName);
                else if (!warnedNoAi) {
                    warnedNoAi = true;
                    qWarning("AiReader: %s not present yet -- is motor_ai_client running? "
                             "Gauges are unaffected; verdicts stay blank until it appears.",
                             kAiShmName);
                }
            }
            if (!ovr) {
                ovr = static_cast<const OverrideRegion *>(
                    mapRegion(kOvrShmName, sizeof(OverrideRegion), kOvrMagic, kOvrVersion, "AiReader"));
                if (ovr) qInfo("AiReader: attached to %s (fault injection available)", kOvrShmName);
            }
        }

        /* --- override first, exactly as motor_diag_service orders it ---- */
        AiResults next;
        bool fromOverride = false;

        if (ovr) {
            uint32_t active = 0;
            char a[kAiStrLen], f[kAiStrLen], pm[kAiStrLen];
            const bool clean = seqlockRead(ovr->seqlock, [&] {
                active = ovr->active;
                std::memcpy(a,  ovr->anomaly,     kAiStrLen);
                std::memcpy(f,  ovr->fault_class, kAiStrLen);
                std::memcpy(pm, ovr->pred_maint,  kAiStrLen);
            });
            if (clean && active) {
                next.anomaly    = to_string(a);
                next.faultClass = to_string(f);
                next.predMaint  = to_string(pm);
                /* No sequence of its own; carry the last AI one so anything
                 * downstream keyed on seq does not see it jump backwards. */
                next.seq        = haveLast ? last.seq : 0;
                next.timestamp  = 0;
                fromOverride    = true;
            }
        }

        if (!fromOverride && ai) {
            RawResults raw;
            std::memset(&raw, 0, sizeof(raw));
            if (seqlockRead(ai->snapshot.seqlock, [&] { copy_snapshot(&ai->snapshot, &raw); })) {
                next.anomaly    = to_string(raw.anomaly);
                next.faultClass = to_string(raw.faultClass);
                next.predMaint  = to_string(raw.predMaint);
                next.seq        = raw.seq;
                next.timestamp  = raw.timestamp;
            } else if (haveLast) {
                next = last;                /* never settled: keep what we had */
            }
        }

        /* Nothing attached yet: publish nothing. Without this the first poll
         * of a cluster started before either producer emitted an empty verdict
         * and logged "source is now the AI" when there was no AI to be the
         * source of it. */
        if (!fromOverride && !ai) {
            usleep(200000);
            continue;
        }

        /* --- publish on change -----------------------------------------
         * Compared by content as well as sequence: the override has no
         * sequence, and releasing it must fall back to the AI verdict even
         * when that verdict has not moved. */
        const bool changed = !haveLast
                          || fromOverride != lastWasOverride
                          || next.seq        != last.seq
                          || next.anomaly    != last.anomaly
                          || next.faultClass != last.faultClass
                          || next.predMaint  != last.predMaint;
        if (changed) {
            if (fromOverride != lastWasOverride || !haveLast)
                qInfo("AiReader: verdict source is now %s",
                      fromOverride ? "INJECTED (/motor_fault_override)" : "the AI");
            last = next;
            haveLast = true;
            lastWasOverride = fromOverride;
            emit newResults(next);
        }

        /* A verdict covers a whole window of rows and lands at the AI server's
         * pace, not the sensor's. 200ms is also a comfortable response time for
         * a hand-driven fault injection. */
        usleep(200000);
    }

    if (ai)  munmap(const_cast<AiRegion *>(ai), sizeof(AiRegion));
    if (ovr) munmap(const_cast<OverrideRegion *>(ovr), sizeof(OverrideRegion));
    m_shm = nullptr;
}

void AiReader::stop()
{
    m_running = false;
}
