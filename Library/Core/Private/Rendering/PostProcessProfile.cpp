// ポストプロセス設定の検査・補間と、固定レイヤーの決定的な合成を実装する。
#include "Rendering/PostProcessProfile.h"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <type_traits>

namespace NorvesLib::Core::Rendering
{
    static_assert(std::is_trivially_copyable_v<PostProcessSettings>);

    namespace
    {
        uint32_t SanitizeValue(float& value, float minimum, float maximum, float fallback, uint32_t field)
        {
            if (!std::isfinite(value) || value < minimum || value > maximum)
            {
                value = fallback;
                return field;
            }
            return PostProcessFields::None;
        }

        float ClampWeight(float value)
        {
            return std::isnan(value) ? 0.0f : std::clamp(value, 0.0f, 1.0f);
        }

        float BlendLinear(float a, float b, float t)
        {
            if (t <= 0.0f)
            {
                return a;
            }
            if (t >= 1.0f)
            {
                return b;
            }
            if (a == b)
            {
                return a;
            }
            return a + (b - a) * t;
        }

        float BlendLogarithmic(float a, float b, float t)
        {
            if (t <= 0.0f)
            {
                return a;
            }
            if (t >= 1.0f)
            {
                return b;
            }
            if (a == b)
            {
                return a;
            }
            return std::exp2(BlendLinear(std::log2(std::max(a, 1.0e-4f)),
                std::log2(std::max(b, 1.0e-4f)), t));
        }

        float WrapDegrees(float value)
        {
            float wrapped = std::fmod(value, 360.0f);
            if (wrapped < 0.0f)
            {
                wrapped += 360.0f;
            }
            // 負の微小値へ 360 を足したときの丸めも範囲内へ戻す。
            return wrapped >= 360.0f ? 0.0f : wrapped;
        }

        PostProcessProtectedHue BlendProtectedHue(const PostProcessProtectedHue& a,
            const PostProcessProtectedHue& b, float t)
        {
            if (t <= 0.0f)
            {
                return a;
            }
            if (t >= 1.0f)
            {
                return b;
            }
            if (!a.bEnabled && !b.bEnabled)
            {
                return a;
            }

            PostProcessProtectedHue from = a.bEnabled ? a : b;
            PostProcessProtectedHue to = b.bEnabled ? b : a;
            if (!a.bEnabled)
            {
                from.Keep = 0.0f;
            }
            if (!b.bEnabled)
            {
                to.Keep = 0.0f;
            }

            PostProcessProtectedHue result = from;
            result.bEnabled = true;
            if (from.CenterDeg != to.CenterDeg)
            {
                float delta = WrapDegrees(to.CenterDeg - from.CenterDeg);
                // 真反対の色相では正方向の弧を採る。
                if (delta > 180.0f)
                {
                    delta -= 360.0f;
                }
                result.CenterDeg = WrapDegrees(from.CenterDeg + delta * t);
            }
            result.WidthDeg = BlendLinear(from.WidthDeg, to.WidthDeg, t);
            result.Keep = BlendLinear(from.Keep, to.Keep, t);
            return result;
        }

        bool SamePath(const char* a, const char* b)
        {
            return a == b || (a != nullptr && b != nullptr && std::strcmp(a, b) == 0);
        }

        PostProcessLook BlendLook(const PostProcessLook& a, const PostProcessLook& b, float t)
        {
            if (t <= 0.0f)
            {
                return a;
            }
            if (t >= 1.0f)
            {
                return b;
            }

            PostProcessLook result = a;
            result.Intensity = BlendLinear(a.Intensity, b.Intensity, t);
            if (a.LutA == b.LutA && a.LutB == b.LutB && a.Blend == b.Blend)
            {
                return result;
            }

            struct Candidate
            {
                const char* Path = nullptr;
                float Weight = 0.0f;
            };
            const Candidate inputs[4] = {
                {a.LutA, (1.0f - t) * (1.0f - a.Blend)},
                {a.LutB, (1.0f - t) * a.Blend},
                {b.LutA, t * (1.0f - b.Blend)},
                {b.LutB, t * b.Blend}};
            Candidate candidates[4];
            uint32_t count = 0;
            for (const Candidate& input : inputs)
            {
                uint32_t index = 0;
                while (index < count && !SamePath(candidates[index].Path, input.Path))
                {
                    ++index;
                }
                if (index == count)
                {
                    candidates[count++] = input;
                }
                else
                {
                    candidates[index].Weight += input.Weight;
                }
            }

            // 安定な挿入順で同じ重みなら最初に列挙されたパスを優先する。
            for (uint32_t index = 1; index < count; ++index)
            {
                const Candidate candidate = candidates[index];
                uint32_t position = index;
                while (position > 0 && candidate.Weight > candidates[position - 1].Weight)
                {
                    candidates[position] = candidates[position - 1];
                    --position;
                }
                candidates[position] = candidate;
            }
            result.LutA = candidates[0].Path;
            // 1 候補のときは同じ LUT を両方に入れ、Blend を厳密に 0 とする。
            result.LutB = count > 1 ? candidates[1].Path : candidates[0].Path;
            const float secondWeight = count > 1 ? candidates[1].Weight : 0.0f;
            const float total = candidates[0].Weight + secondWeight;
            result.Blend = total != 0.0f ? secondWeight / total : 0.0f;
            return result;
        }

