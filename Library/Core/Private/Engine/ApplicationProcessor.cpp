#include "Engine/ApplicationProcessor.h"
#include "Engine/FixedStepScheduler.h"
#include "Engine/Engine.h"
#include "Engine/NorvesEngine.h"
#include "Engine/ApplicationExitFramePolicy.h"
#include "Asset/AssetFileReader.h"
#include "Boot/BootConfig.h"
#include "Application/IApplicationHandler.h"
#include "Application/IApplication.h"
#include "Application/IWindow.h"
#include "Platform/PlatformApplicationFactory.h"
#include "Rendering/RenderWorld.h"
#include "Rendering/RenderingCoordinator.h"
#include "Rendering/SceneView.h"
#include "Rendering/IViewPass.h"
#include "Rendering/PathTracingPass.h"
#include "Rendering/PostProcessStack.h"
#include "Rendering/ToneMappingPass.h"
#include "Resource/FontAtlas.h"
#include "Module/ModuleRegistry.h"
#include "RHI/RHIDeviceFactory.h"
#include "Platform/PlatformInputDevices.h"
#include "Debug/Stats.h"
#include "Logging/LogMacros.h"
#include "Thread/JobSystem.h"
#include "Scripting/ScriptRuntime.h"
#include "FileStream/FileStream.h"
#include "stb_image_write.h"
#include <chrono>
#include <cmath>
#include <cstdlib>
#include <limits>

#ifdef _WIN32
#include <Windows.h>
#endif

using namespace NorvesLib::Core::Container;

namespace
{
    constexpr TCHAR kExitAfterFramesOption[] = TEXT("--exit-after-frames=");
    constexpr size_t kExitAfterFramesOptionLength = (sizeof(kExitAfterFramesOption) / sizeof(TCHAR)) - 1;
    constexpr TCHAR kRenderThreadOption[] = TEXT("--render-thread=");
    constexpr size_t kRenderThreadOptionLength = (sizeof(kRenderThreadOption) / sizeof(TCHAR)) - 1;
    constexpr TCHAR kEnableCanvasViewOption[] = TEXT("--enable-canvas-view");
    constexpr size_t kEnableCanvasViewOptionLength = (sizeof(kEnableCanvasViewOption) / sizeof(TCHAR)) - 1;
    constexpr TCHAR kDisableBoardInstanceBatchingOption[] = TEXT("--disable-board-instance-batching");
    constexpr size_t kDisableBoardInstanceBatchingOptionLength =
        (sizeof(kDisableBoardInstanceBatchingOption) / sizeof(TCHAR)) - 1;

    class ApplicationInitializeTransaction final
    {
    public:
        explicit ApplicationInitializeTransaction(NorvesLib::Core::Engine::ApplicationProcessor& processor)
            : m_Processor(processor)
        {
        }

        ~ApplicationInitializeTransaction()
        {
            if (!m_bCommitted)
            {
                try
                {
                    m_Processor.Shutdown();
                }
                catch (...)
                {
                    LOG_ERROR("ApplicationInitializeTransaction cleanup threw an exception");
                    std::abort();
                }
            }
        }

        void Commit()
        {
            m_bCommitted = true;
        }

    private:
        NorvesLib::Core::Engine::ApplicationProcessor& m_Processor;
        bool m_bCommitted = false;
    };

    struct ApplicationLifecycleState
    {
        NorvesLib::Core::Engine::ApplicationProcessor* Processor = nullptr;
        bool bJobSystem = false;
        bool bEngine = false;
        bool bSkeletalSession = false;
        bool bHandler = false;
        bool bPlatform = false;
        bool bWindow = false;
        bool bDevice = false;
        bool bRenderWorld = false;
        bool bWorld = false;
        bool bScriptRuntime = false;
        bool bHandlerInitialized = false;
        bool bPreShutdownPending = false;
        bool bShutdownPending = false;
        bool bGameMode = false;
        bool bModules = false;
        bool bRunning = false;
    };

    ApplicationLifecycleState GApplicationLifecycleState;

    uint64_t ParsePositiveFrameCount(const TCHAR *pValueText, bool &bValid)
    {
        bValid = false;

        if (!pValueText || pValueText[0] == TEXT('\0'))
        {
            return 0;
        }

        uint64_t value = 0;
        for (const TCHAR *pChar = pValueText; *pChar != TEXT('\0'); ++pChar)
        {
            if (*pChar < TEXT('0') || *pChar > TEXT('9'))
            {
                return 0;
            }

            const uint64_t digit = static_cast<uint64_t>(*pChar - TEXT('0'));
            if (value > (std::numeric_limits<uint64_t>::max() - digit) / 10)
            {
                return 0;
            }

            value = value * 10 + digit;
        }

        if (value == 0)
        {
            return 0;
        }

        bValid = true;
        return value;
    }

    bool TryParseExitAfterFramesOption(const TCHAR *pText, uint64_t &outFrameCount, bool &bMatched)
    {
        outFrameCount = 0;
        bMatched = false;

        if (!pText)
        {
            return false;
        }

        // プレフィックスが一致するか確認
        for (size_t i = 0; i < kExitAfterFramesOptionLength; ++i)
        {
            if (pText[i] == TEXT('\0') || pText[i] != kExitAfterFramesOption[i])
            {
                return false;
            }
        }

        bMatched = true;
        bool bValid = false;
        const uint64_t parsedFrameCount = ParsePositiveFrameCount(
            pText + kExitAfterFramesOptionLength,
            bValid);
        if (!bValid)
        {
            return false;
        }

        outFrameCount = parsedFrameCount;
        return true;
    }

    bool TryParseRenderThreadOption(const TCHAR* pText, bool& bOutEnableMultiThreadedRendering, bool& bMatched)
    {
        bMatched = false;

        if (!pText)
        {
            return false;
        }

        for (size_t i = 0; i < kRenderThreadOptionLength; ++i)
        {
            if (pText[i] == TEXT('\0') || pText[i] != kRenderThreadOption[i])
            {
                return false;
            }
        }

        bMatched = true;
        const TCHAR* pValueText = pText + kRenderThreadOptionLength;
        if (pValueText[0] == TEXT('s') &&
            pValueText[1] == TEXT('t') &&
            pValueText[2] == TEXT('\0'))
        {
            bOutEnableMultiThreadedRendering = false;
            return true;
        }

        if (pValueText[0] == TEXT('m') &&
            pValueText[1] == TEXT('t') &&
            pValueText[2] == TEXT('\0'))
        {
            bOutEnableMultiThreadedRendering = true;
            return true;
        }

        return false;
    }

    bool IsEnableCanvasViewOption(const TCHAR* pText)
    {
        if (!pText)
        {
            return false;
        }

        for (size_t i = 0; i < kEnableCanvasViewOptionLength; ++i)
        {
            if (pText[i] == TEXT('\0') || pText[i] != kEnableCanvasViewOption[i])
            {
                return false;
            }
        }

        return pText[kEnableCanvasViewOptionLength] == TEXT('\0');
    }

    // RenderWorld の pre-device-teardown フックから呼ばれる。RenderThread 停止済み・
    // device 生存中の地点でモジュールを逆順 Shutdown→Uninstall する(MT use-after-free 回避)。
    // 関数ポインタ署名(void(*)(void*))に合わせた自由関数。context は未使用。
    void ShutdownModulesPreDeviceTeardown(void * /*context*/)
    {
        if (NorvesLib::Core::Engine::GEngine)
        {
            NorvesLib::Core::Module::GetModuleRegistry().ShutdownAll(*NorvesLib::Core::Engine::GEngine);
        }
    }

    // --renderer=raster|path-tracing。一致したがbMatchedだけ真で値が不正なら偽を返す。
    bool TryParseRendererOption(const String& argument,
                                NorvesLib::Core::Rendering::RenderingMainViewRenderer& outRenderer,
                                bool& bMatched)
    {
        const String prefix = TEXT("--renderer=");
        bMatched = argument.size() >= prefix.size() &&
                   argument.substr(0, prefix.size()) == prefix;
        if (!bMatched)
        {
            return false;
        }
        const String value = argument.substr(prefix.size());
        if (value == TEXT("raster"))
        {
            outRenderer = NorvesLib::Core::Rendering::RenderingMainViewRenderer::Raster;
            return true;
        }
        if (value == TEXT("path-tracing"))
        {
            outRenderer = NorvesLib::Core::Rendering::RenderingMainViewRenderer::PathTracing;
            return true;
        }
        return false;
    }

    // --path-tracing-samples-per-frame=N（1〜1024）
    bool TryParsePathTracingSamplesPerFrameOption(const String& argument, uint32_t& outSamples,
                                                  bool& bMatched)
    {
        const String prefix = TEXT("--path-tracing-samples-per-frame=");
        bMatched = argument.size() >= prefix.size() &&
                   argument.substr(0, prefix.size()) == prefix;
        if (!bMatched)
        {
            return false;
        }
        const String value = argument.substr(prefix.size());
        if (value.empty() || value.size() > 4u)
        {
            return false;
        }
        uint32_t parsed = 0u;
        for (const auto character : value)
        {
            if (character < TEXT('0') || character > TEXT('9'))
            {
                return false;
            }
            parsed = parsed * 10u + static_cast<uint32_t>(character - TEXT('0'));
        }
        if (parsed == 0u || parsed > 1024u)
        {
            return false;
        }
        outSamples = parsed;
        return true;
    }

    // --path-tracing-transport=full|direct|single-diffuse-bounce|two-diffuse-bounces
    bool TryParsePathTracingTransportOption(
        const String& argument,
        NorvesLib::Core::Rendering::PathTracingTransportScope& outScope,
        bool& bMatched)
    {
        const String prefix = TEXT("--path-tracing-transport=");
        bMatched = argument.size() >= prefix.size() &&
                   argument.substr(0, prefix.size()) == prefix;
        if (!bMatched)
        {
            return false;
        }
        const String value = argument.substr(prefix.size());
        if (value == TEXT("full"))
        {
            outScope = NorvesLib::Core::Rendering::PathTracingTransportScope::Full;
            return true;
        }
        if (value == TEXT("direct"))
        {
            outScope = NorvesLib::Core::Rendering::PathTracingTransportScope::DirectOnly;
            return true;
        }
        if (value == TEXT("single-diffuse-bounce"))
        {
            outScope = NorvesLib::Core::Rendering::PathTracingTransportScope::SingleDiffuseBounce;
            return true;
        }
        if (value == TEXT("two-diffuse-bounces"))
        {
            outScope = NorvesLib::Core::Rendering::PathTracingTransportScope::TwoDiffuseBounces;
            return true;
        }
        return false;
    }

    // --path-tracing-sample-batch=N（0〜255）
    bool TryParsePathTracingSampleBatchOption(const String& argument, uint32_t& outBatch, bool& bMatched)
    {
        const String prefix = TEXT("--path-tracing-sample-batch=");
        bMatched = argument.size() >= prefix.size() &&
                   argument.substr(0, prefix.size()) == prefix;
        if (!bMatched)
        {
            return false;
        }
        const String value = argument.substr(prefix.size());
        if (value.empty() || value.size() > 3u)
        {
            return false;
        }
        uint32_t parsed = 0u;
        for (const auto character : value)
        {
            if (character < TEXT('0') || character > TEXT('9'))
            {
                return false;
            }
            parsed = parsed * 10u + static_cast<uint32_t>(character - TEXT('0'));
        }
        if (parsed > 255u)
        {
            return false;
        }
        outBatch = parsed;
        return true;
    }

    // --path-tracing-pixel-sampling=box|center
    bool TryParsePathTracingPixelSamplingOption(
        const String& argument,
        NorvesLib::Core::Rendering::PathTracingPixelSampling& outSampling,
        bool& bMatched)
    {
        const String prefix = TEXT("--path-tracing-pixel-sampling=");
        bMatched = argument.size() >= prefix.size() &&
                   argument.substr(0, prefix.size()) == prefix;
        if (!bMatched)
        {
            return false;
        }
        const String value = argument.substr(prefix.size());
        if (value == TEXT("box"))
        {
            outSampling = NorvesLib::Core::Rendering::PathTracingPixelSampling::Box;
            return true;
        }
        if (value == TEXT("center"))
        {
            outSampling = NorvesLib::Core::Rendering::PathTracingPixelSampling::Center;
            return true;
        }
        return false;
    }

