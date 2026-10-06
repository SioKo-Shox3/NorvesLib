#pragma once

#include "Container/String.h"
#include "Container/Span.h"
#include "Resource/SkeletalGltfData.h"
#include "Animation/SkeletalRestPose.h"
#include <filesystem>

namespace NorvesLib::Core::Gltf
{
    class BufferSet;
}

namespace NorvesLib::Core::AssetImport
{
    struct LoadedImportSettings;
}

namespace NorvesLib::Core::Skeletal
{
    struct RigV1Limits;
    // importSettings省略時はsource隣を自動探索する。明示値は呼出中だけ借用し、結果へ保持しない。
    // GLB/JSONの元bytes入口。outSourceBuffersはhash用の全sourceと借用BINを保持する。
    // GLB入力はoutSourceBuffersより長く保持し、その所有storageを入力に使わないこと。
    // 失敗時はoutSourceBuffersを空にする。decodedの頂点/関節/clipは独立所有する。
    [[nodiscard]] SkeletalGltfDecodeResult DecodeSkeletalGltf(Container::Span<const uint8_t> sourceBytes,
        const Container::String& sourcePath, Gltf::BufferSet* outSourceBuffers = nullptr,
        const AssetImport::LoadedImportSettings* importSettings = nullptr,
        const SkeletalGltfDecodeOptions* decodeOptions = nullptr);

    // GR82 Stage A: animations>=1。その他は旧入口と同じ1mesh/1skin/1mesh-node/128joint契約。
    // Armatureの中間親やmesh無しclipはまだ受けない。出力所有/失敗時buffer契約も上記と同じ。
    [[nodiscard]] SkeletalGltfDecodeResult DecodeRigGltf(Container::Span<const uint8_t> sourceBytes,
        const Container::String& sourcePath, Gltf::BufferSet* outSourceBuffers = nullptr,
        const AssetImport::LoadedImportSettings* importSettings = nullptr,
        const SkeletalGltfDecodeOptions* decodeOptions = nullptr);

    // native locatorの多clip入口。TCHAR/narrow文字列を経由せず、同じI/O境界へ渡す。
    [[nodiscard]] SkeletalGltfDecodeResult DecodeRigGltfNativePath(Container::Span<const uint8_t> sourceBytes,
        const std::filesystem::path& sourcePath, Gltf::BufferSet* outSourceBuffers = nullptr,
        const AssetImport::LoadedImportSettings* importSettings = nullptr,
        const SkeletalGltfDecodeOptions* decodeOptions = nullptr);

    // BVH clipを同じcookで追加するtarget専用。欠落/空animationsだけを0本として受ける。
    // null/不正型/壊れた既存clipは拒否し、旧1本以上入口やwriterの契約は変更しない。
    [[nodiscard]] SkeletalGltfDecodeResult DecodeBvhTargetRigGltfNativePath(Container::Span<const uint8_t> sourceBytes,
        const std::filesystem::path& sourcePath, Gltf::BufferSet* outSourceBuffers = nullptr,
        const AssetImport::LoadedImportSettings* importSettings = nullptr,
        const SkeletalGltfDecodeOptions* decodeOptions = nullptr);

    // v1専用。jointの直接TRSを同じdecode/設定readから取得する。旧入口には使わない。
    // target用の0clipを許可し、成功時だけrest/scaleを置換する。
    [[nodiscard]] SkeletalGltfDecodeResult DecodeRigAuthorRestGltfNativePath(
        Container::Span<const uint8_t> sourceBytes, const std::filesystem::path& sourcePath,
        Container::VariableArray<SkeletalRestTransform>& outRest, double& outResolvedScale, const RigV1Limits& limits,
        const AssetImport::LoadedImportSettings* importSettings = nullptr,
        const SkeletalGltfDecodeOptions* decodeOptions = nullptr);

    // 旧String入口の互換用。出力buffer配列を求めた場合は全source bytesを所有コピーする。
    using SkeletalGltfSourceBuffers = Container::VariableArray<Container::VariableArray<uint8_t>>;

    [[nodiscard]] SkeletalGltfDecodeResult DecodeSkeletalGltf(const Container::String& jsonText,
                                                              const Container::String& sourcePath,
                                                              SkeletalGltfSourceBuffers* outSourceBuffers = nullptr,
                                                              const AssetImport::LoadedImportSettings* importSettings = nullptr,
        const SkeletalGltfDecodeOptions* decodeOptions = nullptr);
} // namespace NorvesLib::Core::Skeletal