        float EvaluateCurve(float t, PostProcessBlendCurve curve)
        {
            if (t <= 0.0f)
            {
                return 0.0f;
            }
            if (t >= 1.0f)
            {
                return 1.0f;
            }
            // 倍精度で計算し、端点近傍の丸めで単調性や値域を損なわないようにする。
            const double progress = static_cast<double>(t);
            switch (curve)
            {
            case PostProcessBlendCurve::SmoothStep:
                return static_cast<float>(progress * progress * (3.0 - 2.0 * progress));
            case PostProcessBlendCurve::EaseIn:
                return static_cast<float>(progress * progress);
            case PostProcessBlendCurve::EaseOut:
                return static_cast<float>(progress * (2.0 - progress));
            default:
                return t;
            }
        }

        PostProcessSettings Apply(const PostProcessSettings& input, const PostProcessProfile& profile, float weight)
        {
            PostProcessSettings base = input;
            if (!base.bValid)
            {
                base = PostProcessSettings{};
                base.bValid = true;
            }
            PostProcessSettings result = PostProcessSettings::LerpMasked(base, profile.Settings, weight, profile.OverrideMask);
            result.bValid = true;
            return result;
        }
    }

    uint32_t PostProcessSettings::Sanitize()
    {
        const PostProcessSettings defaults;
        uint32_t changed = PostProcessFields::None;
        changed |= SanitizeValue(Grade.ExposureBias, -8.0f, 8.0f, defaults.Grade.ExposureBias, PostProcessFields::ExposureBias);
        changed |= SanitizeValue(Grade.WhiteBalance.Temperature, -1.0f, 1.0f,
            defaults.Grade.WhiteBalance.Temperature, PostProcessFields::WhiteBalance);
        changed |= SanitizeValue(Grade.WhiteBalance.Tint, -1.0f, 1.0f,
            defaults.Grade.WhiteBalance.Tint, PostProcessFields::WhiteBalance);
        changed |= SanitizeValue(Grade.Contrast, 0.1f, 4.0f, defaults.Grade.Contrast, PostProcessFields::Contrast);
        changed |= SanitizeValue(Grade.ContrastPivot, 0.01f, 1.0f, defaults.Grade.ContrastPivot, PostProcessFields::ContrastPivot);
        changed |= SanitizeValue(Grade.Saturation, 0.0f, 4.0f, defaults.Grade.Saturation, PostProcessFields::Saturation);
        changed |= SanitizeValue(Grade.ShadowLift.Amount, 0.0f, 1.0f,
            defaults.Grade.ShadowLift.Amount, PostProcessFields::ShadowLift);
        changed |= SanitizeValue(Grade.ShadowLift.RangeEV, 0.25f, 8.0f,
            defaults.Grade.ShadowLift.RangeEV, PostProcessFields::ShadowLift);
        changed |= SanitizeValue(Grade.HighlightRolloff.Start, 0.05f, 64.0f,
            defaults.Grade.HighlightRolloff.Start, PostProcessFields::HighlightRolloff);
        changed |= SanitizeValue(Grade.HighlightRolloff.Strength, 0.0f, 1.0f,
            defaults.Grade.HighlightRolloff.Strength, PostProcessFields::HighlightRolloff);
        for (uint32_t component = 0; component < 3; ++component)
        {
            changed |= SanitizeValue(Grade.ShadowTint[component], 0.0f, 4.0f,
                defaults.Grade.ShadowTint[component], PostProcessFields::ShadowTint);
            changed |= SanitizeValue(Grade.HighlightTint[component], 0.0f, 4.0f,
                defaults.Grade.HighlightTint[component], PostProcessFields::HighlightTint);
        }
        if (Grade.ProtectedHue.CenterDeg == 360.0f)
        {
            Grade.ProtectedHue.CenterDeg = 0.0f;
            changed |= PostProcessFields::ProtectedHue;
        }
        changed |= SanitizeValue(Grade.ProtectedHue.CenterDeg, 0.0f, 360.0f,
            defaults.Grade.ProtectedHue.CenterDeg, PostProcessFields::ProtectedHue);
        changed |= SanitizeValue(Grade.ProtectedHue.WidthDeg, 0.0f, 180.0f,
            defaults.Grade.ProtectedHue.WidthDeg, PostProcessFields::ProtectedHue);
        changed |= SanitizeValue(Grade.ProtectedHue.Keep, 0.0f, 1.0f,
            defaults.Grade.ProtectedHue.Keep, PostProcessFields::ProtectedHue);
        changed |= SanitizeValue(Lens.Vignette.Intensity, 0.0f, 1.0f,
            defaults.Lens.Vignette.Intensity, PostProcessFields::Vignette);
        changed |= SanitizeValue(Lens.Vignette.Radius, 0.0f, 2.0f,
            defaults.Lens.Vignette.Radius, PostProcessFields::Vignette);
        changed |= SanitizeValue(Lens.Vignette.Softness, 0.001f, 2.0f,
            defaults.Lens.Vignette.Softness, PostProcessFields::Vignette);
        changed |= SanitizeValue(Lens.Grain, 0.0f, 1.0f, defaults.Lens.Grain, PostProcessFields::Grain);
        changed |= SanitizeValue(Lens.ChromaticAberrationPixels, 0.0f, 16.0f,
            defaults.Lens.ChromaticAberrationPixels, PostProcessFields::ChromaticAberration);
        changed |= SanitizeValue(Lens.LensDirtIntensity, 0.0f, 4.0f,
            defaults.Lens.LensDirtIntensity, PostProcessFields::LensDirt);
        changed |= SanitizeValue(Look.Blend, 0.0f, 1.0f, defaults.Look.Blend, PostProcessFields::Look);
        changed |= SanitizeValue(Look.Intensity, 0.0f, 1.0f, defaults.Look.Intensity, PostProcessFields::Look);
        changed |= SanitizeValue(DitherStrength, 0.0f, 4.0f / 255.0f, defaults.DitherStrength, PostProcessFields::Dither);
        return changed;
    }