    // --path-tracing-debug-output=none|albedo|shading-normal|metallic-roughness|hit-distance|sun-visibility
    bool TryParsePathTracingDebugOutputOption(
        const String& argument,
        NorvesLib::Core::Rendering::PathTracingDebugOutput& outOutput,
        bool& bMatched)
    {
        const String prefix = TEXT("--path-tracing-debug-output=");
        bMatched = argument.size() >= prefix.size() &&
                   argument.substr(0, prefix.size()) == prefix;
        if (!bMatched)
        {
            return false;
        }
        using NorvesLib::Core::Rendering::PathTracingDebugOutput;
        const String value = argument.substr(prefix.size());
        const struct
        {
            const TCHAR* Name;
            PathTracingDebugOutput Output;
        } choices[] = {{TEXT("none"), PathTracingDebugOutput::None},
                       {TEXT("albedo"), PathTracingDebugOutput::Albedo},
                       {TEXT("shading-normal"), PathTracingDebugOutput::ShadingNormal},
                       {TEXT("metallic-roughness"), PathTracingDebugOutput::MetallicRoughness},
                       {TEXT("hit-distance"), PathTracingDebugOutput::HitDistance},
                       {TEXT("sun-visibility"), PathTracingDebugOutput::SunVisibility}};
        for (const auto& choice : choices)
        {
            if (value == choice.Name)
            {
                outOutput = choice.Output;
                return true;
            }
        }
        return false;
    }

    // --raster-direct-brdf=neural|analytic
    bool TryParseRasterDirectBrdfOption(
        const String& argument,
        NorvesLib::Core::Rendering::RasterDirectBrdf& outBrdf,
        bool& bMatched)
    {
        const String prefix = TEXT("--raster-direct-brdf=");
        bMatched = argument.size() >= prefix.size() &&
                   argument.substr(0, prefix.size()) == prefix;
        if (!bMatched)
        {
            return false;
        }
        const String value = argument.substr(prefix.size());
        if (value == TEXT("neural"))
        {
            outBrdf = NorvesLib::Core::Rendering::RasterDirectBrdf::Neural;
            return true;
        }
        if (value == TEXT("analytic"))
        {
            outBrdf = NorvesLib::Core::Rendering::RasterDirectBrdf::Analytic;
            return true;
        }
        return false;
    }

    // --visibility-buffer=off|on|debug
    bool TryParseVisibilityBufferOption(
        const String& argument,
        NorvesLib::Core::Rendering::VisibilityBufferMode& outMode,
        bool& bMatched)
    {
        const String prefix = TEXT("--visibility-buffer=");
        bMatched = argument.size() >= prefix.size() &&
                   argument.substr(0, prefix.size()) == prefix;
        if (!bMatched)
        {
            return false;
        }
        const String value = argument.substr(prefix.size());
        if (value == TEXT("off"))
        {
            outMode = NorvesLib::Core::Rendering::VisibilityBufferMode::Off;
            return true;
        }
        if (value == TEXT("on"))
        {
            outMode = NorvesLib::Core::Rendering::VisibilityBufferMode::On;
            return true;
        }
        if (value == TEXT("debug"))
        {
            outMode = NorvesLib::Core::Rendering::VisibilityBufferMode::Debug;
            return true;
        }
        return false;
    }

    // --sw-raster=off|on
    bool TryParseSwRasterOption(
        const String& argument,
        NorvesLib::Core::Rendering::SwRasterMode& outMode,
        bool& bMatched)
    {
        const String prefix = TEXT("--sw-raster=");
        bMatched = argument.size() >= prefix.size() &&
                   argument.substr(0, prefix.size()) == prefix;
        if (!bMatched)
        {
            return false;
        }
        const String value = argument.substr(prefix.size());
        if (value == TEXT("off"))
        {
            outMode = NorvesLib::Core::Rendering::SwRasterMode::Off;
            return true;
        }
        if (value == TEXT("on"))
        {
            outMode = NorvesLib::Core::Rendering::SwRasterMode::On;
            return true;
        }
        return false;
    }

    // --shadow-method=csm|vsm
    bool TryParseShadowMethodOption(
        const String& argument,
        NorvesLib::Core::Rendering::ShadowMethod& outMethod,
        bool& bMatched)
    {
        const String prefix = TEXT("--shadow-method=");
        bMatched = argument.size() >= prefix.size() &&
                   argument.substr(0, prefix.size()) == prefix;
        if (!bMatched)
        {
            return false;
        }
        const String value = argument.substr(prefix.size());
        if (value == TEXT("csm"))
        {
            outMethod = NorvesLib::Core::Rendering::ShadowMethod::Csm;
            return true;
        }
        if (value == TEXT("vsm"))
        {
            outMethod = NorvesLib::Core::Rendering::ShadowMethod::Vsm;
            return true;
        }
        return false;
    }

    // --vsm-pool-pages=<n>: VSM の物理ページのプールのページの数（1 以上の整数。装置の上限へは VirtualShadowMapPass が締める）
    bool TryParseVsmPoolPagesOption(const String& argument, uint32_t& outPages, bool& bMatched)
    {
        const String prefix = TEXT("--vsm-pool-pages=");
        bMatched = argument.size() >= prefix.size() &&
                   argument.substr(0, prefix.size()) == prefix;
        if (!bMatched)
        {
            return false;
        }
        const String value = argument.substr(prefix.size());
        if (value.empty() || value.size() > 7u)
        {
            return false;
        }
        uint32_t parsed = 0u;
        for (const auto character : value)
        {
            if (character < TEXT('0') || character > TEXT('9'))
            {
                return false;
            }
            parsed = parsed * 10u + static_cast<uint32_t>(character - TEXT('0'));
        }
        if (parsed == 0u)
        {
            return false;
        }
        outPages = parsed;
        return true;
    }

    // --tone-map=aces|aces20-lut
    bool TryParseToneMapOption(
        const String& argument,
        NorvesLib::Core::Rendering::ToneMappingOperator& outOperator,
        bool& bMatched)
    {
        const String prefix = TEXT("--tone-map=");
        bMatched = argument.size() >= prefix.size() &&
                   argument.substr(0, prefix.size()) == prefix;
        if (!bMatched)
        {
            return false;
        }
        const String value = argument.substr(prefix.size());
        if (value == TEXT("aces"))
        {
            outOperator = NorvesLib::Core::Rendering::ToneMappingOperator::ACES;
            return true;
        }
        if (value == TEXT("aces20-lut"))
        {
            outOperator = NorvesLib::Core::Rendering::ToneMappingOperator::Aces20Lut;
            return true;
        }
        return false;
    }

    // --capture-png=<path> を読む。値が空なら不正。
    bool TryParseCapturePngOption(const String& argument, String& outPath, bool& bMatched)
    {
        const String prefix = TEXT("--capture-png=");
        bMatched = argument.size() >= prefix.size() &&
                   argument.substr(0, prefix.size()) == prefix;
        if (!bMatched)
        {
            return false;
        }
        const String value = argument.substr(prefix.size());
        if (value.empty())
        {
            return false;
        }
        outPath = value;
        return true;
    }

    // --capture-deterministic: 同じコードを2回撮ると一致する撮影（値を取らない。--capture-png と併せて使う）。
    bool IsCaptureDeterministicOption(const String& argument)
    {
        return argument == String(TEXT("--capture-deterministic"));
    }

    // 撮影の終了条件（アセットが落ち着いてから描いたフレーム数）の既定値。時間方向に積む効果の収束を待つ。
    constexpr uint64_t kCapturePngDefaultSettledRenderedFrames = 60;
    // 撮影を要求してから結果が戻るまで待つ描画フレーム数の上限。超えたら失敗として終了する。
    constexpr uint64_t kCapturePngResultTimeoutRenderedFrames = 120;

    void AppendCapturePngBytes(void* pContext, void* pData, int byteCount)
    {
        auto* pBytes = static_cast<VariableArray<uint8_t>*>(pContext);
        if (!pBytes || !pData || byteCount <= 0)
        {
            return;
        }
        const uint8_t* pSource = static_cast<const uint8_t*>(pData);
        pBytes->insert(pBytes->end(), pSource, pSource + byteCount);
    }

    // BackBuffer の取得結果（表示用に符号化済みの8bit）を不透明のRGBA8 PNGとして保存する。
    bool SaveCapturedFramePng(const NorvesLib::Core::Rendering::CapturedFrame& frame, const String& path)
    {
        using NorvesLib::RHI::Format;
        if (!frame.IsSuccess() || frame.Width == 0 || frame.Height == 0 || frame.BytesPerPixel != 4)
        {
            return false;
        }
        bool bBgra = false;
        switch (frame.Format)
        {
        case Format::B8G8R8A8_UNORM:
        case Format::B8G8R8A8_SRGB:
            bBgra = true;
            break;
        case Format::R8G8B8A8_UNORM:
        case Format::R8G8B8A8_SRGB:
            break;
        default:
            return false;
        }
        const uint64_t tightRowPitch = static_cast<uint64_t>(frame.Width) * 4u;
        if (frame.RowPitchBytes < tightRowPitch ||
            frame.Pixels.size() < static_cast<size_t>(frame.RowPitchBytes) * frame.Height)
        {
            return false;
        }

        VariableArray<uint8_t> rgba(static_cast<size_t>(tightRowPitch) * frame.Height);
        for (uint32_t y = 0; y < frame.Height; ++y)
        {
            const uint8_t* pSourceRow = frame.Pixels.data() + static_cast<size_t>(frame.RowPitchBytes) * y;
            uint8_t* pDestRow = rgba.data() + static_cast<size_t>(tightRowPitch) * y;
            for (uint32_t x = 0; x < frame.Width; ++x)
            {
                const uint8_t* pSource = pSourceRow + x * 4u;
                uint8_t* pDest = pDestRow + x * 4u;
                pDest[0] = bBgra ? pSource[2] : pSource[0];
                pDest[1] = pSource[1];
                pDest[2] = bBgra ? pSource[0] : pSource[2];
                // スワップチェーンのアルファは表示に使われないため不透明にそろえる。
                pDest[3] = 255u;
            }
        }

        VariableArray<uint8_t> png;
        const int writeResult = stbi_write_png_to_func(
            AppendCapturePngBytes,
            &png,
            static_cast<int>(frame.Width),
            static_cast<int>(frame.Height),
            4,
            rgba.data(),
            static_cast<int>(tightRowPitch));
        if (writeResult == 0 || png.empty())
        {
            return false;
        }

        NorvesLib::FileStream::FileStreamUniquePtr stream = NorvesLib::FileStream::FileStream::CreateUnique(
            path, NorvesLib::FileStream::FileMode::Write, NorvesLib::FileStream::FileAccess::Write,
            NorvesLib::FileStream::FileShare::None);
        if (!stream)
        {
            return false;
        }
        if (stream->Write(png.data(), png.size()) != png.size())
        {
            return false;
        }
        stream->Flush();
        return true;
    }

    // 非負の10進数（例 2.8）か分数（例 1/24）を読む。分母は正でなければならない。
    bool TryParseNonNegativeRatio(const String& text, float& outValue)
    {
        double parts[2] = {0.0, 1.0};
        uint32_t partIndex = 0u;
        bool bDigits = false;
        bool bDecimal = false;
        double decimalScale = 1.0;
        for (const auto character : text)
        {
            if (character >= TEXT('0') && character <= TEXT('9'))
            {
                const double digit = static_cast<double>(character - TEXT('0'));
                if (bDecimal)
                {
                    decimalScale *= 0.1;
                    parts[partIndex] += decimalScale * digit;
                }
                else
                {
                    parts[partIndex] = parts[partIndex] * 10.0 + digit;
                }
                bDigits = true;
            }
            else if (character == TEXT('.') && !bDecimal && bDigits)
            {
                bDecimal = true;
            }
            else if (character == TEXT('/') && partIndex == 0u && bDigits)
            {
                partIndex = 1u;
                parts[1] = 0.0;
                bDigits = false;
                bDecimal = false;
                decimalScale = 1.0;
            }
            else
            {
                return false;
            }
        }
        if (!bDigits || !(parts[1] > 0.0))
        {
            return false;
        }
        const double value = parts[0] / parts[1];
        if (!std::isfinite(value) || value > static_cast<double>(std::numeric_limits<float>::max()))
        {
            return false;
        }
        outValue = static_cast<float>(value);
        return true;
    }

