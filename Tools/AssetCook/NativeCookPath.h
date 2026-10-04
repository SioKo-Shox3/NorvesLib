#pragma once
#include "Asset/CookedSkeletalNameCodec.h"
#include "Container/String.h"
#include "Container/VariableArray.h"
#include "Resource/GltfNativePath.h"
#include <filesystem>
#include <utility>

namespace NorvesLib::Tools::AssetCook::Detail
{
    // I/Oのlocatorはnative pathのまま保持する。文字列化は診断表示のためだけに行う。
    // false時はoutを保持する。generic指定時のWindows slashは従来generic_stringと揃える。
    [[nodiscard]] inline bool EncodeCookPathUtf8(const std::filesystem::path& path, Core::Container::AnsiString& out,
                                                 bool bGenericSeparators = true)
    {
        const auto& native = path.native();
        const Core::Container::Span<const std::filesystem::path::value_type> units{native.data(), native.size()};
        const auto size = Core::Asset::MeasureSkeletalNameEncoding(2, units);
        if (!size.Succeeded())
        {
            return false;
        }
        Core::Container::VariableArray<uint8_t> bytes(size.ByteCount);
        if (!Core::Asset::EncodeSkeletalWireName(2, units, {bytes.data(), bytes.size()}).Succeeded())
        {
            return false;
        }
#if defined(_WIN32)
        if (bGenericSeparators)
        {
            for (auto& byte : bytes)
            {
                if (byte == '\\')
                {
                    byte = '/';
                }
            }
        }
#else
        (void)bGenericSeparators;
#endif
        Core::Container::AnsiString candidate;
        if (!bytes.empty())
        {
            candidate.append(reinterpret_cast<const char*>(bytes.data()), bytes.size());
        }
        out = std::move(candidate);
        return true;
    }
} // namespace NorvesLib::Tools::AssetCook::Detail
