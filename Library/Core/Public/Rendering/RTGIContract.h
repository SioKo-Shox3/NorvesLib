#pragma once

#include "RHI/DeviceCapabilities.h"
#include "RHI/ITexture.h"
#include "RHI/RHITypes.h"

#include <cstdint>

namespace NorvesLib::Core::Rendering
{
    inline constexpr uint32_t RTGIHistoryMaximumAge = 8u;
    inline constexpr uint32_t RTGIDiffuseBounceCount = 1u;
    inline constexpr RHI::Format RTGIDiffuseIndirectRadianceFormat =
        RHI::Format::R16G16B16A16_FLOAT;
    inline constexpr RHI::Format RTGIHistoryAgeFormat = RHI::Format::R16_FLOAT;
    inline constexpr RHI::Format RTGIHistoryConfidenceFormat = RHI::Format::R16_FLOAT;
    inline constexpr RHI::Format RTGIHistoryGBufferFormat =
        RHI::Format::R16G16B16A16_FLOAT;
    inline constexpr float RTGIHistoryMaximumWeight = 0.9f;
    inline constexpr float RTGIHistoryLightRevisionWeightLimit = 0.25f;
    inline constexpr float RTGIHistoryInitialConfidence = 0.25f;
    inline constexpr float RTGIHistoryNormalRejectionDot = 0.9f;
    inline constexpr float RTGIHistoryDepthRejectionThreshold = 0.02f;
    inline constexpr float RTGIHistoryMaterialRejectionThreshold = 0.05f;

    /**
     * @brief R6のray-query GIが必要とするRHI能力
     *
     * Vulkan型や拡張名をRendering層へ漏らさず、論理デバイスで有効な能力だけを
     * フレーム契約へコピーします。
     */
    struct RTGIRayQueryCapability
    {
        bool bAccelerationStructure = false;
        bool bRayQuery = false;
        bool bBufferDeviceAddress = false;
        bool bShaderInt64 = false;

        bool IsUsable() const
        {
            return bAccelerationStructure && bRayQuery && bBufferDeviceAddress &&
                   bShaderInt64;
        }
    };

    inline RTGIRayQueryCapability MakeRTGIRayQueryCapability(
        const RHI::DeviceCapabilities& capabilities)
    {
        RTGIRayQueryCapability result;
        result.bAccelerationStructure = capabilities.RayTracing.bAccelerationStructure;
        result.bRayQuery = capabilities.RayTracing.bRayQuery;
        result.bBufferDeviceAddress = capabilities.bBufferDeviceAddress;
        result.bShaderInt64 = capabilities.bShaderInt64;
        return result;
    }

    enum class RTGIIndirectLightingSource : uint8_t
    {
        Raster,
        IBL,
        DDGI,
        RTGI
    };

    enum class RTGIFallbackReason : uint8_t
    {
        None,
        Disabled,
        CapabilityUnavailable,
        TLASUnavailable,
        ResourceUnavailable
    };

    /**
     * @brief 1 bounce diffuse indirect radianceの公開結果
     *
     * Radianceはscene-linearかつpre-exposedで、LightingPassの外から再度pre-exposureを
     * 適用しません。完全な結果だけがRTGI選択へ進み、不完全な結果は公開しません。
     */
    struct RTGIResult
    {
        RHI::TexturePtr DiffuseIndirectRadiance;
        RHI::ResourceState State = RHI::ResourceState::Undefined;
        RHI::Format Format = RTGIDiffuseIndirectRadianceFormat;
        uint32_t BounceCount = RTGIDiffuseBounceCount;
        uint32_t Width = 0u;
        uint32_t Height = 0u;
        uint64_t FrameNumber = 0u;
        uint64_t SceneRevision = 0u;
        uint64_t LightRevision = 0u;
        bool bPreExposed = true;
        bool bDiffuse = true;
        bool bValid = false;

        bool IsComplete() const
        {
            return bValid && bPreExposed && DiffuseIndirectRadiance &&
                   State == RHI::ResourceState::ShaderResource &&
                   bDiffuse && BounceCount == RTGIDiffuseBounceCount &&
                   Format == RTGIDiffuseIndirectRadianceFormat && Width > 0u && Height > 0u &&
                   DiffuseIndirectRadiance->GetWidth() == Width &&
                   DiffuseIndirectRadiance->GetHeight() == Height &&
                   DiffuseIndirectRadiance->GetFormat() == RTGIDiffuseIndirectRadianceFormat &&
                   (DiffuseIndirectRadiance->GetUsage() & RHI::ResourceUsage::ShaderRead) !=
                       RHI::ResourceUsage::None;
        }

        bool IsForFrame(uint64_t frameNumber,
                        uint64_t sceneRevision,
                        uint64_t lightRevision) const
        {
            return FrameNumber == frameNumber && SceneRevision == sceneRevision &&
                   LightRevision == lightRevision;
        }