    // --film-grain=強さ（sRGBの符号化値での標準偏差、0〜1。0でオフ）、--film-grain-seed=N（32 bit）
    bool TryParseFilmGrainOption(const String& argument, float& outStrength, uint32_t& outSeed, bool& bMatched)
    {
        const String seedPrefix = TEXT("--film-grain-seed=");
        if (argument.size() >= seedPrefix.size() && argument.substr(0, seedPrefix.size()) == seedPrefix)
        {
            bMatched = true;
            const String value = argument.substr(seedPrefix.size());
            if (value.empty() || value.size() > 10u)
            {
                return false;
            }
            uint64_t parsed = 0u;
            for (const auto character : value)
            {
                if (character < TEXT('0') || character > TEXT('9'))
                {
                    return false;
                }
                parsed = parsed * 10u + static_cast<uint64_t>(character - TEXT('0'));
            }
            if (parsed > UINT32_MAX)
            {
                return false;
            }
            outSeed = static_cast<uint32_t>(parsed);
            return true;
        }
        const String strengthPrefix = TEXT("--film-grain=");
        bMatched = argument.size() >= strengthPrefix.size() &&
                   argument.substr(0, strengthPrefix.size()) == strengthPrefix;
        if (!bMatched)
        {
            return false;
        }
        float strength = 0.0f;
        if (!TryParseNonNegativeRatio(argument.substr(strengthPrefix.size()), strength) || strength > 1.0f)
        {
            return false;
        }
        outStrength = strength;
        return true;
    }

    // --sw-raster-max-px=<正の数>（ソフトウェアラスタへ振り分けるクラスタの画面上の半径（画素）のしきい値）
    bool TryParseSwRasterMaxPixelsOption(const String& argument, float& outMaxPixels, bool& bMatched)
    {
        const String prefix = TEXT("--sw-raster-max-px=");
        bMatched = argument.size() >= prefix.size() &&
                   argument.substr(0, prefix.size()) == prefix;
        if (!bMatched)
        {
            return false;
        }
        const String value = argument.substr(prefix.size());
        float parsed = 0.0f;
        if (!TryParseNonNegativeRatio(value, parsed) || !(parsed > 0.0f))
        {
            return false;
        }
        outMaxPixels = parsed;
        return true;
    }

    // 連番の1フレームを描くPTの経路の引数。どれかを指定すると連番の経路を有効にする。
    // --path-tracing-frame-duration=秒（正）、--path-tracing-shutter=秒（0以上）、
    // --path-tracing-aperture=f値（正）、--path-tracing-focus-distance=m（0はピンホール）
    bool TryParsePathTracingSequenceOption(
        const String& argument,
        NorvesLib::Core::Rendering::PathTracingSequenceFrameSettings& settings,
        bool& bMatched)
    {
        struct Choice
        {
            const TCHAR* Prefix;
            float NorvesLib::Core::Rendering::PathTracingSequenceFrameSettings::*Field;
            bool bAllowZero;
        };
        using NorvesLib::Core::Rendering::PathTracingSequenceFrameSettings;
        const Choice choices[] = {
            {TEXT("--path-tracing-frame-duration="), &PathTracingSequenceFrameSettings::FrameDuration, false},
            {TEXT("--path-tracing-shutter="), &PathTracingSequenceFrameSettings::ShutterDuration, true},
            {TEXT("--path-tracing-aperture="), &PathTracingSequenceFrameSettings::Aperture, false},
            {TEXT("--path-tracing-focus-distance="), &PathTracingSequenceFrameSettings::FocusDistance, true}};
        bMatched = false;
        for (const Choice& choice : choices)
        {
            const String prefix = choice.Prefix;
            if (argument.size() < prefix.size() || argument.substr(0, prefix.size()) != prefix)
            {
                continue;
            }
            bMatched = true;
            float value = 0.0f;
            if (!TryParseNonNegativeRatio(argument.substr(prefix.size()), value) ||
                (!choice.bAllowZero && value <= 0.0f))
            {
                return false;
            }
            settings.*(choice.Field) = value;
            settings.bEnabled = true;
            return true;
        }
        return false;
    }

    // メインSceneViewのPathTracingPassへ連番の経路を設定する。
    // 初期化中（最初のFramePacketがRenderThreadへ渡る前）にだけ呼ぶ。
    bool ApplyMainViewPathTracingSequenceFrame(
        NorvesLib::Core::Rendering::SceneView* sceneView,
        const NorvesLib::Core::Rendering::PathTracingSequenceFrameSettings& settings)
    {
        if (!sceneView)
        {
            return false;
        }
        NorvesLib::Core::Rendering::IViewPass* pass = sceneView->FindPass("PathTracingPass");
        if (!pass)
        {
            return false;
        }
        // GetName() が "PathTracingPass" を返すのは PathTracingPass だけ
        static_cast<NorvesLib::Core::Rendering::PathTracingPass*>(pass)->SetSequenceFrame(settings);
        return true;
    }

    // メインSceneViewのToneMappingPass。無ければnullptr。
    NorvesLib::Core::Rendering::ToneMappingPass* FindMainViewToneMappingPass(
        NorvesLib::Core::Rendering::SceneView* sceneView)
    {
        if (!sceneView)
        {
            return nullptr;
        }
        NorvesLib::Core::Rendering::PostProcessStack* postProcessStack = sceneView->GetPostProcessStack();
        if (!postProcessStack)
        {
            return nullptr;
        }
        NorvesLib::Core::Rendering::IViewPass* pass = postProcessStack->GetPass("ToneMappingPass");
        // GetName() が "ToneMappingPass" を返すのは ToneMappingPass だけ
        return pass ? static_cast<NorvesLib::Core::Rendering::ToneMappingPass*>(pass) : nullptr;
    }

    // メインSceneViewのToneMappingPassへ演算子を設定する。
    // 初期化中（最初のFramePacketがRenderThreadへ渡る前）にだけ呼ぶ。
    bool ApplyMainViewToneMapOperator(NorvesLib::Core::Rendering::SceneView* sceneView,
                                      NorvesLib::Core::Rendering::ToneMappingOperator toneMapOperator)
    {
        NorvesLib::Core::Rendering::ToneMappingPass* pass = FindMainViewToneMappingPass(sceneView);
        if (!pass)
        {
            return false;
        }
        pass->SetOperator(toneMapOperator);
        return true;
    }

    // メインSceneViewのToneMappingPassへフィルムグレインを設定する。初期化中にだけ呼ぶ。
    bool ApplyMainViewFilmGrain(NorvesLib::Core::Rendering::SceneView* sceneView, float strength, uint32_t seed)
    {
        NorvesLib::Core::Rendering::ToneMappingPass* pass = FindMainViewToneMappingPass(sceneView);
        if (!pass)
        {
            return false;
        }
        pass->SetFilmGrain(strength, seed);
        return true;
    }

    bool IsDisableBoardInstanceBatchingOption(const TCHAR* pText)
    {
        if (!pText)
        {
            return false;
        }

        for (size_t i = 0; i < kDisableBoardInstanceBatchingOptionLength; ++i)
        {
            if (pText[i] == TEXT('\0') || pText[i] != kDisableBoardInstanceBatchingOption[i])
            {
                return false;
            }
        }

        return pText[kDisableBoardInstanceBatchingOptionLength] == TEXT('\0');
    }
} // namespace

namespace NorvesLib::Core::Engine
{

    // シングルトンインスタンス
    static ApplicationProcessor *s_Instance = nullptr;

    ApplicationProcessor::ApplicationProcessor()
        : m_FixedStepScheduler(Container::MakeUnique<FixedStepScheduler>())
    {
    }

    ApplicationProcessor::~ApplicationProcessor() { DisconnectInputWindow(); }

    ApplicationProcessor &ApplicationProcessor::GetInstance()
    {
        if (!s_Instance)
        {
            s_Instance = new ApplicationProcessor();
        }
        return *s_Instance;
    }

    void ApplicationProcessor::DestroyInstance()
    {
        ApplicationProcessor* pInstance = s_Instance;
        s_Instance = nullptr;
        delete pInstance;
    }

