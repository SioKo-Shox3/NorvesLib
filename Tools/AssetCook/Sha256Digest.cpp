#include "Sha256Digest.h"

#if defined(_WIN32)
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <Windows.h>
#include <bcrypt.h>
#endif

#include <limits>

namespace NorvesLib::Tools::AssetCook
{
    bool ComputeSha256Hex(const uint8_t *data, size_t size, Core::Container::AnsiString &outHex)
    {
        outHex.clear();
#if defined(_WIN32)
        BCRYPT_HASH_HANDLE hash = nullptr;
        if (BCryptCreateHash(BCRYPT_SHA256_ALG_HANDLE, &hash, nullptr, 0, nullptr, 0, 0) < 0)
        {
            return false;
        }
        // ULONG に収まる大きさずつ渡す
        bool bSucceeded = true;
        size_t offset = 0;
        while (bSucceeded && offset < size)
        {
            const size_t chunk = size - offset < static_cast<size_t>(1u << 30) ? size - offset : static_cast<size_t>(1u << 30);
            bSucceeded = BCryptHashData(hash, const_cast<PUCHAR>(data + offset), static_cast<ULONG>(chunk), 0) >= 0;
            offset += chunk;
        }
        uint8_t digest[32] = {};
        bSucceeded = bSucceeded && BCryptFinishHash(hash, digest, static_cast<ULONG>(sizeof(digest)), 0) >= 0;
        BCryptDestroyHash(hash);
        if (!bSucceeded)
        {
            return false;
        }
        constexpr char kHex[] = "0123456789abcdef";
        outHex.reserve(64);
        for (const uint8_t value : digest)
        {
            outHex += kHex[value >> 4];
            outHex += kHex[value & 0x0f];
        }
        return true;
#else
        (void)data;
        (void)size;
        return false;
#endif
    }
} // namespace NorvesLib::Tools::AssetCook