        void Clear()
        {
            DiffuseIndirectRadiance.reset();
            State = RHI::ResourceState::Undefined;
            Format = RTGIDiffuseIndirectRadianceFormat;
            BounceCount = RTGIDiffuseBounceCount;
            Width = 0u;
            Height = 0u;
            FrameNumber = 0u;
            SceneRevision = 0u;
            LightRevision = 0u;
            bPreExposed = true;
            bDiffuse = true;
            bValid = false;
        }
    };

    /**
     * @brief 履歴の一方(currentまたはhistory)に属するresource集合
     *
     * Radiance、age、confidenceは同じ寸法の2D resourceとして公開し、scene colorや
     * GBufferの所有権は持ちません。
     */
    struct RTGIHistoryResourceSet
    {
        RHI::TexturePtr Radiance;
        RHI::TexturePtr Age;
        RHI::TexturePtr Confidence;
        RHI::ResourceState State = RHI::ResourceState::Undefined;
        uint32_t Width = 0u;
        uint32_t Height = 0u;
        uint64_t SceneRevision = 0u;
        uint64_t LightRevision = 0u;
        uint32_t AgeFrames = 0u;
        bool bValid = false;

        bool IsComplete() const
        {
            return bValid && Radiance && Age && Confidence &&
                   State == RHI::ResourceState::ShaderResource && Width > 0u && Height > 0u &&
                   AgeFrames <= RTGIHistoryMaximumAge && Radiance->GetWidth() == Width &&
                   Radiance->GetHeight() == Height && Age->GetWidth() == Width &&
                   Age->GetHeight() == Height && Confidence->GetWidth() == Width &&
                   Confidence->GetHeight() == Height &&
                   Radiance->GetFormat() == RTGIDiffuseIndirectRadianceFormat &&
                   Age->GetFormat() == RTGIHistoryAgeFormat &&
                   Confidence->GetFormat() == RTGIHistoryConfidenceFormat &&
                   (Radiance->GetUsage() & RHI::ResourceUsage::ShaderRead) !=
                       RHI::ResourceUsage::None &&
                   (Age->GetUsage() & RHI::ResourceUsage::ShaderRead) !=
                       RHI::ResourceUsage::None &&
                   (Confidence->GetUsage() & RHI::ResourceUsage::ShaderRead) !=
                       RHI::ResourceUsage::None;
        }

        void Clear()
        {
            Radiance.reset();
            Age.reset();
            Confidence.reset();
            State = RHI::ResourceState::Undefined;
            Width = 0u;
            Height = 0u;
            SceneRevision = 0u;
            LightRevision = 0u;
            AgeFrames = 0u;
            bValid = false;
        }
    };

    /**
     * @brief current/historyのping-pong履歴公開値
     */
    struct RTGIHistoryResources
    {
        RTGIHistoryResourceSet Current;
        RTGIHistoryResourceSet History;
        uint64_t FrameNumber = 0u;
        bool bValid = false;

        bool IsComplete() const
        {
            return bValid && Current.IsComplete() && History.IsComplete() &&
                   Current.Width == History.Width && Current.Height == History.Height;
        }

        bool IsForFrame(uint64_t frameNumber,
                        uint64_t sceneRevision,
                        uint64_t lightRevision) const
        {
            return IsComplete() && FrameNumber == frameNumber &&
                   Current.SceneRevision == sceneRevision &&
                   Current.LightRevision == lightRevision;
        }

        /**
         * @brief 履歴側のrevision差を後段の棄却・weight制御へ渡す
         *
         * 動的な物体移動による履歴差はここで保持し、RTGI選択を直ちにfallbackへ
         * 切り替える条件にはしません。構成変更時はcurrentのrevision不一致を
         * IsForFrameで検出し、古い履歴を採用しません。
         */
        bool HasHistoryRevisionMismatch(uint64_t sceneRevision,
                                        uint64_t lightRevision) const
        {
            return IsComplete() &&
                   (History.SceneRevision != sceneRevision ||
                    History.LightRevision != lightRevision);
        }

        void Clear()
        {
            Current.Clear();
            History.Clear();
            FrameNumber = 0u;
            bValid = false;
        }
    };

    struct RTGIFallbackInputs
    {
        bool bRTGIEnabled = false;
        bool bTLASAvailable = false;
        RTGIRayQueryCapability Capability;
        const RTGIResult* Result = nullptr;
        const RTGIHistoryResources* History = nullptr;
        uint64_t FrameNumber = 0u;
        uint64_t SceneRevision = 0u;
        uint64_t LightRevision = 0u;
        bool bDDGIAvailable = false;
        bool bIBLAvailable = false;
    };