    bool ApplicationProcessor::Initialize(const Boot::BootConfig &config) try
    {
        LOG_INFO("ApplicationProcessor::Initialize() - Starting initialization");
        // active sessionを初期化receiptの上書きで失わない。
        if (NorvesLib::Core::GEngine.GetSkeletalAssetSession().IsActive())
        {
            NORVES_LOG_ERROR("SkeletalAssets", "実行中の骨格sessionがあるため再初期化を拒否します");
            return false;
        }
        GApplicationLifecycleState = {};
        GApplicationLifecycleState.Processor = this;
        ApplicationInitializeTransaction transaction(*this);

        // JobSystemを初期化（ワーカースレッドの起動）
        GApplicationLifecycleState.bJobSystem = true;
        Thread::JobSystem::Get().Initialize();
        LOG_INFO("JobSystem initialized");

        // GEngineを作成
        if (!CreateEngine())
        {
            LOG_ERROR("Failed to create engine");
            return false;
        }
        GApplicationLifecycleState.bEngine = true;
        GApplicationLifecycleState.bSkeletalSession = true;
        if (NorvesLib::Core::GEngine.GetSkeletalAssetSession().Begin(
                NorvesLib::Core::GEngine.GetResourceRegistry(), Thread::JobSystem::Get(),
                Thread::Thread::GetCurrentThreadId()) != SkeletalRuntimeStatus::Success)
        {
            NORVES_LOG_ERROR("SkeletalAssets", "owner骨格sessionの開始に失敗しました");
            return false;
        }

        // ターゲットフレームレートを設定
        if (config.TargetFrameRate > 0.0f)
        {
            m_TargetFrameTime = 1.0f / config.TargetFrameRate;
        }

        // ApplicationHandlerを作成
        if (config.CreateHandler)
        {
            auto handler = config.CreateHandler();
            if (!handler)
            {
                LOG_ERROR("Failed to create application handler");
                return false;
            }
            GEngine->SetApplicationHandler(handler);
            GApplicationLifecycleState.bHandler = true;
            LOG_INFO("Application handler created successfully");
        }
        else
        {
            LOG_ERROR("No handler creator specified in BootConfig");
            return false;
        }

        // コマンドライン引数を config.Arguments から取得してパース
        m_ExitAfterFrames = 0;
        m_ExitAfterRenderedFrames = 0;
        m_AssetSettleRenderedBaseline = 0;
        m_bWaitForAssetSettle = false;
        m_bObservedPendingAssets = false;
        m_bAssetSettleBaselineLatched = false;
        m_CapturePngPath = {};
        m_CaptureRequestedRenderedFrame = 0;
        m_bCaptureRequested = false;
        Detail::ExitFrameOptionsAccumulator exitFrameOptions{};
        bool bEnableMultiThreadedRendering = config.bEnableMultiThreadedRendering;
        bool bCaptureDeterministic = false;
        bool bEnableCanvasView = false;
        bool bBoardInstanceBatchingEnabled = true;
        Rendering::RenderingMainViewRenderer mainViewRenderer = Rendering::RenderingMainViewRenderer::Raster;
        uint32_t pathTracingSamplesPerFrame = 1u;
        Rendering::PathTracingTransportScope pathTracingTransport =
            Rendering::PathTracingTransportScope::Full;
        Rendering::PathTracingPixelSampling pathTracingPixelSampling =
            Rendering::PathTracingPixelSampling::Box;
        uint32_t pathTracingSampleBatch = 0u;
        Rendering::PathTracingDebugOutput pathTracingDebugOutput =
            Rendering::PathTracingDebugOutput::None;
        Rendering::RasterDirectBrdf rasterDirectBrdf = Rendering::RasterDirectBrdf::Analytic;
        Rendering::VisibilityBufferMode visibilityBufferMode = Rendering::VisibilityBufferMode::On;
        Rendering::SwRasterMode swRasterMode = Rendering::SwRasterMode::On;
        float swRasterMaxPixels = Rendering::DefaultSwRasterMaxPixels;
        bool bShadowProbe = false;
        // 既定は BootConfig の値（Game は VSM、検証アプリは CSM）。--shadow-method で上書きする
        Rendering::ShadowMethod shadowMethod = config.DefaultSunShadowMethod;
        bool bInvalidShadowMethod = false;
        uint32_t vsmPoolPages = 0u;
        bool bInvalidVsmPoolPages = false;
        Rendering::ToneMappingOperator toneMapOperator = Rendering::ToneMappingOperator::ACES;
        bool bToneMapOperatorRequested = false;
        float filmGrainStrength = 0.0f;
        uint32_t filmGrainSeed = 0u;
        bool bFilmGrainRequested = false;
        Rendering::PathTracingSequenceFrameSettings pathTracingSequenceFrame;
        const VariableArray<String> &args = config.Arguments;
        for (size_t i = 0; i < args.size(); ++i)
        {
            bool bParsedMultiThreadedRendering = false;
            bool bMatchedRenderThread = false;
            const Detail::ExitFrameArgumentResult exitFrameArgument =
                Detail::AccumulateExitFrameArgument(exitFrameOptions, args[i].c_str());
            if (exitFrameArgument.Kind == Detail::ExitFrameArgumentKind::LegacyValid)
            {
                LOG_INFO("ApplicationProcessor runtime option exit_after_frames=%llu",
                         static_cast<unsigned long long>(exitFrameArgument.Value));
            }
            else if (exitFrameArgument.Kind == Detail::ExitFrameArgumentKind::LegacyInvalid)
            {
                LOG_WARNING("ApplicationProcessor runtime option --exit-after-frames ignored: value must be a positive integer");
            }
            else if (exitFrameArgument.Kind == Detail::ExitFrameArgumentKind::RenderedValid)
            {
                LOG_INFO("ApplicationProcessor runtime option exit_after_rendered_frames=%llu",
                         static_cast<unsigned long long>(exitFrameArgument.Value));
            }
            else if (exitFrameArgument.Kind == Detail::ExitFrameArgumentKind::RenderedInvalid)
            {
                LOG_WARNING("ApplicationProcessor runtime option --exit-after-rendered-frames ignored: value must be a positive integer");
            }

            if (TryParseRenderThreadOption(args[i].c_str(), bParsedMultiThreadedRendering, bMatchedRenderThread))
            {
                bEnableMultiThreadedRendering = bParsedMultiThreadedRendering;
                LOG_INFO("ApplicationProcessor runtime option render_thread=%s",
                         bEnableMultiThreadedRendering ? "mt" : "st");
            }
            else if (bMatchedRenderThread)
            {
                LOG_WARNING("ApplicationProcessor runtime option --render-thread ignored: value must be 'st' or 'mt'; current setting remains %s",
                            bEnableMultiThreadedRendering ? "mt" : "st");
            }

            if (IsEnableCanvasViewOption(args[i].c_str()))
            {
                bEnableCanvasView = true;
                LOG_INFO("ApplicationProcessor runtime option enable_canvas_view=true");
            }

            if (IsDisableBoardInstanceBatchingOption(args[i].c_str()))
            {
                bBoardInstanceBatchingEnabled = false;
                LOG_INFO("ApplicationProcessor runtime option board_instance_batching=false");
            }

            bool bMatchedRenderer = false;
            if (TryParseRendererOption(args[i], mainViewRenderer, bMatchedRenderer))
            {
                LOG_INFO("ApplicationProcessor runtime option renderer=%s",
                         mainViewRenderer == Rendering::RenderingMainViewRenderer::PathTracing
                             ? "path-tracing" : "raster");
            }
            else if (bMatchedRenderer)
            {
                LOG_WARNING("ApplicationProcessor runtime option --renderer ignored: value must be 'raster' or 'path-tracing'");
            }

            bool bMatchedSamples = false;
            if (TryParsePathTracingSamplesPerFrameOption(args[i], pathTracingSamplesPerFrame,
                                                         bMatchedSamples))
            {
                LOG_INFO("ApplicationProcessor runtime option path_tracing_samples_per_frame=%u",
                         pathTracingSamplesPerFrame);
            }
            else if (bMatchedSamples)
            {
                LOG_WARNING("ApplicationProcessor runtime option --path-tracing-samples-per-frame ignored: value must be 1-1024");
            }

            bool bMatchedTransport = false;
            if (TryParsePathTracingTransportOption(args[i], pathTracingTransport, bMatchedTransport))
            {
                LOG_INFO("ApplicationProcessor runtime option path_tracing_transport=%u",
                         static_cast<unsigned int>(pathTracingTransport));
            }
            else if (bMatchedTransport)
            {
                LOG_WARNING("ApplicationProcessor runtime option --path-tracing-transport ignored: value must be 'full', 'direct', 'single-diffuse-bounce' or 'two-diffuse-bounces'");
            }

            bool bMatchedSampleBatch = false;
            if (TryParsePathTracingSampleBatchOption(args[i], pathTracingSampleBatch, bMatchedSampleBatch))
            {
                LOG_INFO("ApplicationProcessor runtime option path_tracing_sample_batch=%u",
                         pathTracingSampleBatch);
            }
            else if (bMatchedSampleBatch)
            {
                LOG_WARNING("ApplicationProcessor runtime option --path-tracing-sample-batch ignored: value must be 0-255");
            }

            bool bMatchedPixelSampling = false;
            if (TryParsePathTracingPixelSamplingOption(args[i], pathTracingPixelSampling,
                                                       bMatchedPixelSampling))
            {
                LOG_INFO("ApplicationProcessor runtime option path_tracing_pixel_sampling=%u",
                         static_cast<unsigned int>(pathTracingPixelSampling));
            }
            else if (bMatchedPixelSampling)
            {
                LOG_WARNING("ApplicationProcessor runtime option --path-tracing-pixel-sampling ignored: value must be 'box' or 'center'");
            }

            bool bMatchedDebugOutput = false;
            if (TryParsePathTracingDebugOutputOption(args[i], pathTracingDebugOutput,
                                                     bMatchedDebugOutput))
            {
                LOG_INFO("ApplicationProcessor runtime option path_tracing_debug_output=%u",
                         static_cast<unsigned int>(pathTracingDebugOutput));
            }
            else if (bMatchedDebugOutput)
            {
                LOG_WARNING("ApplicationProcessor runtime option --path-tracing-debug-output ignored: value must be 'none', 'albedo', 'shading-normal', 'metallic-roughness', 'hit-distance' or 'sun-visibility'");
            }

            bool bMatchedDirectBrdf = false;
            if (TryParseRasterDirectBrdfOption(args[i], rasterDirectBrdf, bMatchedDirectBrdf))
            {
                LOG_INFO("ApplicationProcessor runtime option raster_direct_brdf=%u",
                         static_cast<unsigned int>(rasterDirectBrdf));
            }
            else if (bMatchedDirectBrdf)
            {
                LOG_WARNING("ApplicationProcessor runtime option --raster-direct-brdf ignored: value must be 'neural' or 'analytic'");
            }

            bool bMatchedVisibilityBuffer = false;
            if (TryParseVisibilityBufferOption(args[i], visibilityBufferMode, bMatchedVisibilityBuffer))
            {
                LOG_INFO("ApplicationProcessor runtime option visibility_buffer=%u",
                         static_cast<unsigned int>(visibilityBufferMode));
            }
            else if (bMatchedVisibilityBuffer)
            {
                LOG_WARNING("ApplicationProcessor runtime option --visibility-buffer "
                            "ignored: value must be 'off', 'on' or 'debug'");
            }

            bool bMatchedSwRaster = false;
            if (TryParseSwRasterOption(args[i], swRasterMode, bMatchedSwRaster))
            {
                LOG_INFO("ApplicationProcessor runtime option sw_raster=%u",
                         static_cast<unsigned int>(swRasterMode));
            }
            else if (bMatchedSwRaster)
            {
                LOG_WARNING("ApplicationProcessor の起動引数 --sw-raster "
                            "を無視します: 値は 'off' か 'on' にしてください");
            }

            bool bMatchedSwRasterMaxPixels = false;
            if (TryParseSwRasterMaxPixelsOption(args[i], swRasterMaxPixels, bMatchedSwRasterMaxPixels))
            {
                LOG_INFO("ApplicationProcessor runtime option sw_raster_max_px=%.2f", swRasterMaxPixels);
            }
            else if (bMatchedSwRasterMaxPixels)
            {
                LOG_WARNING("ApplicationProcessor の起動引数 --sw-raster-max-px "
                            "を無視します: 値は正の数にしてください");
            }

            // --shadow-method=csm|vsm: 太陽の影の方式（既定は BootConfig::DefaultSunShadowMethod）。不正な値は起動時のエラー
            bool bMatchedShadowMethod = false;
            if (TryParseShadowMethodOption(args[i], shadowMethod, bMatchedShadowMethod))
            {
                LOG_INFO("ApplicationProcessor runtime option shadow_method=%u",
                         static_cast<unsigned int>(shadowMethod));
            }
            else if (bMatchedShadowMethod)
            {
                bInvalidShadowMethod = true;
                LOG_ERROR("ApplicationProcessor の起動引数 --shadow-method の値が不正です: 'csm' か 'vsm' にしてください");
            }

            // --vsm-pool-pages=<n>: VSM の物理ページのプールのページの数（既定 4096）。不正な値は起動時のエラー
            bool bMatchedVsmPoolPages = false;
            if (TryParseVsmPoolPagesOption(args[i], vsmPoolPages, bMatchedVsmPoolPages))
            {
                LOG_INFO("ApplicationProcessor runtime option vsm_pool_pages=%u", vsmPoolPages);
            }
            else if (bMatchedVsmPoolPages)
            {
                bInvalidVsmPoolPages = true;
                LOG_ERROR("ApplicationProcessor の起動引数 --vsm-pool-pages の値が不正です: 1 以上の整数にしてください");
            }

            // --shadow-probe: 太陽の影の標本のパスを足す（統計が有効な構成のみ。Release では無視される）
            if (args[i] == TEXT("--shadow-probe"))
            {
                bShadowProbe = true;
                LOG_INFO("ApplicationProcessor runtime option shadow_probe=1");
            }

            bool bMatchedToneMap = false;
            if (TryParseToneMapOption(args[i], toneMapOperator, bMatchedToneMap))
            {
                bToneMapOperatorRequested = true;
                LOG_INFO("ApplicationProcessor runtime option tone_map=%u",
                         static_cast<unsigned int>(toneMapOperator));
            }
            else if (bMatchedToneMap)
            {
                LOG_WARNING("ApplicationProcessor runtime option --tone-map ignored: value must be 'aces' or 'aces20-lut'");
            }

            bool bMatchedFilmGrain = false;
            if (TryParseFilmGrainOption(args[i], filmGrainStrength, filmGrainSeed, bMatchedFilmGrain))
            {
                bFilmGrainRequested = true;
                LOG_INFO("ApplicationProcessor runtime option film_grain strength=%g seed=%u",
                         static_cast<double>(filmGrainStrength), static_cast<unsigned int>(filmGrainSeed));
            }
            else if (bMatchedFilmGrain)
            {
                LOG_WARNING("ApplicationProcessor runtime option --film-grain ignored: strength must be 0-1 and seed a 32-bit integer");
            }

            bool bMatchedSequenceFrame = false;
            if (TryParsePathTracingSequenceOption(args[i], pathTracingSequenceFrame,
                                                  bMatchedSequenceFrame))
            {
                LOG_INFO("ApplicationProcessor runtime option path_tracing_sequence_frame frame_duration=%g shutter=%g aperture=%g focus_distance=%g",
                         static_cast<double>(pathTracingSequenceFrame.FrameDuration),
                         static_cast<double>(pathTracingSequenceFrame.ShutterDuration),
                         static_cast<double>(pathTracingSequenceFrame.Aperture),
                         static_cast<double>(pathTracingSequenceFrame.FocusDistance));
            }
            else if (bMatchedSequenceFrame)
            {
                LOG_WARNING("ApplicationProcessor runtime option ignored: --path-tracing-frame-duration and --path-tracing-aperture must be positive, --path-tracing-shutter and --path-tracing-focus-distance must be non-negative (decimal or a/b)");
            }

            bool bMatchedCapturePng = false;
            if (TryParseCapturePngOption(args[i], m_CapturePngPath, bMatchedCapturePng))
            {
                LOG_INFO("ApplicationProcessor runtime option capture_png=1");
            }
            else if (bMatchedCapturePng)
            {
                LOG_WARNING("ApplicationProcessor runtime option --capture-png ignored: path must not be empty");
            }

            if (IsCaptureDeterministicOption(args[i]))
            {
                bCaptureDeterministic = true;
            }
        }

        // 決定的な撮影: 経過時間を 1/60 秒に固定し、描画は1フレームずつGameThreadで行う（RenderThread が
        // フレームを飛ばしたり遅れたりして、撮る瞬間の履歴が変わらないようにする）。
        if (bCaptureDeterministic)
        {
            if (m_CapturePngPath.empty())
            {
                LOG_WARNING("ApplicationProcessor runtime option --capture-deterministic "
                            "は --capture-png が無いので無視する");
                bCaptureDeterministic = false;
            }
            else
            {
                GEngine->GetDeterministicCapture().Enable();
                if (bEnableMultiThreadedRendering)
                {
                    bEnableMultiThreadedRendering = false;
                }
                LOG_INFO("ApplicationProcessor runtime option capture_deterministic=1 "
                         "render_thread=st fixed_delta_s=%.6f",
                         static_cast<double>(DeterministicCapture::FixedDeltaSeconds));
            }
        }

        // 撮影はアセットの読み込みが落ち着いた後の画面を取る。描画フレーム数の指定が無ければ既定値を使う。
        // 最終出力だけを写すため、キャンバス（画面空間のボード）は無効にし、overlay（ImGui）は描かない。
        if (!m_CapturePngPath.empty())
        {
            exitFrameOptions.bWaitForAssetSettle = true;
            if (exitFrameOptions.RenderedTarget == 0)
            {
                exitFrameOptions.RenderedTarget = kCapturePngDefaultSettledRenderedFrames;
            }
            if (bEnableCanvasView)
            {
                LOG_WARNING("ApplicationProcessor runtime option --enable-canvas-view ignored with --capture-png");
                bEnableCanvasView = false;
            }
            LOG_INFO("ApplicationProcessor capture_png settled_rendered_frames=%llu",
                     static_cast<unsigned long long>(exitFrameOptions.RenderedTarget));
        }

        const Detail::ExitFrameSelection exitFrameSelection = Detail::SelectExitFrameSelection(exitFrameOptions);
        m_ExitAfterFrames = exitFrameSelection.Metric == Detail::ExitFrameMetric::GameThreadFrames ? exitFrameSelection.Target : 0;
        m_ExitAfterRenderedFrames = exitFrameSelection.Metric == Detail::ExitFrameMetric::RenderedFrames ? exitFrameSelection.Target : 0;
        m_bWaitForAssetSettle = exitFrameSelection.bWaitForAssetSettle;
        if (exitFrameOptions.LegacyTarget > 0 && exitFrameOptions.RenderedTarget > 0)
        {
            LOG_WARNING("ApplicationProcessor runtime option --exit-after-rendered-frames takes precedence over --exit-after-frames");
        }
        if (exitFrameOptions.bWaitForAssetSettle && m_ExitAfterRenderedFrames == 0)
        {
            LOG_WARNING("ApplicationProcessor runtime option --wait-for-asset-settle ignored without --exit-after-rendered-frames");
        }

        if (bInvalidShadowMethod || bInvalidVsmPoolPages)
        {
            return false;
        }

        // OnPreInitialize呼び出し
        auto *handler = GEngine->GetApplicationHandler();
        if (handler && !handler->OnPreInitialize(args))
        {
            LOG_ERROR("OnPreInitialize failed");
            return false;
        }

        // プラットフォームアプリケーションを作成
        if (!CreatePlatformApplication(config))
        {
            LOG_ERROR("Failed to create platform application");
            return false;
        }
        GApplicationLifecycleState.bPlatform = true;

        // メインウィンドウを作成
        if (!CreateMainWindow(config))
        {
            LOG_ERROR("Failed to create main window");
            return false;
        }
        GApplicationLifecycleState.bWindow = true;

        // RHI初期化（エンジン層の責務）
        {
            RHI::RHIDeviceDesc deviceDesc;
            deviceDesc.Api = config.Api;
            deviceDesc.bEnableValidation = config.bEnableRHIValidation;

            m_Device = RHI::CreateRHIDevice(deviceDesc);
            if (!m_Device)
            {
                LOG_ERROR("Failed to initialize RHI");
                return false;
            }
            GApplicationLifecycleState.bDevice = true;
        }
        LOG_INFO("RHI initialized successfully");

        // レンダリングシステムを初期化
        {
            Rendering::RenderWorldSettings renderSettings;
            renderSettings.Device = m_Device;
            renderSettings.WindowHandle = GEngine->GetMainWindow()->GetNativeHandle();
            renderSettings.Width = config.WindowWidth;
            renderSettings.Height = config.WindowHeight;
            renderSettings.BackBufferCount = 2;
            renderSettings.bVSync = true;
            renderSettings.bEnableMultiThreadedRendering = bEnableMultiThreadedRendering;
            renderSettings.bEnableValidation = config.bEnableRHIValidation;
            renderSettings.MainViewRenderer = mainViewRenderer;
            renderSettings.PathTracingSamplesPerFrame = pathTracingSamplesPerFrame;
            renderSettings.PathTracingTransport = pathTracingTransport;
            renderSettings.PathTracingPixelSamplingMode = pathTracingPixelSampling;
            renderSettings.PathTracingSampleBatch = pathTracingSampleBatch;
            renderSettings.PathTracingDebug = pathTracingDebugOutput;
            renderSettings.RasterDirectBrdfMode = rasterDirectBrdf;
            renderSettings.VisibilityBuffer = visibilityBufferMode;
            renderSettings.SwRaster = swRasterMode;
            renderSettings.SwRasterMaxPixels = swRasterMaxPixels;
            renderSettings.bShadowProbe = bShadowProbe;
            renderSettings.SunShadowMethod = shadowMethod;
            renderSettings.VsmPoolPages = vsmPoolPages;

            if (!GEngine->GetRenderWorld().Initialize(renderSettings))
            {
                LOG_ERROR("Failed to initialize RenderWorld");
                return false;
            }
            GApplicationLifecycleState.bRenderWorld = true;
            if (bCaptureDeterministic)
            {
                GEngine->GetRenderWorld().SetDeterministicCapture(true);
            }
            LOG_INFO("RenderWorld initialized successfully");

            auto &coordinator = GEngine->GetRenderWorld().GetRenderingCoordinator();
            coordinator.SetBoardInstanceBatchingEnabled(bBoardInstanceBatchingEnabled);

            if (bToneMapOperatorRequested &&
                !ApplyMainViewToneMapOperator(coordinator.GetMainSceneView().get(), toneMapOperator))
            {
                LOG_WARNING("ApplicationProcessor runtime option --tone-map ignored: main view has no ToneMappingPass");
            }

            if (bFilmGrainRequested &&
                !ApplyMainViewFilmGrain(coordinator.GetMainSceneView().get(), filmGrainStrength, filmGrainSeed))
            {
                LOG_WARNING("ApplicationProcessor runtime option --film-grain ignored: main view has no ToneMappingPass");
            }

            if (pathTracingSequenceFrame.bEnabled &&
                !ApplyMainViewPathTracingSequenceFrame(coordinator.GetMainSceneView().get(),
                                                       pathTracingSequenceFrame))
            {
                LOG_WARNING("ApplicationProcessor runtime option --path-tracing-* sequence frame ignored: main view has no PathTracingPass (use --renderer=path-tracing)");
            }

            if (bEnableCanvasView)
            {
                auto canvasView = coordinator.CreateCanvasView();
                if (canvasView)
                {
                    LOG_INFO("CanvasView created from runtime option");
                }
                else
                {
                    LOG_WARNING("CanvasView runtime option was requested but creation failed");
                }
            }

#if NORVES_ENABLE_CORE_TEXT
            if (auto canvasView = coordinator.GetCanvasView())
            {
                auto atlas = MakeShared<FontAtlas>();
                Asset::AssetFileReader reader;
                if (!atlas->Build(FontAtlasDesc{}, reader, GEngine->GetRenderResources().Textures()))
                {
                    NORVES_LOG_ERROR("ApplicationProcessor", "Failed to build default Core text atlas");
                    return false;
                }
                GEngine->SetDefaultFontAtlas(atlas);
            }
#endif
        }

        // ゲームワールドを初期化
        {
            GApplicationLifecycleState.bWorld = true;
            GEngine->GetWorld().Initialize();

            // WorldにメインSceneViewを設定
            auto &coordinator = GEngine->GetRenderWorld().GetRenderingCoordinator();
            auto mainSceneView = coordinator.GetMainSceneView();
            if (mainSceneView)
            {
                GEngine->GetWorld().SetSceneView(mainSceneView.get());
                LOG_INFO("World connected to MainSceneView");
            }
            else
            {
                LOG_WARNING("MainSceneView not available for World");
            }

            LOG_INFO("World initialized successfully");
        }

        GApplicationLifecycleState.bScriptRuntime = true;
        if (NorvesLib::Core::GEngine.GetScriptRuntime().Initialize(GEngine->GetWorld()) !=
            EScriptRuntimeResult::Success)
        {
            LOG_ERROR("Failed to initialize ScriptRuntime");
            return false;
        }

        // OnInitialize呼び出し
        if (handler && !handler->OnInitialize())
        {
            LOG_ERROR("OnInitialize failed");
            return false;
        }
        if (handler)
        {
            GApplicationLifecycleState.bHandlerInitialized = true;
            GApplicationLifecycleState.bPreShutdownPending = true;
            GApplicationLifecycleState.bShutdownPending = true;
        }

        // GameModeステートマシンを作成
        if (handler)
        {
            auto stateMachine = handler->CreateGameModeStateMachine();
            if (stateMachine)
            {
                GEngine->SetGameModeStateMachine(std::move(stateMachine));
                GApplicationLifecycleState.bGameMode = true;
                LOG_INFO("GameMode state machine created successfully");
            }
        }

        // モジュールの寿命を駆動: RenderWorld 初期化成功後(RenderThread 起動済みで RHI 掴み可)・
        // OnPostInitialize 前に InstallAll。Game が誰も Register しなければ registry は空で no-op。
        {
            // 終了時の静止バリアフックを先に登録する(device 解放直前・RenderThread 停止後に
            // ShutdownAll が走る。MT use-after-free を回避)。
            GEngine->GetRenderWorld().SetPreDeviceTeardownHook(&ShutdownModulesPreDeviceTeardown, nullptr);

            if (!Module::GetModuleRegistry().InstallAll(*GEngine))
            {
                LOG_ERROR("Module InstallAll failed");
                return false;
            }
            GApplicationLifecycleState.bModules = true;
        }

        // OnPostInitialize呼び出し
        if (handler)
        {
            handler->OnPostInitialize();
        }

        // Handlerが差替padを登録していなければ標準backendを所有する。
        if (!GEngine->HasGamepadInputDevice() &&
            !GEngine->AddInputDevice(Platform::CreateGamepadDevice()))
        {
            LOG_ERROR("ゲームパッド入力deviceの生成に失敗しました");
            return false;
        }
        // Handler/モジュールの初期化後に購読し、通知はmessage処理外へ遅延する。
        ConnectInputWindow(GEngine->GetMainWindowShared());
        GEngine->SetRunning(true);
        GApplicationLifecycleState.bRunning = true;
        transaction.Commit();
        LOG_INFO("ApplicationProcessor::Initialize() - Initialization completed");

        return true;
    }
    catch (...)
    {
        LOG_ERROR("ApplicationProcessor::Initialize() - initialization threw an exception");
        return false;
    }

