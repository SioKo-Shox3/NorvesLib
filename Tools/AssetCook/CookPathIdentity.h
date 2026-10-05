#pragma once
#include "Container/String.h"
#include "Container/FixedArray.h"
#include "Container/VariableArray.h"
#include <filesystem>
namespace NorvesLib::Tools::AssetCook::Detail
{
    struct CookPathComponent
    {
        uint32_t Offset = 0, Length = 0;
    };
    struct CookPathIdentity
    {
        // pathは比較専用。元locatorの代わりにI/Oへ戻さない。
        std::filesystem::path Canonical;
        Core::Container::VariableArray<CookPathComponent> Components;
        uint64_t VolumeSerial = 0;
        Core::Container::FixedArray<uint8_t, 16> FileId;
        uint32_t LinkCount = 0;
        bool bPresent = false;
    };
    inline constexpr size_t MaximumCookLocatorUnits = 32767;
    inline constexpr size_t MaximumCookLocatorComponents = 256;
    // local drive絶対pathの語彙検査のみ。Unicodeをnarrowへ変換せず、unsupported表記は拒否する。
    [[nodiscard]] bool NormalizeCookGuardLocator(const std::filesystem::path& input, std::filesystem::path& out,
                                                 Core::Container::AnsiString& error);
    // 既存regular fileまたは不在file endpointを観測。reparse/dir leaf/不明identityは拒否する。
    [[nodiscard]] bool ObserveCookPathIdentity(const std::filesystem::path& locator, CookPathIdentity& out,
                                               Core::Container::AnsiString& error);
    // directory endpoint用。既存directoryまたは既存直親の下の不在leafだけを観測する。
    [[nodiscard]] bool ObserveCookDirectoryIdentity(const std::filesystem::path& locator, CookPathIdentity& out,
                                                    Core::Container::AnsiString& error);
    [[nodiscard]] int CompareCookPhysicalPath(const CookPathIdentity& a, const CookPathIdentity& b);
    [[nodiscard]] bool CookPhysicalAncestor(const CookPathIdentity& ancestor, const CookPathIdentity& child);
    // 共通manifestの同一性。case-fold path一致だけでは別file/不在endpointを統合しない。
    [[nodiscard]] bool SameCookManifestEndpoint(const CookPathIdentity& a, const CookPathIdentity& b);
    [[nodiscard]] int CompareCookFileIdentity(const CookPathIdentity& a, const CookPathIdentity& b);
} // namespace NorvesLib::Tools::AssetCook::Detail