    PostProcessSettings PostProcessSettings::LerpMasked(const PostProcessSettings& a,
        const PostProcessSettings& b, float t, uint32_t mask)
    {
        if (std::isnan(t) || t <= 0.0f || (mask & PostProcessFields::All) == 0)
        {
            return a;
        }
        PostProcessSettings result = a;
        if ((mask & PostProcessFields::ExposureBias) != 0)
        {
            result.Grade.ExposureBias = BlendLinear(a.Grade.ExposureBias, b.Grade.ExposureBias, t);
        }
        if ((mask & PostProcessFields::WhiteBalance) != 0)
        {
            result.Grade.WhiteBalance.Temperature = BlendLinear(a.Grade.WhiteBalance.Temperature, b.Grade.WhiteBalance.Temperature, t);
            result.Grade.WhiteBalance.Tint = BlendLinear(a.Grade.WhiteBalance.Tint, b.Grade.WhiteBalance.Tint, t);
        }
        if ((mask & PostProcessFields::Contrast) != 0)
        {
            result.Grade.Contrast = BlendLogarithmic(a.Grade.Contrast, b.Grade.Contrast, t);
        }
        if ((mask & PostProcessFields::ContrastPivot) != 0)
        {
            result.Grade.ContrastPivot = BlendLinear(a.Grade.ContrastPivot, b.Grade.ContrastPivot, t);
        }
        if ((mask & PostProcessFields::Saturation) != 0)
        {
            result.Grade.Saturation = BlendLogarithmic(a.Grade.Saturation, b.Grade.Saturation, t);
        }
        if ((mask & PostProcessFields::ShadowLift) != 0)
        {
            result.Grade.ShadowLift.Amount = BlendLinear(a.Grade.ShadowLift.Amount, b.Grade.ShadowLift.Amount, t);
            result.Grade.ShadowLift.RangeEV = BlendLinear(a.Grade.ShadowLift.RangeEV, b.Grade.ShadowLift.RangeEV, t);
        }
        if ((mask & PostProcessFields::HighlightRolloff) != 0)
        {
            result.Grade.HighlightRolloff.Start = BlendLinear(a.Grade.HighlightRolloff.Start, b.Grade.HighlightRolloff.Start, t);
            result.Grade.HighlightRolloff.Strength = BlendLinear(a.Grade.HighlightRolloff.Strength, b.Grade.HighlightRolloff.Strength, t);
        }
        for (uint32_t component = 0; component < 3; ++component)
        {
            if ((mask & PostProcessFields::ShadowTint) != 0)
            {
                result.Grade.ShadowTint[component] = BlendLinear(a.Grade.ShadowTint[component], b.Grade.ShadowTint[component], t);
            }
            if ((mask & PostProcessFields::HighlightTint) != 0)
            {
                result.Grade.HighlightTint[component] = BlendLinear(a.Grade.HighlightTint[component], b.Grade.HighlightTint[component], t);
            }
        }
        if ((mask & PostProcessFields::ProtectedHue) != 0)
        {
            result.Grade.ProtectedHue = BlendProtectedHue(a.Grade.ProtectedHue, b.Grade.ProtectedHue, t);
        }
        if ((mask & PostProcessFields::Vignette) != 0)
        {
            result.Lens.Vignette.Intensity = BlendLinear(a.Lens.Vignette.Intensity, b.Lens.Vignette.Intensity, t);
            result.Lens.Vignette.Radius = BlendLinear(a.Lens.Vignette.Radius, b.Lens.Vignette.Radius, t);
            result.Lens.Vignette.Softness = BlendLinear(a.Lens.Vignette.Softness, b.Lens.Vignette.Softness, t);
        }
        if ((mask & PostProcessFields::Grain) != 0)
        {
            result.Lens.Grain = BlendLinear(a.Lens.Grain, b.Lens.Grain, t);
        }
        if ((mask & PostProcessFields::ChromaticAberration) != 0)
        {
            result.Lens.ChromaticAberrationPixels = BlendLinear(a.Lens.ChromaticAberrationPixels, b.Lens.ChromaticAberrationPixels, t);
        }
        if ((mask & PostProcessFields::LensDirt) != 0)
        {
            result.Lens.LensDirtIntensity = BlendLinear(a.Lens.LensDirtIntensity, b.Lens.LensDirtIntensity, t);
        }
        if ((mask & PostProcessFields::Look) != 0)
        {
            result.Look = BlendLook(a.Look, b.Look, t);
        }
        if ((mask & PostProcessFields::Dither) != 0)
        {
            result.DitherStrength = BlendLinear(a.DitherStrength, b.DitherStrength, t);
        }
        return result;
    }