    int ApplicationProcessor::Run()
    {
        struct RunCleanup
        {
            FixedStepScheduler& Scheduler;
            ~RunCleanup()
            {
                // PumpMessages/OnUpdate例外でもheld/fixedPressを残さない。
                if (GEngine)
                {
                    (void)GEngine->ShutdownInputDevices();
                    GEngine->GetInputRebindCapture().Detach();
                    GEngine->GetInputDebugOverlay().Detach();
                    GEngine->GetInputMapper().CancelAll();
                    if(auto window=GEngine->GetMainWindowShared()) (void)window->SetCursorMode(ECursorMode::Normal);
                }
                Scheduler.EndRun();
            }
        } cleanup{*m_FixedStepScheduler};
        LOG_INFO("ApplicationProcessor::Run() - Starting main loop");

        m_LastFrameTimeNanoseconds = std::chrono::duration_cast<std::chrono::nanoseconds>(
            std::chrono::steady_clock::now().time_since_epoch()).count();
        if (GEngine)
        {
            GEngine->GetInputMapper().CancelAll();
            if (!GEngine->GetInputDebugOverlay().Attach() || !GEngine->GetInputRebindCapture().Attach())
            {
                LOG_ERROR("入力controllerの配線が一致しないためRunを開始できません");
                return -1;
            }
        }
        (void)ApplyPendingInputDeviceFocus();
        if (GEngine && !GEngine->InitializeInputDevices())
        {
            LOG_ERROR("入力deviceの開始に失敗しました");
            return -1;
        }
        bool inputDeviceWarning = false;
        m_HapticsFailureWarned = false;
        m_FixedStepScheduler->BeginRun();

        while (GEngine && GEngine->IsRunning() && !GEngine->IsExitRequested())
        {
            // 入力システムのフレーム開始（前フレーム状態保存、累積値リセット）
            // ※ProcessPlatformMessagesの前に呼ぶこと。
            //   メッセージ処理中にInjectされた入力をOnUpdateで参照するため。
            const int64_t inputFrameTime = std::chrono::duration_cast<std::chrono::nanoseconds>(
                std::chrono::steady_clock::now().time_since_epoch()).count();
            if (!BeginInputFrame(inputFrameTime))
            {
                GEngine->GetInputMapper().CancelAll();
                LOG_WARNING("入力frameの開始時刻が不正なため操作を取り消しました");
            }

            (void)SynchronizeInputCursorMode();
            // プラットフォームメッセージ処理
            if (!ProcessPlatformMessages())
            {
                break;
            }

            // 最新のfocus messageを反映した後、Mapper評価前に同frameのpadを供給する。
            const double inputDeviceTime = std::chrono::duration<double>(
                std::chrono::steady_clock::now().time_since_epoch()).count();
            const bool inputDevicesHealthy = GEngine->PollInputDevices(inputDeviceTime);
            (void)ApplyPendingInputDeviceFocus();
            if (!inputDevicesHealthy && !inputDeviceWarning)
            {
                LOG_WARNING("入力deviceが未回復のerrorを報告しています");
            }
            inputDeviceWarning = !inputDevicesHealthy;
            // 1フレームの処理
            Tick();
            (void)SynchronizeInputCursorMode();
        }

        LOG_INFO("ApplicationProcessor::Run() - Main loop ended");

        return GEngine ? GEngine->GetExitCode() : 0;
    }

