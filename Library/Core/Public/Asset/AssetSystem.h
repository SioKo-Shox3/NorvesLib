#pragma once

#include "Asset/AssetFileReader.h"
#include "Asset/AssetManifest.h"
#include "Asset/AssetResolveResult.h"
#include "Container/String.h"
#include "Container/StringView.h"

namespace NorvesLib::Core::Asset
{
    /**
     * @brief Facade for resolving logical asset paths to cooked package entries or loose fallback blobs.
     *
     * Thread contract:
     * - ResolveAsset() and FindCookedVariant() are reentrant when no manifest mutation is running.
     * - Concurrent SetManifest(), ResetManifest(), or LoadManifestFromJsonText() with ResolveAsset()
     *   is not supported in Phase 7.
     * - This facade does not create GPU resources and owns no package cache.
     */
    class AssetSystem
    {
    public:
        AssetSystem();
        explicit AssetSystem(const Container::AnsiString &assetRoot);

        // マニフェストにないテクスチャ等をばらのファイルとして読むときの root を、クック済みの root と別にする。
        // 空なら従来どおりクック済みの root と同じ場所を読む。
        void SetLooseAssetRoot(const Container::AnsiString &looseAssetRoot);
        [[nodiscard]] const Container::AnsiString &GetLooseAssetRoot() const noexcept;

        // クック済みの sRGB 指定のテクスチャを、sRGB の復号なしの UNORM としてアップロードさせる。
        // 今のシェーダーは色のテクスチャ（アルベド）を標本のまま使い、ばらの PNG・JPG も UNORM で上げているため、
        // クック済みの色をばらと同じ見た目で描くための互換設定（既定は無効で、クック済みの指定のとおり sRGB で上げる）。
        void SetTreatSrgbTexturesAsLinear(bool bTreatAsLinear) noexcept { m_bTreatSrgbTexturesAsLinear = bTreatAsLinear; }
        [[nodiscard]] bool GetTreatSrgbTexturesAsLinear() const noexcept { return m_bTreatSrgbTexturesAsLinear; }

        // クック済みを使う前提の設定で、マニフェスト自体を読めなかったことを示す。立てておくと、マニフェストが無いために
        // ばらのファイルで読んだテクスチャも、クック済みが無いテクスチャとして警告の対象になる（既定は無効）。
        void SetCookedExpected(bool bCookedExpected) noexcept { m_bCookedExpected = bCookedExpected; }
        [[nodiscard]] bool IsCookedExpected() const noexcept { return m_bCookedExpected; }

        void ResetManifest();
        void SetManifest(const AssetManifest &manifest);
        [[nodiscard]] bool LoadManifestFromJsonText(const Container::String &jsonText, Container::AnsiStringView sourceName = {});

        [[nodiscard]] AssetManifestResolveResult FindCookedVariant(Container::AnsiStringView logicalPath,
                                                                   AssetKind kind,
                                                                   Container::AnsiStringView variant = AssetManifest::DefaultVariant) const;

        [[nodiscard]] AssetResolveResult ResolveAsset(const AssetResolveRequest &request) const;

        // クック済みのエントリの、パッケージファイル内の位置を求める。パッケージ全体を一度読んで位置を確かめるので、
        // 作成時に1回だけ呼び、その後の読み込みは GetCookedFileReader() の範囲読みで行う。
        // クック済みを使えない（ばらのファイルを読む・圧縮したエントリ・解決の失敗）ときは false。
        [[nodiscard]] bool TryResolveCookedRange(Container::AnsiStringView logicalPath,
                                                 AssetKind kind,
                                                 AssetCookedRange &outRange,
                                                 Container::AnsiString *pOutReason = nullptr,
                                                 Container::AnsiStringView variant = AssetManifest::DefaultVariant) const;

        // クック済みのパッケージを読むファイル読み込み（アセット root を持つ）。範囲読みはここから行う。
        [[nodiscard]] const AssetFileReader &GetCookedFileReader() const noexcept { return m_FileReader; }

        [[nodiscard]] AssetResolveResult ResolveAsset(Container::AnsiStringView logicalPath,
                                                      AssetKind kind,
                                                      Container::AnsiStringView variant = AssetManifest::DefaultVariant,
                                                      AssetFallbackMode fallbackMode = AssetFallbackMode::FailOnCookedFailure) const;

        // Manifest enumeration accessors. Delegate to the privately-held manifest so callers
        // (e.g. the NorvesLib Bridge adapter's asset.getManifest) can list cooked references
        // without reaching into AssetManifest directly. Bounds are the caller's responsibility:
        // index must be < GetAssetCount(). When no manifest is loaded, GetAssetCount() is 0.
        [[nodiscard]] size_t GetAssetCount() const noexcept;
        [[nodiscard]] const AssetCookedReference &GetAssetReference(size_t index) const noexcept;

    private:
        AssetManifest m_Manifest;
        AssetFileReader m_FileReader;
        AssetFileReader m_LooseFileReader;
        Container::AnsiString m_AssetRoot;
        Container::AnsiString m_LooseAssetRoot;
        bool m_bTreatSrgbTexturesAsLinear = false;
        bool m_bCookedExpected = false;
    };
}
