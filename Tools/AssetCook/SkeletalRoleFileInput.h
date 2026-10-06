#pragma once
#include "SkeletalRoleFileRequest.h"

namespace NorvesLib::Tools::AssetCook
{
    [[nodiscard]] bool DecodeSkeletalRoleUtf8Text(Core::Container::AnsiStringView bytes, Core::Container::String& out);
    struct SingleAssetCookRequest;
    struct SkeletalRoleFileInputs
    {
        Core::Container::VariableArray<uint8_t> BvhBytes, ProfileBytes;
    };
    [[nodiscard]] bool ValidateSkeletalRoleFileRequest(const SingleAssetCookRequest& request,
                                                       Core::Container::AnsiString& error);
    // regular fileの長さを確保前に検査し、同じreadの所有bytesを返す。成功時のみ置換。
    [[nodiscard]] bool LoadSkeletalRoleFileInputs(const SkeletalRoleFileRequest& request, SkeletalRoleFileInputs& out,
                                                  Core::Container::AnsiString& error);
    // inputsは返した要求より長く生存し、呼出中不変であること。
    [[nodiscard]] SkeletalRoleProfileCookRequest MakeSkeletalRoleBytesRequest(const SkeletalRoleFileRequest& request,
                                                                              const SkeletalRoleFileInputs& inputs);
    [[nodiscard]] bool AppendSkeletalRoleFileSettingsHash(uint64_t seed, const SkeletalRoleFileRequest& request,
                                                          uint64_t& out, Core::Container::AnsiString& error);
    [[nodiscard]] bool FingerprintSkeletalRoleFileSource(const uint8_t* source, size_t size,
                                                         const SingleAssetCookRequest& request,
                                                         ModelCookFingerprint& out, Core::Container::AnsiString& error);
} // namespace NorvesLib::Tools::AssetCook