    void ApplicationProcessor::Shutdown()
    {
        LOG_INFO("ApplicationProcessor::Shutdown() - Starting shutdown");

        ApplicationLifecycleState& lifecycle = GApplicationLifecycleState;
        if (lifecycle.Processor != this)
        {
            return;
        }
        if (lifecycle.bSkeletalSession)
        {
            auto& skeletal = NorvesLib::Core::GEngine.GetSkeletalAssetSession();
            const auto closed = skeletal.Close();
            const auto drained = skeletal.Drain();
            if (drained == SkeletalRuntimeStatus::Deferred)
            {
                // callback内では依存をまだ解体しない。Run外側のShutdownで完了する。
                if (GEngine)
                {
                    GEngine->RequestExit();
                }
                return;
            }
            if (closed != SkeletalRuntimeStatus::Success || drained != SkeletalRuntimeStatus::Drained)
            {
                NORVES_LOG_ERROR("SkeletalAssets", "owner骨格sessionの終了境界に到達できませんでした");
                std::abort();
            }
        }
        DisconnectInputWindow();

        if (GEngine && lifecycle.bEngine)
        {
            // Handler/World/windowの破棄より先に入力操作とRouter登録を解除する。
            if (!GEngine->ShutdownInputDevices())
            {
                LOG_WARNING("入力deviceの終了に失敗し、所有者破棄時に再試行します");
            }
            GEngine->GetInputRebindCapture().Detach();
            GEngine->GetInputDebugOverlay().Detach();
            GEngine->GetInputMapper().Detach();
            auto *handler = GEngine->GetApplicationHandler();

            if (lifecycle.bRunning)
            {
                GEngine->SetRunning(false);
                lifecycle.bRunning = false;
            }

            // GameModeがResourceを解放する前に最後の描画とGPU使用を完了させる。
            if (lifecycle.bRenderWorld)
            {
                GEngine->GetRenderWorld().WaitForRender();
                GEngine->GetRenderWorld().QuiesceAsyncAssetProducersAndWait();
            }
            if (lifecycle.bJobSystem)
            {
                Thread::JobSystem::Get().StopAcceptingTasks();
                Thread::JobSystem::Get().DrainAcceptedFiniteTasks();
            }

            // OnPreShutdown呼び出し
            if (handler && lifecycle.bPreShutdownPending)
            {
                lifecycle.bPreShutdownPending = false;
                try
                {
                    handler->OnPreShutdown();
                }
                catch (...)
                {
                    LOG_ERROR("ApplicationProcessor::Shutdown() - OnPreShutdown threw an exception");
                }
            }
            // GameModeステートマシンを明示シャットダウン（World/RenderWorld/RHI破棄より前）
            // Routine の Leave() が World/RenderResources へアクセスするため、それらが
            // 生存している間に Leave を実行させる必要がある。
            if (lifecycle.bGameMode)
            {
                if (auto *stateMachine = GEngine->GetGameModeStateMachine())
                {
                    stateMachine->Shutdown();
                }
            }

            // ゲームワールドをFinalize
            // SceneQuery clear before World::Finalize() to avoid holding destroyed Entity*
            if (lifecycle.bWorld)
            {
                GEngine->GetSceneQuery().Clear();
                GEngine->GetWorld().Finalize();
                lifecycle.bWorld = false;
            }

            if (lifecycle.bScriptRuntime)
            {
                const EScriptRuntimeResult runtimeShutdownResult =
                    NorvesLib::Core::GEngine.GetScriptRuntime().Shutdown();
                if (runtimeShutdownResult != EScriptRuntimeResult::Success &&
                    runtimeShutdownResult != EScriptRuntimeResult::NotInitialized)
                {
                    LOG_ERROR("ApplicationProcessor::Shutdown() - ScriptRuntime cleanup failed");
                    std::abort();
                }
                lifecycle.bScriptRuntime = false;
            }

            if (auto atlas = GEngine->GetDefaultFontAtlas())
            {
                atlas->Shutdown(GEngine->GetRenderResources().Textures());
                GEngine->ResetDefaultFontAtlas();
            }

            // レンダリングシステムをシャットダウン
            if (lifecycle.bRenderWorld)
            {
                GEngine->GetRenderWorld().Shutdown();
                lifecycle.bRenderWorld = false;
            }

            // RHI終了（エンジン層の責務）
            if (lifecycle.bDevice)
            {
                m_Device.reset();
                lifecycle.bDevice = false;
            }
            LOG_INFO("RHI shutdown completed");

            // GameModeステートマシンを解放
            if (lifecycle.bGameMode)
            {
                GEngine->SetGameModeStateMachine(nullptr);
                lifecycle.bGameMode = false;
            }

            // OnShutdown呼び出し
            if (handler && lifecycle.bShutdownPending)
            {
                lifecycle.bShutdownPending = false;
                try
                {
                    handler->OnShutdown();
                }
                catch (...)
                {
                    LOG_ERROR("ApplicationProcessor::Shutdown() - OnShutdown threw an exception");
                }
            }

            // メインウィンドウを解放
            if (lifecycle.bWindow)
            {
                GEngine->SetMainWindow(nullptr);
                lifecycle.bWindow = false;
            }

            // プラットフォームアプリケーションを終了
            auto *platformApp = GEngine->GetPlatformApp();
            if (lifecycle.bPlatform && platformApp)
            {
                platformApp->Shutdown();
            }
            if (lifecycle.bPlatform)
            {
                GEngine->SetPlatformApp(nullptr);
                lifecycle.bPlatform = false;
            }

            // ハンドラを解放
            if (lifecycle.bHandler)
            {
                GEngine->SetApplicationHandler(nullptr);
                lifecycle.bHandler = false;
            }
        }

        // 全consumer・renderer解体後、取得したRegistryだけを最後に終了する。
        if (lifecycle.bSkeletalSession)
        {
            if (!NorvesLib::Core::GEngine.GetSkeletalAssetSession().End())
            {
                NORVES_LOG_ERROR("SkeletalAssets", "骨格sessionの所有を終了できませんでした");
                std::abort();
            }
            lifecycle.bSkeletalSession = false;
        }

        // GEngineを破棄
        if (lifecycle.bEngine)
        {
            DestroyEngine();
            lifecycle.bEngine = false;
        }

        // JobSystemをシャットダウン
        if (lifecycle.bJobSystem)
        {
            Thread::JobSystem::Get().Shutdown();
            lifecycle.bJobSystem = false;
        }

        lifecycle.Processor = nullptr;

        LOG_INFO("ApplicationProcessor::Shutdown() - Shutdown completed");
    }

