#include "heap_profiler.h"

#include "symbolize.h"
#include "backtrace.h"

#include <util/generic/hash_set.h>
#include <util/string/join.h>

#include <tcmalloc/malloc_extension.h>

#include <library/cpp/yt/memory/leaky_singleton.h>

#include <library/cpp/yt/threading/spin_lock.h>

namespace NYT {

////////////////////////////////////////////////////////////////////////////////

Y_WEAK void* CreateAllocationTagsData()
{
    return nullptr;
}

Y_WEAK void* CopyAllocationTagsData(void* ptr)
{
    return ptr;
}

Y_WEAK void DestroyAllocationTagsData(void* /*ptr*/)
{ }

Y_WEAK const std::vector<std::pair<TString, TString>>& ReadAllocationTagsData(void* /*ptr*/)
{
    static std::vector<std::pair<TString, TString>> emptyTags;
    return emptyTags;
}

Y_WEAK void StartAllocationTagsCleanupThread(const TDuration& /*cleanupInterval*/)
{ }

////////////////////////////////////////////////////////////////////////////////

} // namespace NYT

namespace NYT::NYTProf {

using namespace NThreading;

////////////////////////////////////////////////////////////////////////////////

NProto::Profile ConvertAllocationProfile(const tcmalloc::Profile& snapshot)
{
    NProto::Profile profile;
    profile.add_string_table();

    auto addString = [&] (TString str) {
        auto index = profile.string_table_size();
        profile.add_string_table(str);
        return index;
    };

    auto sampleType = profile.add_sample_type();
    sampleType->set_type(addString("allocations"));
    sampleType->set_unit(addString("count"));

    sampleType = profile.add_sample_type();
    sampleType->set_type(addString("space"));

    auto bytesUnitId = addString("bytes");
    sampleType->set_unit(bytesUnitId);

    auto periodType = profile.mutable_period_type();
    periodType->set_type(sampleType->type());
    periodType->set_unit(sampleType->unit());

    profile.set_period(snapshot.Period());

    auto memoryTagId = addString("memory_tag");
    auto allocatedSizeId = addString("allocated_size");
    auto requestedSizeId = addString("requested_size");
    auto requestedAlignmentId = addString("requested_alignment");

    THashMap<void*, ui64> locations;
    snapshot.Iterate([&] (const tcmalloc::Profile::Sample& sample) {
        auto sampleProto = profile.add_sample();
        sampleProto->add_value(sample.count);
        sampleProto->add_value(sample.sum);

        auto memoryTag = sample.stack[0];
        if (memoryTag != 0) {
            auto label = sampleProto->add_label();
            label->set_key(memoryTagId);
            label->set_num(reinterpret_cast<i64>(memoryTag));
        }

        auto allocatedSizeLabel = sampleProto->add_label();
        allocatedSizeLabel->set_key(allocatedSizeId);
        allocatedSizeLabel->set_num(sample.allocated_size);
        allocatedSizeLabel->set_num_unit(bytesUnitId);

        auto requestedSizeLabel = sampleProto->add_label();
        requestedSizeLabel->set_key(requestedSizeId);
        requestedSizeLabel->set_num(sample.requested_size);
        requestedSizeLabel->set_num_unit(bytesUnitId);

        auto requestedAlignmentLabel = sampleProto->add_label();
        requestedAlignmentLabel->set_key(requestedAlignmentId);
        requestedAlignmentLabel->set_num(sample.requested_alignment);
        requestedAlignmentLabel->set_num_unit(bytesUnitId);

        for (int i = 1; i < sample.depth; i++) {
            auto ip = sample.stack[i];

            auto it = locations.find(ip);
            if (it != locations.end()) {
                sampleProto->add_location_id(it->second);
                continue;
            }

            auto locationId = locations.size() + 1;

            auto location = profile.add_location();
            location->set_address(reinterpret_cast<ui64>(ip));
            location->set_id(locationId);

            sampleProto->add_location_id(locationId);
            locations[ip] = locationId;
        }

        // TODO(gepardo): Deduplicate values in string table
        for (const auto& [key, value] : ReadAllocationTagsData(sample.user_data)) {
            auto label = sampleProto->add_label();
            label->set_key(addString(key));
            label->set_str(addString(value));
        }
    });

    profile.set_drop_frames(addString(JoinSeq("|", {
        ".*SampleifyAllocation",
        ".*AllocSmall",
        "slow_alloc",
        "TBasicString::TBasicString",
    })));

    Symbolize(&profile, true);
    return profile;
}

NProto::Profile ReadHeapProfile(tcmalloc::ProfileType profileType)
{
    auto snapshot = tcmalloc::MallocExtension::SnapshotCurrent(profileType);
    return ConvertAllocationProfile(snapshot);
}

THashMap<TMemoryTag, ui64> GetEstimatedMemoryUsage()
{
    THashMap<TMemoryTag, ui64> usage;

    auto snapshot = tcmalloc::MallocExtension::SnapshotCurrent(tcmalloc::ProfileType::kHeap);
    snapshot.Iterate([&] (const tcmalloc::Profile::Sample& sample) {
        auto memoryTag = sample.stack[0];
        if (memoryTag != 0) {
            usage[reinterpret_cast<TMemoryTag>(memoryTag)] += sample.sum;
        }
    });

    return usage;
}

static thread_local TMemoryTag MemoryTag = 0;

TMemoryTag GetMemoryTag()
{
    return MemoryTag;
}

TMemoryTag SetMemoryTag(TMemoryTag newTag)
{
    auto oldTag = MemoryTag;
    MemoryTag = newTag;
    return oldTag;
}

struct TMemoryUsageSnapshot
{
    TSpinLock Lock;
    THashMap<TMemoryTag, ui64> Snapshot;
};

void UpdateMemoryUsageSnapshot(THashMap<TMemoryTag, ui64> usageSnapshot)
{
    auto snapshot = LeakySingleton<TMemoryUsageSnapshot>();
    auto guard = Guard(snapshot->Lock);
    snapshot->Snapshot = std::move(usageSnapshot);
}

i64 GetEstimatedMemoryUsage(TMemoryTag tag)
{
    auto snapshot = LeakySingleton<TMemoryUsageSnapshot>();
    auto guard = Guard(snapshot->Lock);
    auto it = snapshot->Snapshot.find(tag);
    if (it != snapshot->Snapshot.end()) {
        return it->second;
    }
    return 0;
}

int AbslStackUnwinder(void** frames, int*,
                      int maxFrames, int skipFrames,
                      const void*,
                      int*)
{
    TUWCursor cursor;

    for (int i = 0; i < skipFrames + 1; ++i) {
        cursor.Next();
    }

    if (maxFrames > 0) {
        frames[0] = reinterpret_cast<void*>(MemoryTag);
    }

    int count = 1;
    for (int i = 1; i < maxFrames; ++i) {
        if (cursor.IsEnd()) {
            return count;
        }

        // IP point's to return address. Substract 1 to get accurate line information for profiler.
        frames[i] = reinterpret_cast<void*>(reinterpret_cast<uintptr_t>(cursor.GetIP()) - 1);
        count++;

        cursor.Next();
    }
    return count;
}

void EnableMemoryProfilingTags()
{
    StartAllocationTagsCleanupThread(TDuration::Seconds(1));
    tcmalloc::MallocExtension::SetSampleUserDataCallbacks(
        &CreateAllocationTagsData,
        &CopyAllocationTagsData,
        &DestroyAllocationTagsData);
}

////////////////////////////////////////////////////////////////////////////////

} // namespace NYT::NYTProf