    PostProcessSettings PostProcessSettings::Lerp(const PostProcessSettings& a, const PostProcessSettings& b, float t)
    {
        if (std::isnan(t) || t <= 0.0f)
        {
            return a;
        }
        if (t >= 1.0f)
        {
            return b;
        }
        if (!a.bValid && !b.bValid)
        {
            return a;
        }
        PostProcessSettings from = a.bValid ? a : PostProcessSettings{};
        PostProcessSettings to = b.bValid ? b : PostProcessSettings{};
        from.bValid = true;
        to.bValid = true;
        return LerpMasked(from, to, t, PostProcessFields::All);
    }

    bool PostProcessProfileMixer::IsValidLayer(PostProcessLayer layer)
    {
        return static_cast<uint32_t>(layer) < LayerCount;
    }

    void PostProcessProfileMixer::SetLayer(PostProcessLayer layer, const PostProcessProfile& profile)
    {
        if (!IsValidLayer(layer))
        {
            return;
        }
        LayerState& state = m_Layers[static_cast<uint32_t>(layer)];
        const float weight = state.Weight;
        state = LayerState{};
        state.Weight = weight;
        state.From = profile;
        state.bFromActive = true;
        RebuildOutput();
    }

    void PostProcessProfileMixer::ClearLayer(PostProcessLayer layer)
    {
        if (!IsValidLayer(layer))
        {
            return;
        }
        LayerState& state = m_Layers[static_cast<uint32_t>(layer)];
        const float weight = state.Weight;
        state = LayerState{};
        state.Weight = weight;
        RebuildOutput();
    }