    struct RTGIFallbackDecision
    {
        RTGIIndirectLightingSource Source = RTGIIndirectLightingSource::Raster;
        RTGIFallbackReason Reason = RTGIFallbackReason::ResourceUnavailable;
        bool bUsedFallback = true;
    };

    /**
     * @brief RTGIから既存間接光へ戻す優先順位を解決
     *
     * RTGI -> R4 DDGI -> 既存IBL -> 既定rasterの順で選びます。結果や履歴が不完全な
     * ときはRTGIを選ばず、呼出元の通常ライティングを継続します。
     */
    inline RTGIFallbackDecision ResolveRTGIIndirectLighting(
        const RTGIFallbackInputs& inputs)
    {
        RTGIFallbackDecision decision;
        if (inputs.bRTGIEnabled)
        {
            if (!inputs.Capability.IsUsable())
            {
                decision.Reason = RTGIFallbackReason::CapabilityUnavailable;
            }
            else if (!inputs.bTLASAvailable)
            {
                decision.Reason = RTGIFallbackReason::TLASUnavailable;
            }
            else if (inputs.Result == nullptr || !inputs.Result->IsComplete() ||
                     !inputs.Result->IsForFrame(inputs.FrameNumber,
                                                inputs.SceneRevision,
                                                inputs.LightRevision) ||
                     inputs.History == nullptr || !inputs.History->IsComplete() ||
                     !inputs.History->IsForFrame(inputs.FrameNumber,
                                                 inputs.SceneRevision,
                                                 inputs.LightRevision))
            {
                decision.Reason = RTGIFallbackReason::ResourceUnavailable;
            }
            else
            {
                decision.Source = RTGIIndirectLightingSource::RTGI;
                decision.Reason = RTGIFallbackReason::None;
                decision.bUsedFallback = false;
                return decision;
            }
        }
        else
        {
            decision.Reason = RTGIFallbackReason::Disabled;
        }

        if (inputs.bDDGIAvailable)
        {
            decision.Source = RTGIIndirectLightingSource::DDGI;
        }
        else if (inputs.bIBLAvailable)
        {
            decision.Source = RTGIIndirectLightingSource::IBL;
        }
        else
        {
            decision.Source = RTGIIndirectLightingSource::Raster;
        }
        return decision;
    }

    /**
     * @brief ViewRenderContextへ公開するRTGI resource状態
     */
    struct RTGIResourcePublication
    {
        RTGIRayQueryCapability Capability;
        RTGIResult Result;
        RTGIHistoryResources History;
        uint64_t FrameNumber = 0u;
        uint64_t SceneRevision = 0u;
        uint64_t LightRevision = 0u;
        bool bEnabled = false;
        bool bTLASAvailable = false;
        bool bPublished = false;

        void Configure(const RTGIRayQueryCapability& capability,
                       bool bEnabledValue,
                       bool bTLASAvailableValue,
                       uint64_t frameNumber,
                       uint64_t sceneRevision,
                       uint64_t lightRevision)
        {
            Capability = capability;
            bEnabled = bEnabledValue;
            bTLASAvailable = bTLASAvailableValue;
            FrameNumber = frameNumber;
            SceneRevision = sceneRevision;
            LightRevision = lightRevision;
            bPublished = false;
            Result.Clear();
            History.Clear();
        }

        void PublishResult(const RTGIResult& result,
                           const RTGIHistoryResources& history)
        {
            Result = result;
            History = history;
            bPublished = bEnabled && Capability.IsUsable() && bTLASAvailable &&
                         Result.IsComplete() &&
                         Result.IsForFrame(FrameNumber, SceneRevision, LightRevision) &&
                         History.IsForFrame(FrameNumber, SceneRevision, LightRevision);
        }

        RTGIFallbackDecision Resolve(bool bDDGIAvailable, bool bIBLAvailable) const
        {
            RTGIFallbackInputs inputs;
            inputs.bRTGIEnabled = bEnabled;
            inputs.bTLASAvailable = bTLASAvailable;
            inputs.Capability = Capability;
            inputs.Result = bPublished ? &Result : nullptr;
            inputs.History = bPublished ? &History : nullptr;
            inputs.FrameNumber = FrameNumber;
            inputs.SceneRevision = SceneRevision;
            inputs.LightRevision = LightRevision;
            inputs.bDDGIAvailable = bDDGIAvailable;
            inputs.bIBLAvailable = bIBLAvailable;
            return ResolveRTGIIndirectLighting(inputs);
        }

        void Clear()
        {
            Capability = RTGIRayQueryCapability{};
            Result.Clear();
            History.Clear();
            FrameNumber = 0u;
            SceneRevision = 0u;
            LightRevision = 0u;
            bEnabled = false;
            bTLASAvailable = false;
            bPublished = false;
        }
    };
} // namespace NorvesLib::Core::Rendering
