#include "Asset/CookedMaterialFormat.h"
#include <bit>
#include <cmath>
#include <cstring>
#include <limits>

namespace NorvesLib::Core::Asset
{
    namespace
    {
        static_assert(sizeof(float) == 4 && std::numeric_limits<float>::is_iec559);
        bool ValidStorage(const void* data, size_t size)
        {
            return data && size <= UINTPTR_MAX - reinterpret_cast<uintptr_t>(data);
        }
        bool Overlap(const void* left, size_t leftSize, const void* right, size_t rightSize)
        {
            const auto a = reinterpret_cast<uintptr_t>(left), b = reinterpret_cast<uintptr_t>(right);
            return a < b + rightSize && b < a + leftSize;
        }
        uint32_t U32(const uint8_t* data)
        {
            return uint32_t(data[0]) | (uint32_t(data[1])<<8) | (uint32_t(data[2])<<16) | (uint32_t(data[3])<<24);
        }
        uint64_t U64(const uint8_t* data)
        {
            return U32(data) | (uint64_t(U32(data+4))<<32);
        }
        void Put32(uint8_t* out, uint32_t value)
        {
            for (size_t index = 0; index < 4; ++index)
            {
                out[index] = static_cast<uint8_t>(value>>(index*8));
            }
        }
        void Put64(uint8_t* out, uint64_t value)
        {
            Put32(out,static_cast<uint32_t>(value));
            Put32(out+4,static_cast<uint32_t>(value>>32));
        }
        bool Unit(float value)
        {
            return std::isfinite(value) && value >= 0 && value <= 1;
        }
    }
    CookedMaterialStatus ValidateCookedMaterialRecord(const CookedMaterialRecord& record, uint64_t stringTableSize) noexcept
    {
        using namespace CookedMaterialFormatV1;
        const CookedMaterialStringRef references[] = {record.Albedo,record.Normal,record.Arm,record.Emissive};
        for (const auto& reference : references)
        {
            if (reference.Offset > stringTableSize || reference.Length > stringTableSize - reference.Offset)
            {
                return CookedMaterialStatus::InvalidReference;
            }
        }
        if ((record.Flags & ~KnownFlags) != 0 || ((record.Flags & AlphaModeMask)>>AlphaModeShift) > 2 ||
            ((record.Flags & ArmUseMask) != 0 && record.Arm.Length == 0))
        {
            return CookedMaterialStatus::InvalidFlags;
        }
        if (record.ShadingModelId != DefaultLit)
        {
            return CookedMaterialStatus::UnsupportedShadingModel;
        }
        for (float component : record.BaseColor)
        {
            if (!Unit(component))
            {
                return CookedMaterialStatus::InvalidNumeric;
            }
        }
        // 負のmetal/roughは未指定。alphaCutoffはglTF同様に非負で、1超過も保持する。
        if (!std::isfinite(record.Metallic) || record.Metallic > 1 || !std::isfinite(record.Roughness) || record.Roughness > 1 ||
            !Unit(record.OcclusionStrength) || !std::isfinite(record.NormalScale) ||
            !std::isfinite(record.AlphaCutoff) || record.AlphaCutoff < 0)
        {
            return CookedMaterialStatus::InvalidNumeric;
        }
        if (!std::isfinite(record.EmissiveNits) || record.EmissiveNits < 0)
        {
            return CookedMaterialStatus::InvalidEmissive;
        }
        double y = 0;
        const double luminance[] = {0.2126,0.7152,0.0722};
        for (size_t index = 0; index < 3; ++index)
        {
            const float color = record.EmissiveColor[index];
            if (!std::isfinite(color) || color < 0 || (record.EmissiveNits == 0 && color != 0) ||
                double(color)*record.EmissiveNits >= 65504.0)
            {
                return CookedMaterialStatus::InvalidEmissive;
            }
            y += luminance[index]*color;
        }
        if (record.EmissiveNits > 0 && std::fabs(y-1.0) > 0.0001)
        {
            return CookedMaterialStatus::InvalidEmissive;
        }
        if (record.EmissiveNits > 0)
        {
            // runtime側のY再正規化後にもhalfの保存範囲を超えないことを保証する。
            for (float color : record.EmissiveColor)
            {
                if ((double(color)/y)*record.EmissiveNits >= 65504.0)
                {
                    return CookedMaterialStatus::InvalidEmissive;
                }
            }
        }
        return CookedMaterialStatus::Success;
    }