    void PostProcessProfileMixer::SetLayerWeight(PostProcessLayer layer, float weight)
    {
        if (!IsValidLayer(layer))
        {
            return;
        }
        m_Layers[static_cast<uint32_t>(layer)].Weight = ClampWeight(weight);
        RebuildOutput();
    }

    void PostProcessProfileMixer::BlendTo(PostProcessLayer layer, const PostProcessProfile& profile,
        float seconds, PostProcessBlendCurve curve)
    {
        BeginTransition(layer, profile, true, seconds, curve);
    }

    void PostProcessProfileMixer::BlendOut(PostProcessLayer layer, float seconds, PostProcessBlendCurve curve)
    {
        BeginTransition(layer, PostProcessProfile{}, false, seconds, curve);
    }

    void PostProcessProfileMixer::BeginTransition(PostProcessLayer layer, const PostProcessProfile& profile,
        bool bToActive, float seconds, PostProcessBlendCurve curve)
    {
        if (!IsValidLayer(layer))
        {
            return;
        }
        if (std::isnan(seconds) || seconds <= 0.0f)
        {
            if (bToActive)
            {
                SetLayer(layer, profile);
            }
            else
            {
                ClearLayer(layer);
            }
            return;
        }
        const uint32_t index = static_cast<uint32_t>(layer);
        LayerState& state = m_Layers[index];
        if (state.bTransitioning)
        {
            // このレイヤーまでの値を固定し、下位レイヤーが変わっても元を置き換える。
            state.Frozen = m_LayerOutputs[index];
            state.bFrozen = true;
            state.bFromActive = true;
        }
        state.To = profile;
        state.bToActive = bToActive;
        state.Elapsed = 0.0;
        state.Duration = static_cast<double>(seconds);
        state.Curve = curve;
        state.bTransitioning = true;
        RebuildOutput();
    }

    void PostProcessProfileMixer::Advance(float unscaledDt)
    {
        const double dt = std::isnan(unscaledDt) || unscaledDt < 0.0f ? 0.0 : static_cast<double>(unscaledDt);
        for (LayerState& state : m_Layers)
        {
            if (!state.bTransitioning)
            {
                continue;
            }
            state.Elapsed = std::min(state.Elapsed + dt, state.Duration);
            if (state.Elapsed >= state.Duration)
            {
                // 終点は補間せず、即時設定と同じ経路でプロファイルを重ねる。
                state.From = state.To;
                state.bFromActive = state.bToActive;
                state.bFrozen = false;
                state.bTransitioning = false;
                state.bToActive = false;
            }
        }
        RebuildOutput();
    }

    void PostProcessProfileMixer::RebuildOutput()
    {
        PostProcessSettings result;
        for (uint32_t index = 0; index < LayerCount; ++index)
        {
            const LayerState& state = m_Layers[index];
            PostProcessSettings from = result;
            if (state.bFromActive)
            {
                from = state.bFrozen ? state.Frozen : Apply(result, state.From, state.Weight);
            }
            if (state.bTransitioning)
            {
                const PostProcessSettings to = state.bToActive ? Apply(result, state.To, state.Weight) : result;
                const float t = static_cast<float>(state.Elapsed / state.Duration);
                result = PostProcessSettings::Lerp(from, to, EvaluateCurve(t, state.Curve));
            }
            else
            {
                result = from;
            }
            m_LayerOutputs[index] = result;
        }
        m_Output = result;
    }

    const PostProcessSettings& PostProcessProfileMixer::GetOutput() const
    {
        return m_Output;
    }

    bool PostProcessProfileMixer::IsLayerActive(PostProcessLayer layer) const
    {
        if (!IsValidLayer(layer))
        {
            return false;
        }
        const LayerState& state = m_Layers[static_cast<uint32_t>(layer)];
        return state.bFromActive || (state.bTransitioning && state.bToActive);
    }

    bool PostProcessProfileMixer::IsTransitioning(PostProcessLayer layer) const
    {
        return IsValidLayer(layer) && m_Layers[static_cast<uint32_t>(layer)].bTransitioning;
    }
} // namespace NorvesLib::Core::Rendering