    void ApplicationProcessor::Tick()
    {
#if NORVES_ENABLE_STATS
        auto &statsManager = Debug::StatsManager::Get();
        const bool bTraceActive = statsManager.IsTraceActive();
        std::chrono::high_resolution_clock::time_point gameThreadStartTime;
        if (bTraceActive)
        {
            gameThreadStartTime = std::chrono::high_resolution_clock::now();
        }
#endif

        // 決定的な撮影では壁時計を使わず、毎フレーム 1/60 秒進める。
        DeterministicCapture &deterministicCapture = GEngine->GetDeterministicCapture();
        const bool bDeterministicCapture = deterministicCapture.IsEnabled();
        const int64_t measuredDeltaNanoseconds = CalculateRawDeltaTimeNanoseconds();
        const int64_t rawDeltaNanoseconds =
            bDeterministicCapture ? DeterministicCapture::FixedDeltaNanoseconds : measuredDeltaNanoseconds;
        const float deltaTime = ClampVariableDeltaTime(rawDeltaNanoseconds);
        GEngine->SetDeltaTime(deltaTime);
        // 入力はゲーム用100ms clampの前の実dtで評価し、OnUpdateから同frame値を読める。
        if (!UpdateInputFrame(m_LastFrameTimeNanoseconds, rawDeltaNanoseconds))
        {
            GEngine->GetInputMapper().CancelAll();
            LOG_WARNING("入力actionの評価に失敗したため操作を取り消しました");
        }
        deterministicCapture.AdvanceFrame();

#if NORVES_ENABLE_STATS
        if (bTraceActive)
        {
            statsManager.BeginFrame(GEngine->GetFrameCount(), deltaTime);
        }
#endif

        // 注: BeginFrame()はRun()ループ内でProcessPlatformMessagesの前に呼ばれている

        auto handlerOwner = GEngine->GetApplicationHandlerShared();
        auto* handler = handlerOwner.get();

        // OnUpdate呼び出し
        // 注: OnUpdate はシミュレーション進行ゲートの影響を受けない。Bridge ポーズ中も
        // 受信フレーム処理（DrainInbound）を回し続ける必要があるため、ここはガードしない。
        auto& skeletalSession = NorvesLib::Core::GEngine.GetSkeletalAssetSession();
        if (m_bWaitForAssetSettle)
        {
            Detail::ObservePendingAssets(HasPendingSkeletalConsumers(skeletalSession, handler),
                                         m_bObservedPendingAssets, m_bAssetSettleBaselineLatched);
        }
        const auto skeletalTick = TickSkeletalOwnerAssetsAndHandler(skeletalSession, handler, deltaTime);
        if (skeletalTick.Status != SkeletalRuntimeStatus::Success)
        {
            GEngine->RequestExit(skeletalTick.Status == SkeletalRuntimeStatus::Closed ? 0 : 1);
            return;
        }

        if (NorvesLib::Core::GEngine.GetScriptRuntime().BeginFrameMaintenance(deltaTime) !=
            EScriptRuntimeResult::Success)
        {
            LOG_ERROR("ScriptRuntime BeginFrameMaintenance failed");
        }

        TickSimulationAndHaptics(rawDeltaNanoseconds, deltaTime, handler);

        // ワールドからSceneViewへProxy同期
        GEngine->GetWorld().SyncToSceneView(
            &GEngine->GetRenderResources().Materials(),
            &GEngine->GetRenderResources().Meshes());
        Container::VariableArray<Rendering::BoardProxy> frameTransient;
        GEngine->GetWorld().CollectTransientBoardProxies(frameTransient);
        GEngine->GetParticleSystem().AppendBoardProxies(frameTransient);
        if (auto canvasView = GEngine->GetRenderWorld().GetRenderingCoordinator().GetCanvasView())
        {
            canvasView->SetTransientBoardProxies(frameTransient);
        }
        // SceneQuery を当フレームの最新 world-AABB で再構築(SyncToSceneView が transform/bounds を確定済み)
        GEngine->GetSceneQuery().Rebuild(GEngine->GetWorld());

        if (NorvesLib::Core::GEngine.GetScriptRuntime().EndFrameMaintenance() !=
            EScriptRuntimeResult::Success)
        {
            LOG_ERROR("ScriptRuntime EndFrameMaintenance failed");
        }

        // モジュールの毎フレーム Tick(GameThread・SyncToSceneView 後・描画前)。空 registry で no-op。
        Module::GetModuleRegistry().TickAll(deltaTime);

        // 描画モジュールの overlay パスを収集(借用ポインタ・非 null のみ)。
        // RenderThread が直接 registry を読まないよう、この集合を書き込み中パケットへ焼く。
        // 撮影時は overlay（ImGui）を最終出力へ載せない。
        Container::VariableArray<Rendering::IViewPass *> overlayPasses;
        for (Module::IRenderModule *renderModule : Module::GetModuleRegistry().GetRenderModules())
        {
            if (!renderModule || !m_CapturePngPath.empty())
            {
                continue;
            }
            if (Rendering::IViewPass *overlayPass = renderModule->GetOverlayPass())
            {
                overlayPasses.push_back(overlayPass);
            }
        }

        // OnPreRender呼び出し
        if (handler)
        {
            handler->OnPreRender();
        }

        bool bRenderedExitReached = false;
        uint64_t renderedFrameCount = 0;
        // 描画処理
        {
            auto &renderWorld = GEngine->GetRenderWorld();
            if (renderWorld.IsInitialized())
            {
                // 決定的な撮影では、GameMode が読み込み後の組み立て（大きな球の生成など）を終えるまでを
                // 読み込み中として数える。
                const bool bSceneAssembling = bDeterministicCapture && !deterministicCapture.IsSceneReady();
                if (m_bWaitForAssetSettle)
                {
                    Detail::ObservePendingAssets(renderWorld.HasPendingAsyncAssets() ||
                                                     HasPendingSkeletalConsumers(skeletalSession, handler) ||
                                                     bSceneAssembling,
                                                 m_bObservedPendingAssets, m_bAssetSettleBaselineLatched);
                }
                renderWorld.BeginFrame();
                // BeginFrame で書き込み中パケットが確保された後に overlay 集合を載せる
                // (同一フレームで RenderFrame が消費・1 フレーム遅延なし)。空なら完全 no-op。
                renderWorld.GetRenderingCoordinator().SetOverlayPassesForNextFrame(
                    Container::Span<Rendering::IViewPass *>(overlayPasses.data(), overlayPasses.size()));
                renderWorld.Render();
                renderWorld.EndFrame();
                renderedFrameCount = renderWorld.GetRenderedFrameCount();
                if (m_ExitAfterRenderedFrames > 0)
                {
                    if (m_bWaitForAssetSettle)
                    {
                        const bool bWasLatched = m_bAssetSettleBaselineLatched;
                        const uint64_t previousBaseline = m_AssetSettleRenderedBaseline;
                        bRenderedExitReached = Detail::EvaluateSettledRenderedExit(
                            renderWorld.HasPendingAsyncAssets() ||
                                HasPendingSkeletalConsumers(skeletalSession, handler) || bSceneAssembling,
                            renderedFrameCount, m_ExitAfterRenderedFrames, m_bObservedPendingAssets,
                            m_bAssetSettleBaselineLatched, m_AssetSettleRenderedBaseline);
                        if (m_bAssetSettleBaselineLatched &&
                            (!bWasLatched || previousBaseline != m_AssetSettleRenderedBaseline))
                        {
                            LOG_INFO("ApplicationProcessor asset settle baseline rendered=%llu",
                                     static_cast<unsigned long long>(m_AssetSettleRenderedBaseline));
                            if (bDeterministicCapture)
                            {
                                // 読み込み完了の時点から、時間・TAA・RTGI・自動露出を数え直す（次のフレームが 0 番）。
                                deterministicCapture.BeginEpoch();
                                renderWorld.BeginDeterministicEpoch();
                                LOG_INFO("ApplicationProcessor capture_deterministic "
                                         "epoch begin rendered=%llu",
                                         static_cast<unsigned long long>(renderedFrameCount));
                            }
                        }
                    }
                    else
                    {
                        bRenderedExitReached = renderedFrameCount >= m_ExitAfterRenderedFrames;
                    }
                }

                // 撮影: 終了条件に達したフレームで最終出力（overlay後のBackBuffer）の取得を要求し、
                // 結果を受け取ってPNGに保存してから終了する。
                if (!m_CapturePngPath.empty())
                {
                    if (!m_bCaptureRequested && bRenderedExitReached)
                    {
                        Rendering::FrameCaptureRequest captureRequest;
                        captureRequest.SourceKind = Rendering::FrameCaptureSourceKind::BackBuffer;
                        if (renderWorld.RequestFrameCapture(captureRequest).IsAccepted())
                        {
                            m_bCaptureRequested = true;
                            m_CaptureRequestedRenderedFrame = renderedFrameCount;
                            LOG_INFO("ApplicationProcessor capture_png requested rendered=%llu",
                                     static_cast<unsigned long long>(renderedFrameCount));
                        }
                        else
                        {
                            LOG_ERROR("ApplicationProcessor capture_png failed: capture request rejected");
                            GEngine->RequestExit(1);
                        }
                    }
                    else if (m_bCaptureRequested)
                    {
                        Rendering::CapturedFrame capturedFrame;
                        if (renderWorld.TryConsumeCapturedFrame(capturedFrame))
                        {
                            if (SaveCapturedFramePng(capturedFrame, m_CapturePngPath))
                            {
                                LOG_INFO("ApplicationProcessor capture_png saved frame=%llu width=%u height=%u",
                                         static_cast<unsigned long long>(capturedFrame.FrameNumber),
                                         capturedFrame.Width,
                                         capturedFrame.Height);
                                GEngine->RequestExit(0);
                            }
                            else
                            {
                                LOG_ERROR("ApplicationProcessor capture_png failed: status=%u format=%u width=%u height=%u",
                                          static_cast<unsigned int>(capturedFrame.Status),
                                          static_cast<unsigned int>(capturedFrame.Format),
                                          capturedFrame.Width,
                                          capturedFrame.Height);
                                GEngine->RequestExit(1);
                            }
                        }
                        else if (renderedFrameCount - m_CaptureRequestedRenderedFrame >
                                 kCapturePngResultTimeoutRenderedFrames)
                        {
                            LOG_ERROR("ApplicationProcessor capture_png failed: no result within %llu rendered frames",
                                      static_cast<unsigned long long>(kCapturePngResultTimeoutRenderedFrames));
                            GEngine->RequestExit(1);
                        }
                    }
                    bRenderedExitReached = false;
                }
            }
        }

        // OnPostRender呼び出し
        if (handler)
        {
            handler->OnPostRender();
        }

        // フレームカウントをインクリメント
        GEngine->IncrementFrameCount();
        if (m_ExitAfterFrames > 0 && GEngine->GetFrameCount() >= m_ExitAfterFrames)
        {
            LOG_INFO_F("ApplicationProcessor::Tick() - exit-after-frames reached frame=%llu target=%llu",
                       static_cast<unsigned long long>(GEngine->GetFrameCount()),
                       static_cast<unsigned long long>(m_ExitAfterFrames));
            GEngine->RequestExit(0);
        }
        else if (bRenderedExitReached)
        {
            LOG_INFO("ApplicationProcessor::Tick() - exit-after-rendered-frames reached rendered=%llu baseline=%llu target=%llu",
                     static_cast<unsigned long long>(renderedFrameCount),
                     static_cast<unsigned long long>(m_AssetSettleRenderedBaseline),
                     static_cast<unsigned long long>(m_ExitAfterRenderedFrames));
            GEngine->RequestExit(0);
        }

        // 入力システムのフレーム終了
        GEngine->GetInputSystem().EndFrame();

#if NORVES_ENABLE_STATS
        if (bTraceActive)
        {
            auto gameThreadEndTime = std::chrono::high_resolution_clock::now();
            const float gameThreadTimeMs =
                std::chrono::duration<float, std::milli>(gameThreadEndTime - gameThreadStartTime).count();
            statsManager.SetGameThreadTimeMs(gameThreadTimeMs);
            statsManager.EndFrame();
        }
#endif
    }

    void ApplicationProcessor::ConnectInputWindow(Container::TSharedPtr<NorvesLib::IWindow> window)
    {
        DisconnectInputWindow();
        if(!GEngine || !window) return;
        m_InputFocusSubscription.Bind(this,&ApplicationProcessor::OnWindowInputFocusChanged);
        window->OnInputFocusChanged().Add(m_InputFocusSubscription);
        m_InputWindow=std::move(window);m_InputEngine=GEngine;
        OnWindowInputFocusChanged(m_InputWindow->IsInputFocused());
        if(!m_InputWindow->SetRawMouseEnabled(true))
            LOG_WARNING("Raw mouseを登録できないためlegacy入力で起動します");
    }

    void ApplicationProcessor::DisconnectInputWindow()
    {
        m_PendingRouterInputFocus.clear();
        if (GEngine && GEngine == m_InputEngine)
        {
            GEngine->GetInputRebindCapture().Abort();
            QueueInputDeviceFocus(false, m_ApplyingInputDeviceFocus || !GEngine->CanUpdateInputDeviceFocus());
        }
        if(m_InputWindow)
        {
            (void)m_InputWindow->SetCursorMode(ECursorMode::Normal);
            (void)m_InputWindow->SetRawMouseEnabled(false);
            m_InputWindow->OnInputFocusChanged().Remove(m_InputFocusSubscription);
        }
        m_InputFocusSubscription.Clear();
        m_InputWindow.reset();m_InputEngine=nullptr;m_PendingInputFocus.clear();m_HasInputFocus=false;
        ++m_InputFocusConnectionSerial;
    }

    void ApplicationProcessor::OnWindowInputFocusChanged(bool focused)
    {
        if(!GEngine || GEngine!=m_InputEngine || !m_InputWindow ||
            GEngine->GetMainWindow()!=m_InputWindow.get()) return;
        if(m_HasInputFocus && m_InputFocused==focused) return;
        m_HasInputFocus=true;m_InputFocused=focused;
        GEngine->GetInputMapper().SetFocused(focused);
        GEngine->GetInputSystem().SetInputFocused(focused);
        if (!focused)
        {
            // rawは即時中立化し、observerへの通知はfocus適用batchで一度だけ行う。
            GEngine->GetInputSystem().DeferReleaseAll();
        }
        // 通知callbackが次のfocusを生む場合も、要求の発生順を先に記録する。
        m_PendingInputFocus.push_back(focused);
        QueueInputDeviceFocus(focused, !focused, true);
    }

