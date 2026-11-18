// ポストプロセスの中立な値、欄ごとの補間、固定容量のプロファイル合成を定義する。
#pragma once

#include "Text/IdentityPool.h"
#include <cstdint>

namespace NorvesLib::Core::Rendering
{
    namespace PostProcessFields
    {
        inline constexpr uint32_t ExposureBias = 1u << 0;
        inline constexpr uint32_t WhiteBalance = 1u << 1;
        inline constexpr uint32_t Contrast = 1u << 2;
        inline constexpr uint32_t ContrastPivot = 1u << 3;
        inline constexpr uint32_t Saturation = 1u << 4;
        inline constexpr uint32_t ShadowLift = 1u << 5;
        inline constexpr uint32_t HighlightRolloff = 1u << 6;
        inline constexpr uint32_t ShadowTint = 1u << 7;
        inline constexpr uint32_t HighlightTint = 1u << 8;
        inline constexpr uint32_t ProtectedHue = 1u << 9;
        inline constexpr uint32_t Vignette = 1u << 10;
        inline constexpr uint32_t Grain = 1u << 11;
        inline constexpr uint32_t ChromaticAberration = 1u << 12;
        inline constexpr uint32_t LensDirt = 1u << 13;
        inline constexpr uint32_t Look = 1u << 14;
        inline constexpr uint32_t Dither = 1u << 15;
        inline constexpr uint32_t All = (1u << 16) - 1;
        inline constexpr uint32_t None = 0;
    }

    // 以下の数値範囲は仮。範囲外は Sanitize で各成分の既定値へ戻す。
    struct PostProcessWhiteBalance
    {
        float Temperature = 0.0f; // -1～1。正で暖色。
        float Tint = 0.0f;        // -1～1。正で緑からマゼンタ方向。
    };

    struct PostProcessShadowLift
    {
        float Amount = 0.0f;  // 0～1。
        float RangeEV = 2.0f; // 0.25～8。支点から何 EV 下までを持ち上げるか。
    };

    struct PostProcessHighlightRolloff
    {
        float Start = 1.0f;    // 0.05～64。シーンリニアの輝度。
        float Strength = 0.0f; // 0～1。0 で無効。
    };

    struct PostProcessProtectedHue
    {
        bool bEnabled = false;
        float CenterDeg = 210.0f; // 0 以上 360 未満。
        float WidthDeg = 40.0f;   // 0～180。
        float Keep = 1.0f;        // 0～1。
    };

    struct PostProcessGrade
    {
        float ExposureBias = 0.0f; // -8～8 EV。自動露出と独立した見た目の補正。
        PostProcessWhiteBalance WhiteBalance;
        float Contrast = 1.0f;     // 0.1～4。log2 空間での支点まわりの傾き。
        float ContrastPivot = 0.18f; // 0.01～1。シーンリニアの支点。
        float Saturation = 1.0f;   // 0～4。
        PostProcessShadowLift ShadowLift;
        PostProcessHighlightRolloff HighlightRolloff;
        float ShadowTint[3] = {1.0f, 1.0f, 1.0f}; // 各 0～4。線形 RGB の乗算。
        float HighlightTint[3] = {1.0f, 1.0f, 1.0f}; // 各 0～4。線形 RGB の乗算。
        PostProcessProtectedHue ProtectedHue;
    };

    struct PostProcessVignette
    {
        float Intensity = 0.0f; // 0～1。
        float Radius = 0.8f;    // 0～2。
        float Softness = 0.5f;  // 0.001～2。
    };

    struct PostProcessLens
    {
        PostProcessVignette Vignette;
        float Grain = 0.0f;                     // 0～1。
        float ChromaticAberrationPixels = 0.0f; // 0～16 ピクセル。
        float LensDirtIntensity = 0.0f;         // 0～4。
    };

    struct PostProcessLook
    {
        // Assets/ 相対のパス。リテラルか IdentityPool 内の静的寿命の文字列だけを借りる。
        // nullptr は恒等の LUT として扱う。文字列の所有権は持たない。
        const char* LutA = nullptr;
        const char* LutB = nullptr;
        float Blend = 0.0f;     // 0～1。0 で LutA、1 で LutB。
        float Intensity = 1.0f; // 0～1。元の値に対する LUT の適用率。
    };

    /** @brief カメラごとにコピーできる、中立値を既定としたポストプロセス設定。 */
    struct PostProcessSettings
    {
        bool bValid = false; // false なら描画側は従来の静的設定を使う。
        PostProcessGrade Grade;
        PostProcessLens Lens;
        PostProcessLook Look;
        float DitherStrength = 1.0f / 255.0f; // 仮の範囲は 0～4/255。

        /** @brief 不正な数値を成分ごとの既定値に戻し、変更した欄の組を返す。 */
        uint32_t Sanitize();
        /** @brief 無効な側を中立値として補間する。端点では入力をそのまま返す。 */
        static PostProcessSettings Lerp(const PostProcessSettings& a, const PostProcessSettings& b, float t);
        /** @brief 指定した欄の組だけを補間する。bValid は a のまま保持する。 */
        static PostProcessSettings LerpMasked(const PostProcessSettings& a, const PostProcessSettings& b, float t, uint32_t mask);
    };

    struct PostProcessProfile
    {
        Identity Name;
        PostProcessSettings Settings;
        uint32_t OverrideMask = PostProcessFields::All;
    };

    enum class PostProcessLayer : uint8_t
    {
        Base = 0,
        Weather = 1,
        Shot = 2,
        Count = 3
    };

    enum class PostProcessBlendCurve : uint8_t
    {
        Linear,
        SmoothStep,
        EaseIn,
        EaseOut
    };

    /** @brief 場所・天候・ショットを順に重ねる、ヒープ確保のないミキサー。 */
    class PostProcessProfileMixer
    {
    public:
        void SetLayer(PostProcessLayer layer, const PostProcessProfile& profile);
        void ClearLayer(PostProcessLayer layer);
        void SetLayerWeight(PostProcessLayer layer, float weight);
        void BlendTo(PostProcessLayer layer, const PostProcessProfile& profile, float seconds, PostProcessBlendCurve curve);
        void BlendOut(PostProcessLayer layer, float seconds, PostProcessBlendCurve curve);
        /** @brief ヒットストップの影響を受けない dt で進める。NaN と負値は 0。 */
        void Advance(float unscaledDt);
        const PostProcessSettings& GetOutput() const;
        bool IsLayerActive(PostProcessLayer layer) const;
        bool IsTransitioning(PostProcessLayer layer) const;

    private:
        struct LayerState
        {
            PostProcessProfile From;
            PostProcessProfile To;
            PostProcessSettings Frozen;
            float Weight = 1.0f;
            double Elapsed = 0.0;
            double Duration = 0.0;
            PostProcessBlendCurve Curve = PostProcessBlendCurve::Linear;
            bool bFromActive = false;
            bool bToActive = false;
            bool bFrozen = false;
            bool bTransitioning = false;
        };

        static constexpr uint32_t LayerCount = static_cast<uint32_t>(PostProcessLayer::Count);
        // 範囲外のレイヤー指定は変更を行わず、照会では false を返す。
        static bool IsValidLayer(PostProcessLayer layer);
        void BeginTransition(PostProcessLayer layer, const PostProcessProfile& profile, bool bToActive,
            float seconds, PostProcessBlendCurve curve);
        void RebuildOutput();

        LayerState m_Layers[LayerCount];
        PostProcessSettings m_LayerOutputs[LayerCount]; // 各レイヤーまでの直前の合成結果。
        PostProcessSettings m_Output;
    };
} // namespace NorvesLib::Core::Rendering
