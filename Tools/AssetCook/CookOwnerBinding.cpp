// 値identityの安定した導出だけを行い、資産やstateを開かない。
#include "CookOwnerBinding.h"
#include "CookOutputPaths.h"
#include "Asset/AssetPath.h"
#include "Container/FixedArray.h"
#include "Container/VariableArray.h"
#include "Text/UnicodeText.h"
#include <exception>
#include <utility>
#if defined(_WIN32)
#include <Windows.h>
#include <bcrypt.h>
#endif
namespace NorvesLib::Tools::AssetCook
{
    namespace
    {
        using OwnerText = Core::Container::AnsiString;
        using OwnerView = Core::Container::AnsiStringView;
        using OwnerBytes = Core::Container::VariableArray<uint8_t>;
        bool Fail(OwnerText& error, const char* code)
        {
            error = "cook_owner_id: ";
            error.append(code);
            return false;
        }
        bool ValidIdentity(OwnerView value)
        {
            if (value.empty() || value.size() > MaximumCookOwnerIdentityBytes)
            {
                return false;
            }
            bool bNoNul = true;
            return Core::TextDetail::ForEachUnicodeScalar<char>({value.data(), value.size()},
                                                                [&](uint32_t scalar)
                                                                {
                                                                    bNoNul = bNoNul && scalar != 0;
                                                                }) &&
                   bNoNul;
        }
        bool ValidManifest(OwnerView value)
        {
            if (!ValidIdentity(value) || !Detail::CookOutputPaths::SafeOutputName(value))
            {
                return false;
            }
            const auto normalized = Core::Asset::AssetPath::Normalize(value);
            return normalized.HasLogicalPath() &&
                   Detail::CookOutputPaths::EqualName(normalized.GetLogicalPath(), value);
        }
        void U32(OwnerBytes& wire, uint32_t value)
        {
            for (unsigned i = 0; i < 4; ++i)
            {
                wire.push_back(static_cast<uint8_t>(value >> (8 * i)));
            }
        }
        void Field(OwnerBytes& wire, OwnerView value)
        {
            U32(wire, static_cast<uint32_t>(value.size()));
            for (const unsigned char unit : value)
            {
                wire.push_back(unit);
            }
        }
    } // namespace
    bool ComputeCookOwnerId(const CookOwnerIdentity& identity, OwnerText& outOwnerId, OwnerText& error)
    {
        // errorのclearによるinput/output破壊を防ぐ。値を読み終える前のaliasを認めない。
        if (&error == &outOwnerId || &error == &identity.CanonicalSpecLocator ||
            &error == &identity.CanonicalFinalRuntimeRootIdentity || &error == &identity.ManifestName)
        {
            return false;
        }
        error.clear();
#if !defined(_WIN32)
        (void)identity;
        (void)outOwnerId;
        return Fail(error, "windows_cng_required");
#else
        try
        {
            if (&outOwnerId == &identity.CanonicalSpecLocator ||
                &outOwnerId == &identity.CanonicalFinalRuntimeRootIdentity || &outOwnerId == &identity.ManifestName)
            {
                return Fail(error, "aliased_output");
            }
            if (!ValidIdentity(identity.CanonicalSpecLocator) ||
                !ValidIdentity(identity.CanonicalFinalRuntimeRootIdentity) || !ValidManifest(identity.ManifestName))
            {
                return Fail(error, "invalid_identity_or_manifest");
            }
            OwnerBytes wire;
            wire.reserve(4 + 4 * 4 + sizeof("NorvesLib.AssetCook") - 1 + 3 * MaximumCookOwnerIdentityBytes);
            U32(wire, 1);
            Field(wire, "NorvesLib.AssetCook");
            Field(wire, identity.CanonicalSpecLocator);
            Field(wire, identity.CanonicalFinalRuntimeRootIdentity);
            Field(wire, identity.ManifestName);
            Core::Container::FixedArray<uint8_t, 32> digest;
            const NTSTATUS status =
                BCryptHash(BCRYPT_SHA256_ALG_HANDLE, nullptr, 0, wire.data(), static_cast<ULONG>(wire.size()),
                           digest.data(), static_cast<ULONG>(digest.size()));
            if (status < 0)
            {
                return Fail(error, "sha256_failed");
            }
            bool bNonzero = false;
            OwnerText candidate;
            candidate.reserve(32);
            constexpr char hex[] = "0123456789abcdef";
            for (size_t i = 0; i < 16; ++i)
            {
                bNonzero = bNonzero || digest[i] != 0;
                candidate.push_back(hex[digest[i] >> 4]);
                candidate.push_back(hex[digest[i] & 15]);
            }
            if (!bNonzero)
            {
                return Fail(error, "reserved_zero_id");
            }
            outOwnerId = std::move(candidate);
            return true;
        }
        catch (const std::exception&)
        {
            return Fail(error, "derivation_exception");
        }
#endif
    }
} // namespace NorvesLib::Tools::AssetCook