    void ApplicationProcessor::QueueInputDeviceFocus(bool focused, bool resetOperations, bool notifyRouter)
    {
        if (!GEngine)
        {
            return;
        }
        if (m_PendingInputDeviceFocusEngine && m_PendingInputDeviceFocusEngine != GEngine)
        {
            m_PendingInputDeviceFocusLoss = false;
            m_PendingInputDeviceFocusReset = false;
            m_PendingRouterInputFocus.clear();
        }
        if (m_HasPendingInputDeviceFocus && m_PendingInputDeviceFocusSerial != m_InputFocusConnectionSerial)
        {
            m_PendingRouterInputFocus.clear();
        }
        m_PendingInputDeviceFocusEngine = GEngine;
        m_PendingInputDeviceFocusSerial = m_InputFocusConnectionSerial;
        if (notifyRouter)
        {
            m_PendingRouterInputFocus.push_back(focused);
        }
        m_HasPendingInputDeviceFocus = true;
        m_PendingInputDeviceFocus = focused;
        m_PendingInputDeviceFocusLoss = m_PendingInputDeviceFocusLoss || !focused;
        m_PendingInputDeviceFocusReset = m_PendingInputDeviceFocusReset || resetOperations;
        (void)ApplyPendingInputDeviceFocus();
    }
    bool ApplicationProcessor::ApplyPendingInputDeviceFocus()
    {
        if (!m_HasPendingInputDeviceFocus)
        {
            return true;
        }
        if (m_ApplyingInputDeviceFocus)
        {
            return false;
        }
        auto* engine = m_PendingInputDeviceFocusEngine;
        if (!GEngine || GEngine != engine)
        {
            m_HasPendingInputDeviceFocus = false;
            m_PendingInputDeviceFocusEngine = nullptr;
            m_PendingInputDeviceFocusLoss = false;
            m_PendingInputDeviceFocusReset = false;
            m_PendingRouterInputFocus.clear();
            return false;
        }
        if (!engine->CanUpdateInputDeviceFocus())
        {
            return false;
        }
        struct ApplyGuard
        {
            explicit ApplyGuard(bool& applying) : Applying(applying) { Applying = true; }
            ~ApplyGuard() { Applying = false; }
            bool& Applying;
        } guard(m_ApplyingInputDeviceFocus);
        // 対象batchをcallbackより先に取り出す。途中の新通知は次batchへ残す。
        const bool lostFocus = m_PendingInputDeviceFocusLoss;
        const bool targetFocus = m_PendingInputDeviceFocus;
        const bool reset = m_PendingInputDeviceFocusReset;
        const auto connectionSerial = m_PendingInputDeviceFocusSerial;
        Container::VariableArray<bool> routerFocus;
        routerFocus.swap(m_PendingRouterInputFocus);
        m_HasPendingInputDeviceFocus = false;
        m_PendingInputDeviceFocusEngine = nullptr;
        m_PendingInputDeviceFocusLoss = false;
        m_PendingInputDeviceFocusReset = false;
        // 同一poll中の喪失→復帰でも、一度は取消/zero/復帰baselineを通す。
        if (lostFocus)
        {
            (void)engine->SetInputDevicesFocused(false);
        }
        if (targetFocus || !lostFocus)
        {
            (void)engine->SetInputDevicesFocused(targetFocus);
        }
        if (reset)
        {
            // 喪失後に同じpollの残りslotから入った値も、安全な配送外で取り消す。
            engine->GetInputMapper().CancelAll();
            engine->GetInputSystem().ReleaseAll();
        }
        for (bool focused : routerFocus)
        {
            if (GEngine != engine || m_InputFocusConnectionSerial != connectionSerial)
            {
                break;
            }
            engine->GetInputRouter().NotifyInputFocusChanged(focused);
        }
        return !m_HasPendingInputDeviceFocus;
    }

    bool ApplicationProcessor::SynchronizeInputCursorMode()
    {
        if(!GEngine) return false;
        const auto window=GEngine->GetMainWindowShared();
        if(!window) return true;
        const bool success=window->SetCursorMode(GEngine->GetInputMapper().GetRequestedCursorMode());
        if(!success && !m_CursorFailureWarned) LOG_WARNING("カーソルmodeを適用できません。次frameで再試行します");
        m_CursorFailureWarned=!success;
        return success;
    }

    void ApplicationProcessor::DispatchInputFocusEvents()
    {
        if(!GEngine || GEngine!=m_InputEngine || !m_InputWindow ||
            GEngine->GetMainWindow()!=m_InputWindow.get())
        {
            m_PendingInputFocus.clear();return;
        }
        if(m_DispatchingInputFocus) return;
        struct DispatchGuard
        {
            bool& Active;
            explicit DispatchGuard(bool& active):Active(active) { Active=true; }
            ~DispatchGuard() { Active=false; }
        } guard(m_DispatchingInputFocus);
        const auto sourceWindow=m_InputWindow;
        auto* sourceEngine=m_InputEngine;
        const auto sourceSerial=m_InputFocusConnectionSerial;
        Container::VariableArray<bool> pending;
        pending.swap(m_PendingInputFocus);
        const auto handler=GEngine->GetApplicationHandlerShared();
        for(bool focused : pending)
        {
            if(!handler || !GEngine || GEngine!=sourceEngine || m_InputFocusConnectionSerial!=sourceSerial ||
                GEngine->GetMainWindow()!=sourceWindow.get()) break;
            if(focused) handler->OnFocusGained();
            else handler->OnFocusLost();
        }
    }

    bool ApplicationProcessor::ProcessPlatformMessages()
    {
        auto* sourceEngine=GEngine;
        auto* platformApp=sourceEngine ? sourceEngine->GetPlatformApp() : nullptr;
        bool exitRequested=false;int exitCode=0;
        if(platformApp)
        {
            platformApp->PumpMessages();
            exitRequested=platformApp->IsExitRequested();
            if(exitRequested) exitCode=platformApp->GetExitCode();
        }
        // focus通知中に生まれた次batchも、Handlerへ渡す前の安全地点で適用する。
        (void)ApplyPendingInputDeviceFocus();
        // Handlerはここでwindow/platformを破棄し得るので、以後借用platformへ触れない。
        DispatchInputFocusEvents();
        if(!GEngine || GEngine!=sourceEngine) return false;
        if(exitRequested)
        {
            GEngine->RequestExit(exitCode);return false;
        }
        return !GEngine->IsExitRequested();
    }

    void ApplicationProcessor::TickSimulationAndHaptics(int64_t rawDeltaNanoseconds, float deltaTime,
        Application::IApplicationHandler* handler)
    {
        if (!GEngine)
        {
            return;
        }
        (void)ApplyPendingInputDeviceFocus();
        // pause中も描画を維持する。gateは同frameで一度だけ取得する。
        const bool bAdvanceSim = handler ? handler->ShouldAdvanceSimulation() : true;
        // simulationが発行するPlayより前にpauseを反映し、停止はこの地点で送信する。
        (void)GEngine->SetHapticsPaused(!bAdvanceSim);
        TickSimulation(rawDeltaNanoseconds, deltaTime, bAdvanceSim, handler);
        const bool hapticsHealthy = UpdateHapticsFrame(rawDeltaNanoseconds);
        if (!hapticsHealthy && !m_HapticsFailureWarned)
        {
            LOG_WARNING("振動出力が未回復のerrorを報告しています");
        }
        m_HapticsFailureWarned = !hapticsHealthy;
    }

    bool ApplicationProcessor::UpdateHapticsFrame(int64_t rawDeltaNanoseconds)
    {
        if (!GEngine || rawDeltaNanoseconds < 0)
        {
            return false;
        }
        const bool healthy = GEngine->UpdateHaptics(static_cast<double>(rawDeltaNanoseconds) * 1e-9);
        (void)ApplyPendingInputDeviceFocus();
        return healthy;
    }

    FixedStepAdvanceResult ApplicationProcessor::TickSimulation(
        int64_t rawDeltaNanoseconds, float deltaTime, bool bAdvanceSimulation,
        Application::IApplicationHandler* handler)
    {
        if (bAdvanceSimulation)
        {
            GEngine->UpdateGameModeStateMachine(deltaTime);
            GEngine->GetWorld().Tick(deltaTime);
            GEngine->GetParticleSystem().Tick(deltaTime);
        }
        const FixedStepAdvanceResult result = AdvanceFixedSimulation(rawDeltaNanoseconds, bAdvanceSimulation);
        if (bAdvanceSimulation)
        {
            GEngine->GetWorld().LateTick(deltaTime);
            Module::GetModuleRegistry().DispatchLateTick(deltaTime);
            if (handler)
            {
                handler->OnLateUpdate(deltaTime);
            }
        }
        return result;
    }

    bool ApplicationProcessor::BeginInputFrame(int64_t timeNanoseconds)
    {
        if (!GEngine || timeNanoseconds < 0) return false;
        const double time = static_cast<double>(timeNanoseconds) / 1'000'000'000.0;
        if (!GEngine->GetInputSystem().CanBeginFrame(time))
        {
            return false;
        }
        if (!GEngine->GetInputMapper().BeginFrame(time)) return false;
        // Mapperの時刻検証成功後、message配送前に正本の前frame保存/累積解除も行う。
        if (!GEngine->GetInputSystem().BeginFrame(time))
        {
            return false;
        }
        GEngine->GetInputRebindCapture().BeginFrame();
        return true;
    }
    bool ApplicationProcessor::UpdateInputFrame(int64_t timeNanoseconds, int64_t rawDeltaNanoseconds)
    {
        if (!GEngine || timeNanoseconds < 0) return false;
        const double time = static_cast<double>(timeNanoseconds) / 1'000'000'000.0;
        const double dt = rawDeltaNanoseconds > 0
            ? static_cast<double>(rawDeltaNanoseconds) / 1'000'000'000.0 : 0.0;
        GEngine->GetInputDebugOverlay().Advance();
        GEngine->GetInputRebindCapture().Advance();
        return GEngine->GetInputMapper().Update(time, dt);
    }

    int64_t ApplicationProcessor::CalculateRawDeltaTimeNanoseconds()
    {
        const int64_t currentTimeNanoseconds = std::chrono::duration_cast<std::chrono::nanoseconds>(
            std::chrono::steady_clock::now().time_since_epoch()).count();
        const int64_t rawDeltaNanoseconds = currentTimeNanoseconds - m_LastFrameTimeNanoseconds;
        m_LastFrameTimeNanoseconds = currentTimeNanoseconds;
        return rawDeltaNanoseconds;
    }

    float ApplicationProcessor::ClampVariableDeltaTime(int64_t rawDeltaNanoseconds) const
    {
        if (rawDeltaNanoseconds <= 0)
        {
            return 0.0f;
        }

        constexpr int64_t MaximumVariableDeltaNanoseconds = 100'000'000;
        const int64_t clampedNanoseconds = rawDeltaNanoseconds < MaximumVariableDeltaNanoseconds
            ? rawDeltaNanoseconds
            : MaximumVariableDeltaNanoseconds;
        return static_cast<float>(clampedNanoseconds) / 1'000'000'000.0f;
    }

    FixedStepAdvanceResult ApplicationProcessor::AdvanceFixedSimulation(
        int64_t rawDeltaNanoseconds,
        bool bAdvanceSimulation)
    {
        FixedStepAdvanceResult result = m_FixedStepScheduler->Advance(
            rawDeltaNanoseconds,
            bAdvanceSimulation);
        if (result.Status != EFixedStepAdvanceStatus::Advanced || result.ExecutedSteps == 0)
        {
            return result;
        }

        constexpr float FixedDeltaTime = 1.0f / 60.0f;
        for (uint64_t step = 0; step < result.ExecutedSteps; ++step)
        {
            World& world = GEngine->GetWorld();
            world.UpdateWorldTransforms();
            Module::GetModuleRegistry().DispatchPreFixedTick(FixedDeltaTime);
            world.DispatchFixedTick(FixedDeltaTime);
            Module::GetModuleRegistry().DispatchFixedTick(FixedDeltaTime);
            world.UpdateWorldTransforms();
            world.CleanupAfterFixedStep();
        }

        return result;
    }

    bool ApplicationProcessor::CreateEngine()
    {
        if (GEngine)
        {
            LOG_WARNING("Engine already exists");
            return true;
        }

        GEngine = new Engine();
        LOG_INFO("Engine created successfully");
        return true;
    }

    void ApplicationProcessor::DestroyEngine()
    {
        if (GEngine)
        {
            delete GEngine;
            GEngine = nullptr;
            LOG_INFO("Engine destroyed");
        }
    }

    bool ApplicationProcessor::CreatePlatformApplication(const Boot::BootConfig &config)
    {
        auto platformApp = Platform::CreatePlatformApplication();
        if (!platformApp)
        {
            LOG_ERROR("Failed to create platform application");
            return false;
        }

        if (!platformApp->Initialize(config.Arguments))
        {
            LOG_ERROR("Failed to initialize platform application");
            return false;
        }

        GEngine->SetPlatformApp(std::move(platformApp));
        LOG_INFO("Platform application created and initialized");
        return true;
    }

    bool ApplicationProcessor::CreateMainWindow(const Boot::BootConfig &config)
    {
        auto window = Platform::CreatePlatformWindow();
        if (!window)
        {
            LOG_ERROR("Failed to create window");
            return false;
        }

        // ウィンドウタイトルをStringからstd::stringに変換（必要に応じて）
        if (!window->Create(config.WindowTitle, config.WindowWidth, config.WindowHeight))
        {
            LOG_ERROR("Failed to create window with specified parameters");
            return false;
        }

        window->Show();

        // プラットフォームアプリケーションにウィンドウを登録
        auto *platformApp = GEngine->GetPlatformApp();
        if (platformApp)
        {
            platformApp->RegisterWindow(window);
        }

        GEngine->SetMainWindow(window);
        LOG_INFO("Main window created successfully");
        return true;
    }

} // namespace NorvesLib::Core::Engine