    CookedMaterialStatus ReadCookedMaterialRecord(Container::Span<const uint8_t> bytes,
        uint64_t stringTableSize, CookedMaterialRecord& out) noexcept
    {
        using namespace CookedMaterialFormatV1;
        if (bytes.size() != RecordSize || !ValidStorage(bytes.data(),bytes.size()) ||
            !ValidStorage(&out,sizeof(out)) || Overlap(bytes.data(),bytes.size(),&out,sizeof(out)))
        {
            return CookedMaterialStatus::InvalidInput;
        }
        CookedMaterialRecord candidate;
        CookedMaterialStringRef* references[] = {&candidate.Albedo,&candidate.Normal,&candidate.Arm,&candidate.Emissive};
        for (size_t index = 0; index < 4; ++index)
        {
            const auto* source = bytes.data()+index*StringRefSize;
            if (U32(source+12) != 0)
            {
                return CookedMaterialStatus::InvalidReserved;
            }
            *references[index] = {U64(source),U32(source+8)};
        }
        if (U32(bytes.data()+Offset::Reserved) != 0)
        {
            return CookedMaterialStatus::InvalidReserved;
        }
        for (size_t index = 0; index < 4; ++index)
        {
            candidate.BaseColor[index] = std::bit_cast<float>(U32(bytes.data()+Offset::BaseColor+index*4));
        }
        for (size_t index = 0; index < 3; ++index)
        {
            candidate.EmissiveColor[index] = std::bit_cast<float>(U32(bytes.data()+Offset::EmissiveColor+index*4));
        }
        float* scalars[] = {&candidate.EmissiveNits,&candidate.Metallic,&candidate.Roughness,
            &candidate.OcclusionStrength,&candidate.NormalScale,&candidate.AlphaCutoff};
        for (size_t index = 0; index < 6; ++index)
        {
            *scalars[index] = std::bit_cast<float>(U32(bytes.data()+Offset::EmissiveNits+index*4));
        }
        candidate.Flags = U32(bytes.data()+Offset::Flags);
        candidate.ShadingModelId = U32(bytes.data()+Offset::ShadingModelId);
        const auto status = ValidateCookedMaterialRecord(candidate,stringTableSize);
        if (status == CookedMaterialStatus::Success)
        {
            out = candidate;
        }
        return status;
    }

    CookedMaterialStatus WriteCookedMaterialRecord(const CookedMaterialRecord& record,
        uint64_t stringTableSize, Container::Span<uint8_t> out) noexcept
    {
        using namespace CookedMaterialFormatV1;
        if (out.size() < RecordSize || !ValidStorage(out.data(),out.size()) || !ValidStorage(&record,sizeof(record)) ||
            Overlap(&record,sizeof(record),out.data(),out.size()))
        {
            return CookedMaterialStatus::InvalidInput;
        }
        const auto status = ValidateCookedMaterialRecord(record,stringTableSize);
        if (status != CookedMaterialStatus::Success)
        {
            return status;
        }
        uint8_t candidate[RecordSize]{};
        const CookedMaterialStringRef references[] = {record.Albedo,record.Normal,record.Arm,record.Emissive};
        for (size_t index = 0; index < 4; ++index)
        {
            Put64(candidate+index*StringRefSize,references[index].Offset);
            Put32(candidate+index*StringRefSize+8,references[index].Length);
        }
        for (size_t index = 0; index < 4; ++index)
        {
            Put32(candidate+Offset::BaseColor+index*4,std::bit_cast<uint32_t>(record.BaseColor[index]));
        }
        for (size_t index = 0; index < 3; ++index)
        {
            Put32(candidate+Offset::EmissiveColor+index*4,std::bit_cast<uint32_t>(record.EmissiveColor[index]));
        }
        const float scalars[] = {record.EmissiveNits,record.Metallic,record.Roughness,record.OcclusionStrength,record.NormalScale,record.AlphaCutoff};
        for (size_t index = 0; index < 6; ++index)
        {
            Put32(candidate+Offset::EmissiveNits+index*4,std::bit_cast<uint32_t>(scalars[index]));
        }
        Put32(candidate+Offset::Flags,record.Flags);
        Put32(candidate+Offset::ShadingModelId,record.ShadingModelId);
        std::memcpy(out.data(),candidate,RecordSize);
        return CookedMaterialStatus::Success;
    }
} // namespace NorvesLib::Core::Asset
