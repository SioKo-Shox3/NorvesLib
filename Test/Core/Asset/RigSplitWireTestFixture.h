#pragma once
#include "ClipBankV1Fixture.h"
#include "Asset/RigSplitWire.h"
#include "Asset/RigSplitAllocationTestAccess.h"
namespace NorvesLib::Tests::RigSplitWireFixture
{
    struct AllocationCounts
    {
        uint32_t Owned = 0, Topology = 0, Slot = 0, Base = 0, Pixels = 0, ImageCopies = 0, Arm = 0;
        static void Observe(const char* stage, void* opaque)
        {
            auto& c = *static_cast<AllocationCounts*>(opaque);
            if (std::strcmp(stage, "skeleton_owned") == 0 || std::strcmp(stage, "mesh_owned") == 0)
            {
                ++c.Owned;
            }
            if (std::strcmp(stage, "topology_owned") == 0)
            {
                ++c.Topology;
            }
            if (std::strcmp(stage, "material_slot_copy") == 0)
            {
                ++c.Slot;
            }
            if (std::strcmp(stage, "material_base_copy") == 0)
            {
                ++c.Base;
            }
            if (std::strcmp(stage, "image_pixels") == 0)
            {
                ++c.Pixels;
            }
            if (std::strcmp(stage, "image_decoded_copy") == 0 || std::strcmp(stage, "image_cache_copy") == 0 ||
                std::strcmp(stage, "image_return_copy") == 0)
            {
                ++c.ImageCopies;
            }
            if (std::strcmp(stage, "arm_scratch_copy") == 0)
            {
                ++c.Arm;
            }
        }
    };
    struct ObserveAllocations
    {
        explicit ObserveAllocations(AllocationCounts& counts)
        {
            Core::Skeletal::Detail::SetSplitAllocationProbe(AllocationCounts::Observe, &counts);
        }
        ~ObserveAllocations()
        {
            Core::Skeletal::Detail::SetSplitAllocationProbe(nullptr, nullptr);
        }
    };
    inline RigV1Fixture::Bytes Strings(const RigV1Fixture::Bytes& source, const RigV1Fixture::Bytes& strings)
    {
        namespace F = RigV1Fixture;
        namespace W = Core::Skeletal::SplitWire;
        Core::Container::VariableArray<W::OutputSection> sections(F::U32(source, 28));
        for (size_t i = 0; i < sections.size(); ++i)
        {
            const size_t o = 256 + i * 32;
            sections[i].Code = F::U32(source, o);
            sections[i].Record = F::U32(source, o + 24);
            const auto offset = F::U64(source, o + 8), size = F::U64(source, o + 16);
            sections[i].Data.insert(sections[i].Data.end(), source.begin() + size_t(offset),
                                    source.begin() + size_t(offset + size));
        }
        sections[0].Data = strings;
        F::Bytes result;
        RIG_CHECK(W::WriteEnvelope(F::U32(source, 20), F::U64(source, 56), sections, result, {}) ==
                  Core::Skeletal::RigV1Status::Success);
        return result;
    }
    inline RigV1Fixture::Bytes Optional(const RigV1Fixture::Bytes& input)
    {
        namespace F = RigV1Fixture;
        auto bytes = input;
        const uint32_t count = F::U32(bytes, 28);
        const size_t insert = 256 + count * 32;
        bytes.insert(bytes.begin() + insert, 32, 0);
        for (uint32_t i = 0; i < count; ++i)
        {
            F::Put64(bytes, 256 + i * 32 + 8, F::U64(bytes, 256 + i * 32 + 8) + 32);
        }
        F::Put32(bytes, insert, 0x54534554);
        F::Put64(bytes, insert + 8, bytes.size());
        F::Put64(bytes, insert + 16, 16);
        F::Put32(bytes, insert + 24, 1);
        F::Put32(bytes, insert + 28, 16);
        bytes.resize(bytes.size() + 16, 0);
        F::Put32(bytes, 28, count + 1);
        F::Put64(bytes, 40, bytes.size());
        F::Reseal(bytes);
        return bytes;
    }
} // namespace NorvesLib::Tests::RigSplitWireFixture
