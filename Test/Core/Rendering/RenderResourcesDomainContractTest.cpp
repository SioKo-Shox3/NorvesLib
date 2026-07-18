#include "Rendering/RenderResources.h"
#include "Library/Core/Private/Rendering/TextureAssetRuntime.h"
#include "Library/Core/Private/Rendering/TextureAssetResolver.h"
#include "Library/Core/Private/Rendering/TextureAsyncLoadQueue.h"
#include "Resource/GLTFAnalyzer.h"
#include "Library/Core/Private/Rendering/RenderWorldAssetFlushPolicy.h"
#include "Container/String.h"
#include "RHI/IBuffer.h"
#include "RHI/ICommandList.h"
#include "RHI/IDevice.h"
#include "RHI/IFramebuffer.h"
#include "RHI/IPipeline.h"
#include "RHI/IRenderPass.h"
#include "RHI/ISampler.h"
#include "RHI/IShader.h"
#include "RHI/IShaderCompiler.h"
#include "RHI/ISwapChain.h"
#include "Thread/JobSystem.h"

#include <cassert>
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstring>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <mutex>
#include <thread>
#include <vector>
#if defined(_MSC_VER)
#include <crtdbg.h>
#endif

#undef assert
#define assert(expression)                                                                                             \
    do                                                                                                                 \
    {                                                                                                                  \
        if (!(expression))                                                                                             \
        {                                                                                                              \
            std::cerr << "Assertion failed: " << #expression << " at " << __FILE__ << ":" << __LINE__ << "\n";       \
            std::exit(1);                                                                                              \
        }                                                                                                              \
    } while (false)

using namespace NorvesLib::Core::Rendering;
using NorvesLib::Core::Container::MakeShared;
using NorvesLib::Core::Container::String;

namespace NorvesLib::Core::Resource
{
    struct GLTFAnalyzerShutdownTestAccess
    {
        static void Close()
        {
            GLTFAnalyzer::CloseAsyncAssetLoadAdmissionAndWait();
        }

        static void Reopen()
        {
            GLTFAnalyzer::ReopenAsyncAssetLoadAdmission();
        }

        static bool IsOpen()
        {
            return GLTFAnalyzer::IsAsyncAssetLoadAdmissionOpen();
        }
    };
}

namespace NorvesLib::Core::Rendering
{
    struct TextureAsyncLoadQueueShutdownTestAccess
    {
        static void SetWaitHook(TextureAsyncLoadQueue& queue, Delegate<void> hook)
        {
            queue.SetWaitHookForTesting(std::move(hook));
        }
    };

    struct TextureAssetRuntimeShutdownTestAccess
    {
        static TextureAssetRuntime* Get(RenderResources& resources)
        {
            return resources.GetTextureAssetRuntimeForTesting();
        }

        static void SetAdmissionCloseHook(TextureAssetRuntime& runtime, Delegate<void> hook)
        {
            runtime.m_AdmissionCloseHookForTesting = std::move(hook);
        }

        static void Close(TextureAssetRuntime& runtime)
        {
            runtime.CloseAndWait();
        }

        static void Reopen(TextureAssetRuntime& runtime)
        {
            runtime.Bind(runtime.m_pDevice, runtime.m_pGpuResources);
        }

        static void SetQueueWaitHook(TextureAssetRuntime& runtime, Delegate<void> hook)
        {
            TextureAsyncLoadQueueShutdownTestAccess::SetWaitHook(*runtime.m_TextureAsyncLoads, std::move(hook));
        }
    };
}

namespace
{
    String ReadTextFile(const std::filesystem::path& path)
    {
        std::ifstream input(path, std::ios::binary);
        assert(input.is_open());

        String contents;
        char character = '\0';
        while (input.get(character))
        {
            contents.push_back(static_cast<TCHAR>(static_cast<unsigned char>(character)));
        }
        return contents;
    }

    size_t FindMatchingBrace(const String& source, size_t openingBrace)
    {
        assert(openingBrace < source.size());
        assert(source[openingBrace] == _T('{'));

        size_t depth = 0;
        for (size_t index = openingBrace; index < source.size(); ++index)
        {
            if (source[index] == _T('{'))
            {
                ++depth;
            }
            else if (source[index] == _T('}'))
            {
                assert(depth > 0);
                --depth;
                if (depth == 0)
                {
                    return index;
                }
            }
        }

        assert(false);
        return String::npos;
    }

    String ReadFunctionSource(const String& source, const String& signature)
    {
        const size_t functionPosition = source.find(signature);
        assert(functionPosition != String::npos);

        const size_t openingBrace = source.find(_T('{'), functionPosition);
        assert(openingBrace != String::npos);
        const size_t closingBrace = FindMatchingBrace(source, openingBrace);
        return source.substr(functionPosition, closingBrace - functionPosition + 1);
    }

    size_t CountOccurrences(const String& source, const String& needle)
    {
        assert(!needle.empty());

        size_t count = 0;
        size_t position = 0;
        while ((position = source.find(needle, position)) != String::npos)
        {
            ++count;
            position += needle.size();
        }
        return count;
    }

    size_t FindMatchingPreprocessorEnd(const String& source, size_t openingDirective)
    {
        assert(openingDirective < source.size());

        size_t depth = 0;
        size_t lineStart = openingDirective;
        while (lineStart < source.size())
        {
            const size_t lineEnd = source.find(_T('\n'), lineStart);
            const size_t lineLength = (lineEnd == String::npos ? source.size() : lineEnd) - lineStart;
            const String line = source.substr(lineStart, lineLength);
            if (line.find(_T("#if")) == 0)
            {
                ++depth;
            }
            else if (line.find(_T("#endif")) == 0)
            {
                assert(depth > 0);
                --depth;
                if (depth == 0)
                {
                    return lineEnd == String::npos ? source.size() : lineEnd;
                }
            }

            if (lineEnd == String::npos)
            {
                break;
            }
            lineStart = lineEnd + 1;
        }

        assert(false);
        return String::npos;
    }

    bool HasRenderThreadStartPublicationReset(const String& source)
    {
        const size_t statsResetPosition = source.find(_T("m_Stats = ThreadStats{};"));
        const size_t publishedResetPosition = source.find(
            _T("m_PublishedFramesRendered.Store(0, std::memory_order_release);"));
        const size_t threadStartPosition = source.find(_T("m_Thread = Container::MakeUnique<Thread::Thread>"));
        return statsResetPosition != String::npos &&
               publishedResetPosition != String::npos &&
               threadStartPosition != String::npos &&
               statsResetPosition < publishedResetPosition &&
               publishedResetPosition < threadStartPosition;
    }

    bool HasRenderedFramePublication(const String& source)
    {
        const String framesRendered = _T("m_Stats.FramesRendered++;");
        const String publishedFrames = _T("m_PublishedFramesRendered.Store(m_Stats.FramesRendered, std::memory_order_release);");
        const size_t resumedLogPosition = source.find(
            _T("stage=asset_gpu_flush_window_resumed role=render_thread window_id=%llu ready_frames=%llu frames_rendered=%llu success=1"));
        if (resumedLogPosition == String::npos ||
            CountOccurrences(source, framesRendered) != 2 ||
            CountOccurrences(source, publishedFrames) != 2)
        {
            return false;
        }

        size_t incrementPosition = source.find(framesRendered);
        while (incrementPosition != String::npos)
        {
            const size_t publicationPosition = source.find(publishedFrames, incrementPosition + framesRendered.size());
            const size_t nextIncrementPosition = source.find(framesRendered, incrementPosition + framesRendered.size());
            if (publicationPosition == String::npos ||
                publicationPosition >= resumedLogPosition ||
                (nextIncrementPosition != String::npos && publicationPosition >= nextIncrementPosition))
            {
                return false;
            }
            incrementPosition = nextIncrementPosition;
        }
        return true;
    }

    bool HasRenderWorldInitializeStatsReset(const String& source)
    {
        const size_t statsResetPosition = source.find(_T("m_Stats = RenderingStats{};"));
        const size_t coordinatorInitializePosition = source.find(_T("m_RenderingCoordinator.Initialize(coordSettings)"));
        return statsResetPosition != String::npos &&
               coordinatorInitializePosition != String::npos &&
               statsResetPosition < coordinatorInitializePosition;
    }

    void AssertRenderWorldAssetFlushWiring()
    {
        std::filesystem::path repositoryRoot = std::filesystem::path(__FILE__).parent_path();
        for (uint32_t level = 0; level < 3; ++level)
        {
            repositoryRoot = repositoryRoot.parent_path();
        }

        const String renderWorldSource = ReadTextFile(
            repositoryRoot / "Library/Core/Private/Rendering/RenderWorld.cpp");
        const String beginFrameSource = ReadFunctionSource(
            renderWorldSource, _T("void RenderWorld::BeginFrame()"));
        const String pendingAssetsSource = ReadFunctionSource(
            renderWorldSource, _T("bool RenderWorld::HasPendingAsyncAssets() const"));
        const String endFrameSource = ReadFunctionSource(
            renderWorldSource, _T("void RenderWorld::EndFrame()"));
        const String waitForRenderSource = ReadFunctionSource(
            renderWorldSource, _T("void RenderWorld::WaitForRender()"));

        const String initializeSource = ReadFunctionSource(
            renderWorldSource, _T("bool RenderWorld::Initialize(const RenderWorldSettings &settings)"));

        assert(HasRenderWorldInitializeStatsReset(initializeSource));
        assert(!HasRenderWorldInitializeStatsReset(_T("m_RenderingCoordinator.Initialize(coordSettings)")));

        const size_t texturePendingPosition = pendingAssetsSource.find(
            _T("m_RenderResources.Textures().GetPendingAsyncLoadCount()"));
        const size_t cookedModelPendingPosition = pendingAssetsSource.find(
            _T("m_RenderResources.MegaGeometry().GetPendingAsyncModelLoadCount()"));
        const size_t gltfPendingPosition = pendingAssetsSource.find(
            _T("Resource::GLTFAnalyzer::GetPendingAsyncModelLoadCount()"));
        const size_t pendingAssetsCallPosition = beginFrameSource.find(_T("HasPendingAsyncAssets()"));
        const size_t decisionPosition = beginFrameSource.find(_T("Detail::DecideAssetGpuFlushAction("));
        assert(texturePendingPosition != String::npos);
        assert(cookedModelPendingPosition != String::npos);
        assert(gltfPendingPosition != String::npos);
        assert(decisionPosition != String::npos);
        assert(pendingAssetsCallPosition != String::npos);
        assert(CountOccurrences(beginFrameSource, _T("Detail::DecideAssetGpuFlushAction(")) == 1);
        assert(CountOccurrences(beginFrameSource, _T("HasPendingAsyncAssets()")) == 1);

        const size_t acquirePosition = beginFrameSource.find(
            _T("m_RenderThread.TryAcquireAssetGpuFlushWindow()"));
        const size_t flushGuardPosition = beginFrameSource.find(
            _T("assetFlushAction == Detail::AssetGpuFlushAction::FlushAndRender"));
        assert(acquirePosition != String::npos);
        assert(flushGuardPosition != String::npos);
        assert(pendingAssetsCallPosition < acquirePosition);
        assert(acquirePosition < decisionPosition);
        assert(pendingAssetsCallPosition < decisionPosition);
        assert(decisionPosition < flushGuardPosition);

        const size_t flushGuardOpeningBrace = beginFrameSource.find(_T('{'), flushGuardPosition);
        assert(flushGuardOpeningBrace != String::npos);
        const size_t flushGuardClosingBrace = FindMatchingBrace(beginFrameSource, flushGuardOpeningBrace);
        const size_t textureFlushPosition = beginFrameSource.find(
            _T("FlushCompletedTextureLoads()"), flushGuardPosition);
        const size_t cookedModelFlushPosition = beginFrameSource.find(
            _T("MegaGeometry().FlushCompletedModelLoads(1)"), flushGuardPosition);
        const size_t gltfFlushPosition = beginFrameSource.find(
            _T("Resource::GLTFAnalyzer::FlushCompletedModelLoads(modelLoadContext)"), flushGuardPosition);
        assert(textureFlushPosition != String::npos);
        assert(cookedModelFlushPosition != String::npos);
        assert(gltfFlushPosition != String::npos);
        assert(flushGuardOpeningBrace < textureFlushPosition);
        assert(textureFlushPosition < cookedModelFlushPosition);
        assert(cookedModelFlushPosition < gltfFlushPosition);
        assert(gltfFlushPosition < flushGuardClosingBrace);

        const size_t coordinatorBeginFramePosition = beginFrameSource.find(
            _T("m_RenderingCoordinator.BeginFrame();"));
        const size_t skipRenderGuardPosition = beginFrameSource.find(
            _T("assetFlushAction != Detail::AssetGpuFlushAction::DeferFlushAndSkipRender"));
        assert(coordinatorBeginFramePosition != String::npos);
        assert(skipRenderGuardPosition != String::npos);
        assert(flushGuardClosingBrace < coordinatorBeginFramePosition);
        assert(skipRenderGuardPosition < coordinatorBeginFramePosition);

        const size_t resizeWaitPosition = beginFrameSource.find(_T("WaitForRender();"));
        const size_t resizeQuiescedPosition = beginFrameSource.find(
            _T("bRenderThreadQuiesced = true;"), resizeWaitPosition);
        assert(beginFrameSource.find(_T("bool bRenderThreadQuiesced = false;")) != String::npos);
        assert(resizeWaitPosition != String::npos);
        assert(resizeQuiescedPosition != String::npos);
        assert(resizeWaitPosition < resizeQuiescedPosition);
        assert(resizeQuiescedPosition < decisionPosition);
        assert(CountOccurrences(beginFrameSource, _T("WaitForRender();")) == 1);
        assert(beginFrameSource.find(
                   _T("m_RenderThread.IsRunning(), bRenderThreadQuiesced, bAssetGpuFlushWindowAcquired,")) !=
               String::npos);
        assert(beginFrameSource.find(_T("bHasPendingAsyncAssets);"), decisionPosition) != String::npos);

        assert(beginFrameSource.find(_T("NotifyNewFrame(")) == String::npos);
        assert(endFrameSource.find(_T("m_RenderThread.NotifyNewFrame(packet);")) != String::npos);
        assert(waitForRenderSource.find(_T("m_RenderThread.WaitForIdle();")) != String::npos);
        assert(waitForRenderSource.find(_T("m_Device->WaitIdle();")) != String::npos);
    }

    void AssertRenderThreadAssetGpuFlushWindowWiring()
    {
        std::filesystem::path repositoryRoot = std::filesystem::path(__FILE__).parent_path();
        for (uint32_t level = 0; level < 3; ++level)
        {
            repositoryRoot = repositoryRoot.parent_path();
        }

        const String renderThreadHeader = ReadTextFile(
            repositoryRoot / "Library/Core/Public/Rendering/RenderThread.h");
        const String renderThreadSource = ReadTextFile(
            repositoryRoot / "Library/Core/Private/Rendering/RenderThread.cpp");
        const String acquireSource = ReadFunctionSource(
            renderThreadSource, _T("bool RenderThread::TryAcquireAssetGpuFlushWindow()"));
        const String startSource = ReadFunctionSource(
            renderThreadSource, _T("void RenderThread::Start()"));
        const String stopSource = ReadFunctionSource(
            renderThreadSource, _T("void RenderThread::Stop()"));
        const String waitForIdleSource = ReadFunctionSource(
            renderThreadSource, _T("void RenderThread::WaitForIdle()"));
        const String waitForFrameSource = ReadFunctionSource(
            renderThreadSource, _T("void RenderThread::WaitForFrame()"));
        const String notifyNewFrameSource = ReadFunctionSource(
            renderThreadSource, _T("void RenderThread::NotifyNewFrame(FramePacket* packet)"));
        const String renderLoopSource = ReadFunctionSource(
            renderThreadSource, _T("void RenderThread::RenderLoop()"));

        assert(HasRenderThreadStartPublicationReset(startSource));
        assert(!HasRenderThreadStartPublicationReset(
            _T("m_Stats = ThreadStats{}; m_Thread = Container::MakeUnique<Thread::Thread>")));
        assert(HasRenderedFramePublication(renderLoopSource));
        assert(!HasRenderedFramePublication(
            _T("m_Stats.FramesRendered++; m_Stats.FramesRendered++; stage=asset_gpu_flush_window_resumed role=render_thread window_id=%llu ready_frames=%llu frames_rendered=%llu success=1")));

        assert(renderThreadHeader.find(_T("friend class RenderWorld;")) != String::npos);
        assert(renderThreadHeader.find(_T("bool TryAcquireAssetGpuFlushWindow();")) != String::npos);
        assert(renderThreadHeader.find(_T("m_bAssetGpuFlushWindowRequested")) != String::npos);
        assert(renderThreadHeader.find(_T("m_bAssetGpuFlushWindowReady")) != String::npos);
        assert(renderThreadHeader.find(_T("m_bReportAssetGpuFlushWindowResume")) != String::npos);

        for (const String* lifecycleSource : {&startSource, &stopSource})
        {
            assert(lifecycleSource->find(_T("m_bAssetGpuFlushWindowRequested = false;")) != String::npos);
            assert(lifecycleSource->find(_T("m_bAssetGpuFlushWindowReady = false;")) != String::npos);
            assert(lifecycleSource->find(_T("m_bReportAssetGpuFlushWindowResume = false;")) != String::npos);
            assert(lifecycleSource->find(_T("m_AssetGpuFlushWindowId = 0;")) != String::npos);
            assert(lifecycleSource->find(_T("m_AssetGpuFlushWindowReadyFrames = 0;")) != String::npos);
        }

        assert(acquireSource.find(_T("m_FrameMutex.TryLock()")) != String::npos);
        assert(CountOccurrences(acquireSource, _T("m_FrameMutex.Unlock();")) == 2);
        assert(acquireSource.find(_T("m_bFrameComplete.Load(std::memory_order_acquire)")) != String::npos);
        assert(acquireSource.find(_T("!m_bNewFrameReady.Load(std::memory_order_acquire)")) != String::npos);
        assert(acquireSource.find(_T("m_CurrentPacket == nullptr")) != String::npos);
        assert(acquireSource.find(_T("m_bAssetGpuFlushWindowReady = false;")) != String::npos);
        assert(acquireSource.find(_T("m_bReportAssetGpuFlushWindowResume = true;")) != String::npos);
        assert(acquireSource.find(_T("m_bAssetGpuFlushWindowRequested = true;")) != String::npos);
        assert(acquireSource.find(_T("m_FrameCondition.NotifyOne();")) != String::npos);
        assert(acquireSource.find(_T("Wait(")) == String::npos);
        assert(acquireSource.find(_T("WaitIdle(")) == String::npos);

        assert(waitForFrameSource.find(_T("!m_bAssetGpuFlushWindowRequested")) != String::npos);
        assert(waitForFrameSource.find(_T("m_bShouldExit.Load(std::memory_order_acquire)")) != String::npos);
        assert(waitForIdleSource.find(_T("WaitForFrame();")) != String::npos);
        assert(notifyNewFrameSource.find(_T("m_bAssetGpuFlushWindowReady = false;")) != String::npos);

        const size_t frameCompletePredicatePosition = waitForFrameSource.find(
            _T("m_bFrameComplete.Load(std::memory_order_acquire)"));
        const size_t newFramePredicatePosition = waitForFrameSource.find(
            _T("!m_bNewFrameReady.Load(std::memory_order_acquire)"));
        const size_t packetPredicatePosition = waitForFrameSource.find(_T("m_CurrentPacket == nullptr"));
        const size_t flushWindowPredicatePosition = waitForFrameSource.find(
            _T("!m_bAssetGpuFlushWindowRequested"));
        const size_t exitPredicatePosition = waitForFrameSource.find(
            _T("m_bShouldExit.Load(std::memory_order_acquire)"));
        assert(frameCompletePredicatePosition != String::npos);
        assert(newFramePredicatePosition != String::npos);
        assert(packetPredicatePosition != String::npos);
        assert(flushWindowPredicatePosition != String::npos);
        assert(exitPredicatePosition != String::npos);
        assert(frameCompletePredicatePosition < newFramePredicatePosition);
        assert(newFramePredicatePosition < packetPredicatePosition);
        assert(packetPredicatePosition < flushWindowPredicatePosition);
        assert(flushWindowPredicatePosition < exitPredicatePosition);

        const size_t selectionPosition = renderLoopSource.find(_T("FramePacket* packet = nullptr;"));
        const size_t newFramePosition = renderLoopSource.find(
            _T("if (m_bNewFrameReady.Load(std::memory_order_acquire))"), selectionPosition);
        const size_t requestPosition = renderLoopSource.find(
            _T("else if (m_bAssetGpuFlushWindowRequested)"), newFramePosition);
        const size_t drainBranchPosition = renderLoopSource.find(_T("if (bDrainAssetGpuFlushWindow)"));
        const size_t waitIdlePosition = renderLoopSource.find(_T("device->WaitIdle();"), drainBranchPosition);
        const size_t readyPosition = renderLoopSource.find(_T("m_bAssetGpuFlushWindowReady = true;"), drainBranchPosition);
        const size_t readyLogPosition = renderLoopSource.find(
            _T("stage=asset_gpu_flush_window_ready role=render_thread window_id=%llu frames_rendered=%llu success=1"),
            drainBranchPosition);
        const size_t drainNotifyPosition = renderLoopSource.find(
            _T("m_IdleCondition.NotifyAll();"), drainBranchPosition);
        const size_t drainContinuePosition = renderLoopSource.find(_T("continue;"), drainBranchPosition);
        assert(selectionPosition != String::npos);
        assert(newFramePosition != String::npos);
        assert(requestPosition != String::npos);
        assert(newFramePosition < requestPosition);
        assert(drainBranchPosition != String::npos);
        assert(waitIdlePosition != String::npos);
        assert(readyPosition != String::npos);
        assert(readyLogPosition != String::npos);
        assert(drainNotifyPosition != String::npos);
        assert(drainContinuePosition != String::npos);
        assert(drainBranchPosition < waitIdlePosition);
        assert(waitIdlePosition < readyPosition);
        assert(readyPosition < readyLogPosition);
        assert(readyLogPosition < drainNotifyPosition);
        assert(drainNotifyPosition < drainContinuePosition);

        const String drainBranchSource = renderLoopSource.substr(
            drainBranchPosition, drainContinuePosition - drainBranchPosition);
        assert(drainBranchSource.find(_T("FramesRendered++")) == String::npos);
        assert(drainBranchSource.find(_T("m_Stats.FramesRendered =")) == String::npos);

        const size_t packetBranchPosition = renderLoopSource.find(_T("if (m_Coordinator && packet)"));
        const size_t packetBranchOpeningBrace = renderLoopSource.find(_T('{'), packetBranchPosition);
        const size_t packetBranchClosingBrace = FindMatchingBrace(renderLoopSource, packetBranchOpeningBrace);
        const size_t renderFramePosition = renderLoopSource.find(_T("m_Coordinator->RenderFrame(packet);"), packetBranchPosition);
        const size_t releasePacketPosition = renderLoopSource.find(_T("m_Coordinator->ReleasePacket(packet);"), renderFramePosition);
        const size_t framesRenderedPosition = renderLoopSource.find(_T("m_Stats.FramesRendered++;"), renderFramePosition);
        const size_t resumedLogPosition = renderLoopSource.find(
            _T("stage=asset_gpu_flush_window_resumed role=render_thread window_id=%llu ready_frames=%llu frames_rendered=%llu success=1"),
            framesRenderedPosition);
        assert(packetBranchPosition != String::npos);
        assert(renderFramePosition != String::npos);
        assert(framesRenderedPosition != String::npos);
        assert(resumedLogPosition != String::npos);
        assert(packetBranchPosition < renderFramePosition);
        assert(renderFramePosition < framesRenderedPosition);
        assert(framesRenderedPosition < resumedLogPosition);
        assert(resumedLogPosition < packetBranchClosingBrace);
        assert(drainContinuePosition < packetBranchPosition);

        const size_t frameCompletePosition = renderLoopSource.find(
            _T("m_bFrameComplete.Store(true, std::memory_order_release);"), packetBranchClosingBrace);
        assert(releasePacketPosition != String::npos);
        assert(frameCompletePosition != String::npos);
        assert(releasePacketPosition < frameCompletePosition);
    }

    void AssertApplicationShutdownQuiescenceWiring()
    {
        std::filesystem::path repositoryRoot = std::filesystem::path(__FILE__).parent_path();
        for (uint32_t level = 0; level < 3; ++level)
        {
            repositoryRoot = repositoryRoot.parent_path();
        }

        const String applicationProcessorSource = ReadTextFile(
            repositoryRoot / "Library/Core/Private/Engine/ApplicationProcessor.cpp");
        const String renderWorldHeader = ReadTextFile(
            repositoryRoot / "Library/Core/Public/Rendering/RenderWorld.h");
        const String renderWorldSource = ReadTextFile(
            repositoryRoot / "Library/Core/Private/Rendering/RenderWorld.cpp");
        const String renderResourcesSource = ReadTextFile(
            repositoryRoot / "Library/Core/Private/Rendering/RenderResources.cpp");
        const String shutdownSource = ReadFunctionSource(
            applicationProcessorSource, _T("void ApplicationProcessor::Shutdown()"));
        const String renderWorldShutdownSource = ReadFunctionSource(
            renderWorldSource, _T("void RenderWorld::Shutdown()"));
        const String quiesceSource = ReadFunctionSource(
            renderWorldSource, _T("void RenderWorld::QuiesceAsyncAssetProducersAndWait()"));
        const String closeResourcesSource = ReadFunctionSource(
            renderResourcesSource, _T("void RenderResources::CloseAsyncAssetLoadAdmissionAndWait()"));

        const String waitForRenderCall = _T("GEngine->GetRenderWorld().WaitForRender();");
        const String quiesceCall = _T("GEngine->GetRenderWorld().QuiesceAsyncAssetProducersAndWait();");
        const String stopAcceptingCall = _T("Thread::JobSystem::Get().StopAcceptingTasks();");
        const String drainAcceptedCall = _T("Thread::JobSystem::Get().DrainAcceptedFiniteTasks();");
        const size_t handlerLookupPosition = shutdownSource.find(_T("auto *handler = GEngine->GetApplicationHandler();"));
        const size_t waitForRenderPosition = shutdownSource.find(waitForRenderCall);
        const size_t quiescePosition = shutdownSource.find(quiesceCall);
        const size_t stopAcceptingPosition = shutdownSource.find(stopAcceptingCall);
        const size_t drainAcceptedPosition = shutdownSource.find(drainAcceptedCall);
        const size_t preShutdownPosition = shutdownSource.find(_T("handler->OnPreShutdown();"));
        const size_t stateMachineShutdownPosition = shutdownSource.find(_T("stateMachine->Shutdown();"));
        const size_t sceneQueryClearPosition = shutdownSource.find(_T("GEngine->GetSceneQuery().Clear();"));
        const size_t worldFinalizePosition = shutdownSource.find(_T("GEngine->GetWorld().Finalize();"));
        const size_t renderWorldShutdownPosition = shutdownSource.find(_T("GEngine->GetRenderWorld().Shutdown();"));
        const size_t rhiTeardownPosition = shutdownSource.find(_T("m_Device.reset();"));
        const size_t destroyEnginePosition = shutdownSource.find(_T("DestroyEngine();"));
        const size_t jobSystemShutdownPosition = shutdownSource.find(_T("Thread::JobSystem::Get().Shutdown();"));
        const size_t legacyClosePosition = quiesceSource.find(
            _T("Resource::GLTFAnalyzer::CloseAsyncAssetLoadAdmissionAndWait();"));
        const size_t resourcesClosePosition = quiesceSource.find(
            _T("m_RenderResources.CloseAsyncAssetLoadAdmissionAndWait();"));
        const size_t modelClosePosition = closeResourcesSource.find(_T("m_Impl->ModelAssets->CloseAndDrain();"));
        const size_t textureClosePosition = closeResourcesSource.find(_T("m_Impl->TextureAssets->CloseAndWait();"));

        assert(CountOccurrences(shutdownSource, waitForRenderCall) == 1);
        assert(CountOccurrences(shutdownSource, quiesceCall) == 1);
        assert(CountOccurrences(shutdownSource, stopAcceptingCall) == 1);
        assert(CountOccurrences(shutdownSource, drainAcceptedCall) == 1);
        assert(shutdownSource.find(_T("WaitForAll()")) == String::npos);
        assert(renderWorldHeader.find(_T("void QuiesceAsyncAssetProducersAndWait();")) != String::npos);
        assert(quiesceSource.find(_T("Resource::GLTFAnalyzer::CloseAsyncAssetLoadAdmissionAndWait();")) != String::npos);
        assert(quiesceSource.find(_T("m_RenderResources.CloseAsyncAssetLoadAdmissionAndWait();")) != String::npos);
        assert(renderWorldShutdownSource.find(_T("QuiesceAsyncAssetProducersAndWait();")) != String::npos);
        assert(closeResourcesSource.find(_T("m_Impl->ModelAssets->CloseAndDrain();")) != String::npos);
        assert(closeResourcesSource.find(_T("m_Impl->TextureAssets->CloseAndWait();")) != String::npos);
        assert(handlerLookupPosition != String::npos);
        assert(waitForRenderPosition != String::npos);
        assert(preShutdownPosition != String::npos);
        assert(stateMachineShutdownPosition != String::npos);
        assert(sceneQueryClearPosition != String::npos);
        assert(worldFinalizePosition != String::npos);
        assert(renderWorldShutdownPosition != String::npos);
        assert(rhiTeardownPosition != String::npos);
        assert(destroyEnginePosition != String::npos);
        assert(jobSystemShutdownPosition != String::npos);
        assert(legacyClosePosition < resourcesClosePosition);
        assert(modelClosePosition < textureClosePosition);
        assert(handlerLookupPosition < waitForRenderPosition);
        assert(waitForRenderPosition < quiescePosition);
        assert(quiescePosition < stopAcceptingPosition);
        assert(stopAcceptingPosition < drainAcceptedPosition);
        assert(drainAcceptedPosition < preShutdownPosition);
        assert(preShutdownPosition < stateMachineShutdownPosition);
        assert(stateMachineShutdownPosition < sceneQueryClearPosition);
        assert(sceneQueryClearPosition < worldFinalizePosition);
        assert(worldFinalizePosition < renderWorldShutdownPosition);
        assert(renderWorldShutdownPosition < rhiTeardownPosition);
        assert(rhiTeardownPosition < destroyEnginePosition);
        assert(destroyEnginePosition < jobSystemShutdownPosition);
    }

    void AssertAssetGpuFlushDecisionMatrix()
    {
        using NorvesLib::Core::Rendering::Detail::AssetGpuFlushAction;
        using NorvesLib::Core::Rendering::Detail::DecideAssetGpuFlushAction;

        assert(DecideAssetGpuFlushAction(false, false, false, true) == AssetGpuFlushAction::FlushAndRender);
        assert(DecideAssetGpuFlushAction(true, true, false, true) == AssetGpuFlushAction::FlushAndRender);
        assert(DecideAssetGpuFlushAction(true, false, false, false) ==
               AssetGpuFlushAction::DeferFlushAndRender);
        assert(DecideAssetGpuFlushAction(true, false, false, true) ==
               AssetGpuFlushAction::DeferFlushAndSkipRender);
        assert(DecideAssetGpuFlushAction(true, false, true, true) == AssetGpuFlushAction::FlushAndRender);

        AssetGpuFlushAction action = DecideAssetGpuFlushAction(true, false, false, true);
        assert(action == AssetGpuFlushAction::DeferFlushAndSkipRender);
        action = DecideAssetGpuFlushAction(true, false, true, true);
        assert(action == AssetGpuFlushAction::FlushAndRender);
    }

    void AssertVulkanWaitIdleTeardownWiring()
    {
        std::filesystem::path repositoryRoot = std::filesystem::path(__FILE__).parent_path();
        for (uint32_t level = 0; level < 3; ++level)
        {
            repositoryRoot = repositoryRoot.parent_path();
        }

        const String deviceSource = ReadTextFile(
            repositoryRoot / "Library/Core/Private/RHI/Vulkan/VulkanDevice.cpp");
        const String deviceHeaderSource = ReadTextFile(
            repositoryRoot / "Library/Core/Private/RHI/Vulkan/VulkanDevice.h");
        const String swapChainSource = ReadTextFile(
            repositoryRoot / "Library/Core/Private/RHI/Vulkan/VulkanSwapChain.cpp");
        const String commandListSource = ReadTextFile(
            repositoryRoot / "Library/Core/Private/RHI/Vulkan/VulkanCommandList.cpp");
        const String coreCMakeSource = ReadTextFile(repositoryRoot / "Library/Core/CMakeLists.txt");

        const String helperSource = ReadFunctionSource(
            deviceSource,
            _T("VkResult WaitIdleWithoutResultCheck(vk::Device device, const char* context) noexcept"));
        const String deviceDestructorSource = ReadFunctionSource(deviceSource, _T("VulkanDevice::~VulkanDevice()"));
        const String publicWaitIdleSource = ReadFunctionSource(deviceSource, _T("void VulkanDevice::WaitIdle()"));
        const String createLogicalDeviceSource = ReadFunctionSource(
            deviceSource,
            _T("void VulkanDevice::CreateLogicalDevice()"));
        const String getDeviceExtensionsSource = ReadFunctionSource(
            deviceSource,
            _T("VariableArray<const char *> VulkanDevice::GetDeviceExtensions()"));
        const String deviceFaultReportSource = ReadFunctionSource(
            deviceSource,
            _T("void VulkanDevice::ReportDeviceFaultOnce()"));
        const String swapChainDestructorSource = ReadFunctionSource(
            swapChainSource,
            _T("VulkanSwapChain::~VulkanSwapChain()"));
        const String resizeSource = ReadFunctionSource(
            swapChainSource,
            _T("void VulkanSwapChain::Resize(uint32_t width, uint32_t height)"));
        const String commandListDestructorSource = ReadFunctionSource(
            commandListSource,
            _T("VulkanCommandList::~VulkanCommandList()"));

        assert(helperSource.find(_T("if (!device)")) != String::npos);
        assert(helperSource.find(_T("return VK_SUCCESS;")) != String::npos);
        assert(helperSource.find(_T("const VkResult result = VULKAN_HPP_DEFAULT_DISPATCHER.vkDeviceWaitIdle(")) !=
               String::npos);
        assert(helperSource.find(_T("static_cast<VkDevice>(device)")) != String::npos);
        assert(helperSource.find(_T("if (result != VK_SUCCESS)")) != String::npos);
        assert(helperSource.find(
                   _T("NORVES_LOG_ERROR(\"Vulkan\", \"vkDeviceWaitIdle failed context=%s result=%d\", context, static_cast<int32_t>(result));")) !=
               String::npos);
        assert(helperSource.find(_T("return result;")) != String::npos);

        assert(deviceSource.find(_T("VULKAN_HPP_DEFAULT_DISPATCHER.init(m_device);")) != String::npos);
        assert(CountOccurrences(deviceSource, _T("WaitIdleWithoutResultCheck(")) == 2);
        assert(publicWaitIdleSource.find(_T("const VkResult result = WaitIdleWithoutResultCheck(m_device, \"VulkanDevice::WaitIdle\");")) !=
               String::npos);
        assert(publicWaitIdleSource.find(_T("if (result == VK_ERROR_DEVICE_LOST)")) != String::npos);
        assert(publicWaitIdleSource.find(_T("ReportDeviceFaultOnce();")) != String::npos);
        assert(deviceDestructorSource.find(_T("if (m_device)")) != String::npos);
        assert(deviceDestructorSource.find(_T("WaitIdle();")) != String::npos);

        assert(deviceHeaderSource.find(_T("#include \"Thread/Atomic.h\"")) != String::npos);
        assert(deviceHeaderSource.find(_T("VkPhysicalDeviceFaultFeaturesEXT m_deviceFaultFeatures")) != String::npos);
        assert(deviceHeaderSource.find(_T("Thread::Atomic<bool> m_bDeviceFaultReported")) != String::npos);
        assert(deviceHeaderSource.find(_T("void ReportDeviceFaultOnce();")) != String::npos);

        const size_t exchangePosition = deviceFaultReportSource.find(
            _T("if (m_bDeviceFaultReported.Exchange(true))"));
        const size_t featureGuardPosition = deviceFaultReportSource.find(
            _T("if (m_deviceFaultFeatures.deviceFault != VK_TRUE)"));
        const size_t procLookupPosition = deviceFaultReportSource.find(
            _T("const PFN_vkGetDeviceFaultInfoEXT getDeviceFaultInfo ="));
        const size_t procGuardPosition = deviceFaultReportSource.find(
            _T("if (getDeviceFaultInfo == nullptr)"));
        const size_t totalCallPosition = deviceFaultReportSource.find(
            _T("getDeviceFaultInfo(static_cast<VkDevice>(m_device), &totalCounts, nullptr);"));
        const size_t totalCountsPosition = deviceFaultReportSource.find(_T("totalAddressInfoCount"));
        const size_t detailsCallPosition = deviceFaultReportSource.find(
            _T("getDeviceFaultInfo(static_cast<VkDevice>(m_device), &writtenCounts, &faultInfo);"));
        assert(exchangePosition != String::npos);
        assert(featureGuardPosition != String::npos);
        assert(procLookupPosition != String::npos);
        assert(procGuardPosition != String::npos);
        assert(totalCallPosition != String::npos);
        assert(totalCountsPosition != String::npos);
        assert(detailsCallPosition != String::npos);
        assert(exchangePosition < featureGuardPosition);
        assert(featureGuardPosition < procLookupPosition);
        assert(procLookupPosition < procGuardPosition);
        assert(procGuardPosition < totalCallPosition);
        assert(totalCallPosition < totalCountsPosition);
        assert(totalCountsPosition < detailsCallPosition);
        assert(deviceFaultReportSource.find(_T("VK_STRUCTURE_TYPE_DEVICE_FAULT_COUNTS_EXT")) != String::npos);
        assert(deviceFaultReportSource.find(_T("VK_STRUCTURE_TYPE_DEVICE_FAULT_INFO_EXT")) != String::npos);
        assert(deviceFaultReportSource.find(_T("FixedArray<VkDeviceFaultAddressInfoEXT, 16>")) != String::npos);
        assert(deviceFaultReportSource.find(_T("FixedArray<VkDeviceFaultVendorInfoEXT, 16>")) != String::npos);
        assert(deviceFaultReportSource.find(_T("constexpr uint32_t maxCapturedFaultInfos = 16;")) != String::npos);
        assert(deviceFaultReportSource.find(_T("std::min(totalAddressInfoCount, maxCapturedFaultInfos)")) != String::npos);
        assert(deviceFaultReportSource.find(_T("std::min(totalVendorInfoCount, maxCapturedFaultInfos)")) != String::npos);
        assert(deviceFaultReportSource.find(_T("totalVendorInfoCount")) != String::npos);
        assert(deviceFaultReportSource.find(_T("totalVendorBinarySize")) != String::npos);
        assert(deviceFaultReportSource.find(_T("writtenCounts")) != String::npos);
        assert(deviceFaultReportSource.find(_T("writtenCounts.vendorBinarySize = 0;")) != String::npos);
        assert(deviceFaultReportSource.find(_T("pVendorBinaryData = nullptr")) != String::npos);
        assert(deviceFaultReportSource.find(_T("if (totalResult != VK_SUCCESS && totalResult != VK_INCOMPLETE)")) !=
               String::npos);
        assert(deviceFaultReportSource.find(_T("if (detailsResult != VK_SUCCESS && detailsResult != VK_INCOMPLETE)")) !=
               String::npos);
        assert(deviceFaultReportSource.find(_T("while (")) == String::npos);

        const size_t extensionGuardPosition = getDeviceExtensionsSource.find(
            _T("if (hasExtension(VK_EXT_DEVICE_FAULT_EXTENSION_NAME))"));
        const size_t extensionPushPosition = getDeviceExtensionsSource.find(
            _T("extensions.push_back(VK_EXT_DEVICE_FAULT_EXTENSION_NAME);"));
        assert(extensionGuardPosition != String::npos);
        assert(extensionPushPosition != String::npos);
        assert(extensionGuardPosition < extensionPushPosition);

        const size_t deviceFaultRequestedPosition = createLogicalDeviceSource.find(
            _T("bool bDeviceFaultRequested = false;"));
        const size_t deviceFaultQueryGuardPosition = createLogicalDeviceSource.find(
            _T("if (bDeviceFaultRequested)"));
        const size_t deviceFaultQueryPosition = createLogicalDeviceSource.find(
            _T("m_physicalDevice.getFeatures2(&deviceFaultFeatures2Query);"));
        const size_t featuresTailPosition = createLogicalDeviceSource.find(
            _T("void **featuresTail = &m_vulkan12Features.pNext;"));
        const size_t deviceFaultFeatureGuardPosition = createLogicalDeviceSource.find(
            _T("if (m_deviceFaultFeatures.deviceFault == VK_TRUE)"));
        const size_t deviceFaultTailPosition = createLogicalDeviceSource.find(
            _T("*featuresTail = &m_deviceFaultFeatures;"));
        const size_t cooperativeVectorGuardPosition = createLogicalDeviceSource.find(
            _T("if (bCooperativeVectorRequested)"));
        const size_t cooperativeVectorTailPosition = createLogicalDeviceSource.find(
            _T("*featuresTail = &m_cooperativeVectorFeatures;"));
        assert(deviceFaultRequestedPosition != String::npos);
        assert(deviceFaultQueryGuardPosition != String::npos);
        assert(deviceFaultQueryPosition != String::npos);
        assert(featuresTailPosition != String::npos);
        assert(deviceFaultFeatureGuardPosition != String::npos);
        assert(deviceFaultTailPosition != String::npos);
        assert(cooperativeVectorGuardPosition != String::npos);
        assert(cooperativeVectorTailPosition != String::npos);
        assert(deviceFaultRequestedPosition < deviceFaultQueryGuardPosition);
        assert(deviceFaultQueryGuardPosition < deviceFaultQueryPosition);
        assert(deviceFaultQueryPosition < featuresTailPosition);
        assert(featuresTailPosition < deviceFaultFeatureGuardPosition);
        assert(deviceFaultFeatureGuardPosition < deviceFaultTailPosition);
        assert(deviceFaultTailPosition < cooperativeVectorGuardPosition);
        assert(cooperativeVectorGuardPosition < cooperativeVectorTailPosition);

        assert(swapChainDestructorSource.find(_T("m_device->WaitIdle();")) != String::npos);
        assert(resizeSource.find(_T("m_device->WaitIdle();")) != String::npos);
        assert(commandListDestructorSource.find(_T("m_device->WaitIdle();")) != String::npos);

        assert(publicWaitIdleSource.find(_T(".waitIdle(")) == String::npos);
        assert(deviceDestructorSource.find(_T(".waitIdle(")) == String::npos);
        assert(swapChainDestructorSource.find(_T(".waitIdle(")) == String::npos);
        assert(resizeSource.find(_T(".waitIdle(")) == String::npos);
        assert(commandListDestructorSource.find(_T(".waitIdle(")) == String::npos);
        assert(deviceSource.find(_T("m_graphicsQueue.waitIdle();")) != String::npos);

        assert(coreCMakeSource.find(_T("VULKAN_HPP_ASSERT_ON_RESULT")) == String::npos);
        assert(coreCMakeSource.find(_T("VULKAN_HPP_DISABLE_ENHANCED_MODE")) == String::npos);
    }

    void AssertVulkanAddressBindingDiagnosticsWiring()
    {
        std::filesystem::path repositoryRoot = std::filesystem::path(__FILE__).parent_path();
        for (uint32_t level = 0; level < 3; ++level)
        {
            repositoryRoot = repositoryRoot.parent_path();
        }

        const String deviceSource = ReadTextFile(
            repositoryRoot / "Library/Core/Private/RHI/Vulkan/VulkanDevice.cpp");
        const String deviceHeaderSource = ReadTextFile(
            repositoryRoot / "Library/Core/Private/RHI/Vulkan/VulkanDevice.h");
        const String bufferSource = ReadTextFile(
            repositoryRoot / "Library/Core/Private/RHI/Vulkan/VulkanBuffer.cpp");
        const String textureSource = ReadTextFile(
            repositoryRoot / "Library/Core/Private/RHI/Vulkan/VulkanTexture.cpp");
        const String createInstanceSource = ReadFunctionSource(deviceSource, _T("void VulkanDevice::CreateInstance()"));
        const String constructorSource = ReadFunctionSource(deviceSource, _T("VulkanDevice::VulkanDevice(bool bEnableValidation)"));
        const String setupDebugMessengerSource = ReadFunctionSource(
            deviceSource,
            _T("void VulkanDevice::SetupDebugMessenger()"));
        const size_t setupAddressBindingDebugMessengerSourcePosition = deviceSource.find(
            _T("void VulkanDevice::SetupAddressBindingDebugMessenger()"));
        const String setupAddressBindingDebugMessengerSource =
            setupAddressBindingDebugMessengerSourcePosition == String::npos
                ? String()
                : ReadFunctionSource(
                    deviceSource,
                    _T("void VulkanDevice::SetupAddressBindingDebugMessenger()"));
        const String createLogicalDeviceSource = ReadFunctionSource(
            deviceSource,
            _T("void VulkanDevice::CreateLogicalDevice()"));
        const String getDeviceExtensionsSource = ReadFunctionSource(
            deviceSource,
            _T("VariableArray<const char *> VulkanDevice::GetDeviceExtensions()"));
        const String debugCallbackSource = ReadFunctionSource(
            deviceSource,
            _T("VKAPI_ATTR VkBool32 VKAPI_CALL VulkanDevice::DebugCallback("));
        const String findAddressBindingCallbackDataSource = ReadFunctionSource(
            deviceSource,
            _T("const VkDeviceAddressBindingCallbackDataEXT *VulkanDevice::FindAddressBindingCallbackData("));
        const String destructorSource = ReadFunctionSource(deviceSource, _T("VulkanDevice::~VulkanDevice()"));
        const String bufferCreateSource = ReadFunctionSource(
            bufferSource,
            _T("void VulkanBuffer::CreateBuffer(vk::BufferUsageFlags usage, vk::MemoryPropertyFlags properties)"));
        const String bufferUpdateSource = ReadFunctionSource(
            bufferSource,
            _T("void VulkanBuffer::Update(const void *data, uint64_t size, uint64_t offset)"));
        const String ownedTextureCreateSource = ReadFunctionSource(textureSource, _T("void VulkanTexture::CreateTexture()"));
        const String externalTextureConstructorSource = ReadFunctionSource(
            textureSource,
            _T("VulkanTexture::VulkanTexture(TSharedPtr<VulkanDevice> device, const TextureDesc &desc, vk::Image image)"));
        const String textureUpdateSource = ReadFunctionSource(
            textureSource,
            _T("void VulkanTexture::Update(const void *data, uint32_t rowPitch, uint32_t slicePitch,"));

        assert(deviceHeaderSource.find(_T("VkPhysicalDeviceAddressBindingReportFeaturesEXT m_addressBindingReportFeatures")) != String::npos);
        assert(deviceHeaderSource.find(_T("FileStream::FileStreamPtr m_addressBindingDiagnosticsSink")) != String::npos);
        assert(deviceHeaderSource.find(_T("Thread::Mutex m_addressBindingDiagnosticsMutex")) != String::npos);
        assert(deviceHeaderSource.find(_T("void InitializeAddressBindingDiagnostics();")) != String::npos);
        assert(deviceHeaderSource.find(_T("void ShutdownAddressBindingDiagnostics();")) != String::npos);
        assert(deviceHeaderSource.find(_T("void RecordAddressBindingDiagnostic(")) != String::npos);
        assert(deviceHeaderSource.find(_T("void SetupAddressBindingDebugMessenger();")) != String::npos);
        assert(deviceHeaderSource.find(_T("vk::DebugUtilsMessengerEXT m_addressBindingDebugMessenger;")) != String::npos);
        assert(deviceHeaderSource.find(_T("void SetDebugObjectName(")) != String::npos);
        const size_t headerAddressBindingGuardPosition = deviceHeaderSource.find(
            _T("#if defined(VK_EXT_device_address_binding_report)"));
        const size_t headerAddressBindingGuardEnd = FindMatchingPreprocessorEnd(
            deviceHeaderSource,
            headerAddressBindingGuardPosition);
        const size_t headerSetDebugObjectNamePosition = deviceHeaderSource.find(_T("void SetDebugObjectName("));
        assert(headerAddressBindingGuardPosition != String::npos);
        assert(headerSetDebugObjectNamePosition > headerAddressBindingGuardEnd);

        const size_t sourceAddressBindingGuardPosition = deviceSource.find(
            _T("#if defined(VK_EXT_device_address_binding_report)"),
            deviceSource.find(_T("return Format::UNKNOWN;")));
        const size_t sourceAddressBindingGuardEnd = FindMatchingPreprocessorEnd(
            deviceSource,
            sourceAddressBindingGuardPosition);
        const size_t sourceSetDebugObjectNamePosition = deviceSource.find(
            _T("void VulkanDevice::SetDebugObjectName("));
        assert(sourceAddressBindingGuardPosition != String::npos);
        assert(sourceSetDebugObjectNamePosition > sourceAddressBindingGuardEnd);

        assert(getDeviceExtensionsSource.find(_T("m_bValidationEnabled && hasExtension(VK_EXT_DEVICE_ADDRESS_BINDING_REPORT_EXTENSION_NAME)")) != String::npos);
        assert(getDeviceExtensionsSource.find(_T("extensions.push_back(VK_EXT_DEVICE_ADDRESS_BINDING_REPORT_EXTENSION_NAME);")) != String::npos);
        assert(createLogicalDeviceSource.find(_T("bool bAddressBindingReportRequested = false;")) != String::npos);
        assert(createLogicalDeviceSource.find(_T("VkPhysicalDeviceAddressBindingReportFeaturesEXT addressBindingReportFeaturesQuery")) != String::npos);
        assert(createLogicalDeviceSource.find(_T("m_addressBindingReportFeatures")) != String::npos);
        assert(createLogicalDeviceSource.find(_T("*featuresTail = &m_addressBindingReportFeatures;")) != String::npos);
        assert(createLogicalDeviceSource.find(_T("featuresTail = &m_addressBindingReportFeatures.pNext;")) != String::npos);
        assert(createLogicalDeviceSource.find(_T("createInfo.enabledExtensionCount = static_cast<uint32_t>(extensions.size());")) != String::npos);
        assert(createLogicalDeviceSource.find(_T("createInfo.ppEnabledExtensionNames = extensions.data();")) != String::npos);
        assert(createLogicalDeviceSource.find(_T("*featuresTail = &m_deviceFaultFeatures;")) <
               createLogicalDeviceSource.find(_T("*featuresTail = &m_cooperativeVectorFeatures;")));
        assert(createLogicalDeviceSource.find(_T("*featuresTail = &m_cooperativeVectorFeatures;")) <
               createLogicalDeviceSource.find(_T("*featuresTail = &m_addressBindingReportFeatures;")));
        assert(createLogicalDeviceSource.find(_T("*featuresTail = &m_addressBindingReportFeatures;")) <
               createLogicalDeviceSource.find(_T("*featuresTail = nullptr;")));

        assert(createInstanceSource.find(_T("eInfo |")) == String::npos);
        assert(createInstanceSource.find(_T("eDeviceAddressBinding")) == String::npos);
        assert(createInstanceSource.find(_T("pUserData")) == String::npos);
        const size_t setupDebugMessengerPosition = constructorSource.find(_T("SetupDebugMessenger();"));
        const size_t pickPhysicalDevicePosition = constructorSource.find(_T("PickPhysicalDevice();"));
        const size_t createLogicalDevicePosition = constructorSource.find(_T("CreateLogicalDevice();"));
        const size_t setupAddressBindingDebugMessengerPosition = constructorSource.find(
            _T("SetupAddressBindingDebugMessenger();"));
        const size_t initializeAddressBindingDiagnosticsPosition = constructorSource.find(
            _T("InitializeAddressBindingDiagnostics();"));
        const size_t resourceAllocatorPosition = constructorSource.find(_T("m_ResourceAllocator ="));
        assert(setupDebugMessengerPosition != String::npos);
        assert(setupDebugMessengerPosition < pickPhysicalDevicePosition);
        assert(pickPhysicalDevicePosition < createLogicalDevicePosition);
        assert(setupDebugMessengerSource.find(_T("vk::DebugUtilsMessageTypeFlagBitsEXT::eGeneral")) != String::npos);
        assert(setupDebugMessengerSource.find(_T("vk::DebugUtilsMessageTypeFlagBitsEXT::eValidation")) != String::npos);
        assert(setupDebugMessengerSource.find(_T("vk::DebugUtilsMessageTypeFlagBitsEXT::ePerformance")) != String::npos);
        assert(setupDebugMessengerSource.find(_T("vk::DebugUtilsMessageTypeFlagBitsEXT::eDeviceAddressBinding")) == String::npos);
        assert(setupDebugMessengerSource.find(_T("createInfo.pUserData = this;")) != String::npos);
        assert(setupDebugMessengerSource.find(_T("m_addressBindingReportFeatures")) == String::npos);
        assert(setupAddressBindingDebugMessengerSourcePosition != String::npos);
        assert(setupAddressBindingDebugMessengerPosition != String::npos);
        assert(initializeAddressBindingDiagnosticsPosition != String::npos);
        assert(resourceAllocatorPosition != String::npos);
        assert(createLogicalDevicePosition < setupAddressBindingDebugMessengerPosition);
        assert(setupAddressBindingDebugMessengerPosition < initializeAddressBindingDiagnosticsPosition);
        assert(initializeAddressBindingDiagnosticsPosition < resourceAllocatorPosition);
        assert(setupAddressBindingDebugMessengerSource.find(
            _T("vk::DebugUtilsMessageSeverityFlagBitsEXT::eInfo")) != String::npos);
        assert(setupAddressBindingDebugMessengerSource.find(
            _T("vk::DebugUtilsMessageTypeFlagBitsEXT::eDeviceAddressBinding")) != String::npos);
        assert(setupAddressBindingDebugMessengerSource.find(
            _T("vk::DebugUtilsMessageTypeFlagBitsEXT::eGeneral")) == String::npos);
        assert(setupAddressBindingDebugMessengerSource.find(
            _T("vk::DebugUtilsMessageTypeFlagBitsEXT::eValidation")) == String::npos);
        assert(setupAddressBindingDebugMessengerSource.find(
            _T("vk::DebugUtilsMessageTypeFlagBitsEXT::ePerformance")) == String::npos);
        assert(setupAddressBindingDebugMessengerSource.find(_T("createInfo.pUserData = this;")) != String::npos);
        assert(debugCallbackSource.find(_T("try")) != String::npos);
        assert(debugCallbackSource.find(_T("catch (...)")) != String::npos);
        assert(debugCallbackSource.find(_T("VK_DEBUG_UTILS_MESSAGE_TYPE_DEVICE_ADDRESS_BINDING_BIT_EXT")) != String::npos);
        assert(debugCallbackSource.find(_T("VkDeviceAddressBindingCallbackDataEXT")) != String::npos);
        assert(findAddressBindingCallbackDataSource.find(_T("pNext")) != String::npos);
        assert(findAddressBindingCallbackDataSource.find(_T("while (pNext != nullptr)")) != String::npos);
        assert(debugCallbackSource.find(_T("return VK_FALSE;")) != String::npos);
        assert(debugCallbackSource.find(_T("m_device")) == String::npos);
        assert(debugCallbackSource.find(_T("vkGet")) == String::npos);
        assert(debugCallbackSource.find(_T("std::cerr")) != String::npos);
        assert(debugCallbackSource.find(_T("NORVES_LOG_WARNING")) == String::npos);

        assert(deviceSource.find(_T("schema=vulkan_device_address_binding_v1")) != String::npos);
        assert(deviceSource.find(_T("event_id=")) != String::npos);
        assert(deviceSource.find(_T("binding=")) != String::npos);
        assert(deviceSource.find(_T("base=0x")) != String::npos);
        assert(deviceSource.find(_T("size=0x")) != String::npos);
        assert(deviceSource.find(_T("end=0x")) != String::npos);
        assert(deviceSource.find(_T("internal_object=")) != String::npos);
        assert(deviceSource.find(_T("object_count=")) != String::npos);
        assert(deviceSource.find(_T("type_name=")) != String::npos);
        assert(deviceSource.find(_T("<unnamed>")) != String::npos);
        assert(deviceSource.find(_T("NORVES_ASSET_DIR")) != String::npos);
        assert(deviceSource.find(_T("assetDirectory.substr")) != String::npos);
        assert(deviceSource.find(_T("std::filesystem::create_directories")) != String::npos);
        assert(deviceSource.find(_T("std::error_code")) != String::npos);
        assert(deviceSource.find(_T("std::filesystem::create_directories")) <
               deviceSource.find(_T("FileStream::FileStream::Create(")));
        assert(deviceSource.find(_T("NorvesLib::Thread::ScopedLock lock(m_addressBindingDiagnosticsMutex);")) != String::npos);
        assert(CountOccurrences(deviceSource, _T("NorvesLib::Thread::ScopedLock lock(m_addressBindingDiagnosticsMutex);")) == 2);
        assert(deviceSource.find(_T("m_addressBindingDiagnosticsSink->Close();")) != String::npos);
        assert(deviceSource.find(_T("m_addressBindingDiagnosticsSink.reset();")) != String::npos);
        assert(destructorSource.find(_T("ShutdownAddressBindingDiagnostics();")) != String::npos);
        const size_t deviceDestroyPosition = destructorSource.find(_T("m_device.destroy();"));
        const size_t shutdownAddressBindingDiagnosticsPosition = destructorSource.find(
            _T("ShutdownAddressBindingDiagnostics();"));
        const size_t addressBindingMessengerDestroyPosition = destructorSource.find(
            _T("destroyDebugUtilsMessengerEXT(m_addressBindingDebugMessenger)"));
        const size_t debugMessengerDestroyPosition = destructorSource.find(
            _T("destroyDebugUtilsMessengerEXT(m_debugMessenger)"));
        assert(deviceDestroyPosition != String::npos);
        assert(shutdownAddressBindingDiagnosticsPosition != String::npos);
        assert(addressBindingMessengerDestroyPosition != String::npos);
        assert(debugMessengerDestroyPosition != String::npos);
        assert(deviceDestroyPosition < shutdownAddressBindingDiagnosticsPosition);
        assert(shutdownAddressBindingDiagnosticsPosition < addressBindingMessengerDestroyPosition);
        assert(addressBindingMessengerDestroyPosition < debugMessengerDestroyPosition);

        assert(bufferCreateSource.find(_T("SetDebugObjectName(")) != String::npos);
        assert(bufferCreateSource.find(_T("m_desc.DebugName")) != String::npos);
        assert(ownedTextureCreateSource.find(_T("SetDebugObjectName(")) != String::npos);
        assert(ownedTextureCreateSource.find(_T("m_desc.DebugName")) != String::npos);
        assert(externalTextureConstructorSource.find(_T("SetDebugObjectName(")) != String::npos);
        assert(externalTextureConstructorSource.find(_T("m_desc.DebugName")) != String::npos);
        assert(bufferUpdateSource.find(_T("stagingDesc.DebugName = \"VulkanBuffer.Update.Staging\";")) != String::npos);
        assert(textureUpdateSource.find(_T("VulkanTexture.Update.Staging")) != String::npos);
        assert(bufferUpdateSource.find(_T("m_desc.DebugName")) == String::npos);
        assert(textureUpdateSource.find(_T("m_desc.DebugName")) == String::npos);
    }

    class FakeTexture final : public NorvesLib::RHI::ITexture
    {
    public:
        explicit FakeTexture(const NorvesLib::RHI::TextureDesc &desc)
            : Desc(desc)
        {
        }

        uint32_t GetWidth() const override { return Desc.Width; }
        uint32_t GetHeight() const override { return Desc.Height; }
        uint32_t GetDepth() const override { return Desc.Depth; }
        uint32_t GetMipLevels() const override { return Desc.MipLevels; }
        uint32_t GetArraySize() const override { return Desc.ArraySize; }
        NorvesLib::RHI::Format GetFormat() const override { return Desc.TextureFormat; }
        NorvesLib::RHI::ResourceUsage GetUsage() const override { return Desc.Usage; }
        bool IsCubemap() const override { return Desc.IsCubemap; }

        void Update(const void *data,
                    uint32_t rowPitch,
                    uint32_t slicePitch,
                    uint32_t mipLevel = 0,
                    uint32_t arrayIndex = 0) override
        {
            (void)data;
            (void)rowPitch;
            (void)slicePitch;
            (void)mipLevel;
            (void)arrayIndex;
        }

        NorvesLib::RHI::TextureDesc Desc;
    };

    class FakeBuffer final : public NorvesLib::RHI::IBuffer
    {
    public:
        explicit FakeBuffer(const NorvesLib::RHI::BufferDesc &desc)
            : Desc(desc),
              Bytes(static_cast<size_t>(desc.Size))
        {
        }

        uint64_t GetSize() const override { return Desc.Size; }

        void *Map(uint64_t offset = 0, uint64_t size = 0) override
        {
            (void)size;
            return offset < Bytes.size() ? Bytes.data() + offset : nullptr;
        }

        void Unmap() override {}

        void Update(const void *data, uint64_t size, uint64_t offset = 0) override
        {
            LastUpdateSize = size;
            LastUpdateOffset = offset;
            if (data == nullptr || offset + size > Bytes.size())
            {
                return;
            }

            std::memcpy(Bytes.data() + offset, data, static_cast<size_t>(size));
        }

        NorvesLib::RHI::ResourceUsage GetUsage() const override { return Desc.Usage; }

        NorvesLib::RHI::BufferDesc Desc;
        std::vector<uint8_t> Bytes;
        uint64_t LastUpdateSize = 0;
        uint64_t LastUpdateOffset = 0;
    };

    class FakeSampler final : public NorvesLib::RHI::ISampler
    {
    public:
        explicit FakeSampler(const NorvesLib::RHI::SamplerDesc &desc)
            : Desc(desc)
        {
        }

        NorvesLib::RHI::FilterMode GetFilterMin() const override { return Desc.filterMin; }
        NorvesLib::RHI::FilterMode GetFilterMag() const override { return Desc.filterMag; }
        NorvesLib::RHI::FilterMode GetFilterMip() const override { return Desc.filterMip; }
        NorvesLib::RHI::TextureAddressMode GetAddressModeU() const override { return Desc.addressU; }
        NorvesLib::RHI::TextureAddressMode GetAddressModeV() const override { return Desc.addressV; }
        NorvesLib::RHI::TextureAddressMode GetAddressModeW() const override { return Desc.addressW; }
        uint32_t GetMaxAnisotropy() const override { return Desc.maxAnisotropy; }
        NorvesLib::RHI::CompareFunc GetCompareFunc() const override { return Desc.compareFunc; }

        NorvesLib::RHI::SamplerDesc Desc;
    };

    class FakeDevice final : public NorvesLib::RHI::IDevice
    {
    public:
        NorvesLib::RHI::BufferPtr CreateBuffer(const NorvesLib::RHI::BufferDesc &desc) override
        {
            CreatedBufferDescs.push_back(desc);
            LastBuffer = MakeShared<FakeBuffer>(desc);
            return LastBuffer;
        }

        NorvesLib::RHI::TexturePtr CreateTexture(const NorvesLib::RHI::TextureDesc &desc) override
        {
            CreatedTextureDescs.push_back(desc);
            LastTexture = MakeShared<FakeTexture>(desc);
            return LastTexture;
        }

        NorvesLib::RHI::SamplerPtr CreateSampler(const NorvesLib::RHI::SamplerDesc &desc) override
        {
            CreatedSamplerDescs.push_back(desc);
            LastSampler = MakeShared<FakeSampler>(desc);
            return LastSampler;
        }

        NorvesLib::RHI::ShaderPtr CreateShader(const NorvesLib::RHI::ShaderDesc &) override { return {}; }
        NorvesLib::RHI::CommandListPtr CreateCommandList() override { return {}; }
        NorvesLib::RHI::SwapChainPtr CreateSwapChain(const NorvesLib::RHI::SwapChainDesc &) override { return {}; }
        NorvesLib::RHI::RenderPassPtr CreateRenderPass(const NorvesLib::RHI::RenderPassDesc &) override { return {}; }
        NorvesLib::RHI::FramebufferPtr CreateFramebuffer(const NorvesLib::RHI::FramebufferDesc &) override { return {}; }
        NorvesLib::RHI::PipelinePtr CreateGraphicsPipeline(const NorvesLib::RHI::GraphicsPipelineDesc &) override { return {}; }
        NorvesLib::RHI::PipelinePtr CreateComputePipeline(const NorvesLib::RHI::ComputePipelineDesc &) override { return {}; }
        NorvesLib::RHI::DescriptorSetPtr CreateDescriptorSet(const NorvesLib::RHI::DescriptorSetDesc &) override { return {}; }
        NorvesLib::RHI::ShaderCompilerPtr CreateShaderCompiler() override { return {}; }
        NorvesLib::RHI::IGPUResourceAllocator* GetResourceAllocator() override { return nullptr; }
        void WaitIdle() override {}
        NorvesLib::RHI::API GetAPI() const override { return NorvesLib::RHI::API::None; }
        const NorvesLib::RHI::DeviceCapabilities &GetCapabilities() const override { return Capabilities; }
        NorvesLib::Math::Matrix4x4 AdjustProjectionForClipSpace(
            const NorvesLib::Math::Matrix4x4 &projection,
            bool bApplyYFlip = true) const override
        {
            (void)bApplyYFlip;
            return projection;
        }

        NorvesLib::RHI::DeviceCapabilities Capabilities;
        std::vector<NorvesLib::RHI::BufferDesc> CreatedBufferDescs;
        std::vector<NorvesLib::RHI::TextureDesc> CreatedTextureDescs;
        std::vector<NorvesLib::RHI::SamplerDesc> CreatedSamplerDescs;
        NorvesLib::Core::Container::TSharedPtr<FakeBuffer> LastBuffer;
        NorvesLib::Core::Container::TSharedPtr<FakeTexture> LastTexture;
        NorvesLib::Core::Container::TSharedPtr<FakeSampler> LastSampler;
    };

    TextureCreateInfo MakeTextureCreateInfo(const char *debugName)
    {
        TextureCreateInfo createInfo;
        createInfo.Width = 4;
        createInfo.Height = 2;
        createInfo.MipLevels = 1;
        createInfo.PixelFormat = TextureCreateInfo::Format::RGBA8_UNORM;
        createInfo.DebugName = debugName;
        return createInfo;
    }

    NorvesLib::RHI::TextureDesc MakeExternalTextureDesc()
    {
        NorvesLib::RHI::TextureDesc desc;
        desc.Width = 8;
        desc.Height = 4;
        desc.Depth = 1;
        desc.MipLevels = 1;
        desc.ArraySize = 1;
        desc.TextureFormat = NorvesLib::RHI::Format::R8G8B8A8_UNORM;
        desc.Usage = NorvesLib::RHI::ResourceUsage::ShaderRead;
        desc.DebugName = "ExternalTexture";
        return desc;
    }

    void TestLegacyGLTFAdmission(RenderResources& manager)
    {
        using NorvesLib::Core::Resource::GLTFAnalyzer;
        using NorvesLib::Core::Resource::GLTFAnalyzerShutdownTestAccess;

        GLTFAnalyzerShutdownTestAccess::Reopen();
        assert(GLTFAnalyzerShutdownTestAccess::IsOpen());
        const ModelLoadResourceContext context{manager.Textures(), manager.MegaGeometry()};
        const uint32_t firstRequest = GLTFAnalyzer::LoadModelAsync("missing_shutdown_contract.gltf", context);
        assert(firstRequest != 0);
        GLTFAnalyzer::CancelModelLoad(firstRequest);
        assert(GLTFAnalyzerShutdownTestAccess::IsOpen());

        GLTFAnalyzerShutdownTestAccess::Close();
        assert(!GLTFAnalyzerShutdownTestAccess::IsOpen());
        assert(GLTFAnalyzer::GetPendingAsyncModelLoadCount() == 0);
        assert(GLTFAnalyzer::LoadModelAsync("missing_shutdown_contract.gltf", context) == 0);
        GLTFAnalyzerShutdownTestAccess::Close();

        GLTFAnalyzerShutdownTestAccess::Reopen();
        assert(GLTFAnalyzerShutdownTestAccess::IsOpen());
        const uint32_t reopenedRequest = GLTFAnalyzer::LoadModelAsync("missing_shutdown_reopen.gltf", context);
        assert(reopenedRequest != 0);
        GLTFAnalyzer::CancelModelLoad(reopenedRequest);
        GLTFAnalyzerShutdownTestAccess::Close();
        GLTFAnalyzerShutdownTestAccess::Reopen();
    }

    void TestTextureAdmission(RenderResources& manager)
    {
        TextureAssetRuntime* runtime =
            NorvesLib::Core::Rendering::TextureAssetRuntimeShutdownTestAccess::Get(manager);
        assert(runtime != nullptr);

        bool bAdmissionCloseHookRan = false;
        NorvesLib::Core::Rendering::TextureAssetRuntimeShutdownTestAccess::SetAdmissionCloseHook(
            *runtime,
            [&bAdmissionCloseHookRan]()
            {
                bAdmissionCloseHookRan = true;
            });
        NorvesLib::Core::Rendering::TextureAssetRuntimeShutdownTestAccess::Close(*runtime);
        assert(bAdmissionCloseHookRan);
        assert(manager.Textures().GetPendingAsyncLoadCount() == 0);
        assert(manager.Textures().LoadTextureAsync("missing_shutdown_contract.png") == 0);
        NorvesLib::Core::Rendering::TextureAssetRuntimeShutdownTestAccess::Close(*runtime);
        NorvesLib::Core::Rendering::TextureAssetRuntimeShutdownTestAccess::Reopen(*runtime);

        const uint32_t reopenedRequest = manager.Textures().LoadTextureAsync("missing_shutdown_reopen.png");
        assert(reopenedRequest != 0);
        NorvesLib::Core::Rendering::TextureAssetRuntimeShutdownTestAccess::Close(*runtime);
        NorvesLib::Core::Rendering::TextureAssetRuntimeShutdownTestAccess::Reopen(*runtime);
    }

    void TestTextureQueueCloseWaitHandshake()
    {
        using namespace std::chrono_literals;
        TextureAsyncLoadQueue queue;
        TextureAssetLoadPlan plan;
        plan.RequestPath = "queue_wait_handshake.png";
        plan.CacheKey = "queue_wait_handshake";
        auto request = queue.CreateRequest(
            plan,
            TextureAssetFallbackMode::FailOnCookedFailure,
            {});
        assert(request != nullptr);
        request->Task = NorvesLib::Thread::Task::Create([]() {});
        assert(queue.EnqueueOrAppendDuplicateAndSubmit(request).bSubmitted);
        request->Task->Cancel();
        request->Task->Wait();

        TextureAsyncLoadQueue::CompletedBatch batch = queue.DetachCompletedRequests();
        assert(batch.Requests.size() == 1);

        std::mutex gateMutex;
        std::condition_variable gateCondition;
        bool bWaitHookReached = false;
        std::atomic<bool> bCloseReturned{false};
        NorvesLib::Core::Rendering::TextureAsyncLoadQueueShutdownTestAccess::SetWaitHook(
            queue,
            [&gateMutex, &gateCondition, &bWaitHookReached]()
            {
                std::lock_guard<std::mutex> lock(gateMutex);
                bWaitHookReached = true;
                gateCondition.notify_all();
            });

        std::thread closeThread([&queue, &bCloseReturned]()
        {
            queue.CloseCancelAllAndWait();
            bCloseReturned.store(true, std::memory_order_release);
        });

        bool bHookObserved = false;
        {
            std::unique_lock<std::mutex> lock(gateMutex);
            bHookObserved = gateCondition.wait_for(lock, 2s, [&bWaitHookReached]()
            {
                return bWaitHookReached;
            });
        }
        if (!bHookObserved)
        {
            batch.Guard.Reset();
            closeThread.join();
            assert(bHookObserved);
        }
        assert(!bCloseReturned.load(std::memory_order_acquire));

        batch.Guard.Reset();
        closeThread.join();
        assert(bCloseReturned.load(std::memory_order_acquire));
        assert(queue.GetPendingCount() == 0);
    }

    void TestTextureProductionCallbackCloseWait(RenderResources& manager)
    {
        using namespace std::chrono_literals;
        TextureAssetRuntime* runtime =
            NorvesLib::Core::Rendering::TextureAssetRuntimeShutdownTestAccess::Get(manager);
        assert(runtime != nullptr);

        std::mutex gateMutex;
        std::condition_variable gateCondition;
        bool bCallbackEntered = false;
        bool bCloseAdmissionReached = false;
        bool bQueueWaitReached = false;
        bool bReleaseCallback = false;
        uint32_t lateRequestId = 99;
        std::atomic<bool> bCloseReturned{false};
        NorvesLib::Core::Rendering::TextureAssetRuntimeShutdownTestAccess::SetAdmissionCloseHook(
            *runtime,
            [&gateMutex, &gateCondition, &bCloseAdmissionReached]()
            {
                std::lock_guard<std::mutex> lock(gateMutex);
                bCloseAdmissionReached = true;
                gateCondition.notify_all();
            });
        NorvesLib::Core::Rendering::TextureAssetRuntimeShutdownTestAccess::SetQueueWaitHook(
            *runtime,
            [&gateMutex, &gateCondition, &bQueueWaitReached]()
            {
                std::lock_guard<std::mutex> lock(gateMutex);
                bQueueWaitReached = true;
                gateCondition.notify_all();
            });

        assert(manager.Textures().LoadTextureAsync(
                   "missing_texture_callback_close.png",
                   [&manager, &gateMutex, &gateCondition, &bCallbackEntered,
                    &bCloseAdmissionReached, &bReleaseCallback, &lateRequestId](TextureHandle)
                   {
                       std::unique_lock<std::mutex> lock(gateMutex);
                       bCallbackEntered = true;
                       gateCondition.notify_all();
                       gateCondition.wait(lock, [&bCloseAdmissionReached]()
                       {
                           return bCloseAdmissionReached;
                       });
                       lateRequestId = manager.Textures().LoadTextureAsync("late_texture_callback.png");
                       gateCondition.wait(lock, [&bReleaseCallback]()
                       {
                           return bReleaseCallback;
                       });
                   }) != 0);
        NorvesLib::Thread::JobSystem::Get().DrainAcceptedFiniteTasks();

        std::thread flushThread([&manager]()
        {
            manager.Textures().FlushCompletedTextureLoads();
        });
        bool bCallbackObserved = false;
        {
            std::unique_lock<std::mutex> lock(gateMutex);
            bCallbackObserved = gateCondition.wait_for(lock, 2s, [&bCallbackEntered]()
            {
                return bCallbackEntered;
            });
        }
        bool bWaitObserved = false;
        bool bCloseBlocked = false;
        std::thread closeThread;
        if (bCallbackObserved)
        {
            closeThread = std::thread([runtime, &bCloseReturned]()
            {
                NorvesLib::Core::Rendering::TextureAssetRuntimeShutdownTestAccess::Close(*runtime);
                bCloseReturned.store(true, std::memory_order_release);
            });
            {
                std::unique_lock<std::mutex> lock(gateMutex);
                bWaitObserved = gateCondition.wait_for(lock, 2s, [&bQueueWaitReached]()
                {
                    return bQueueWaitReached;
                });
            }
            bCloseBlocked = !bCloseReturned.load(std::memory_order_acquire);
        }

        {
            std::lock_guard<std::mutex> lock(gateMutex);
            bCloseAdmissionReached = true;
            bReleaseCallback = true;
            gateCondition.notify_all();
        }
        flushThread.join();
        if (closeThread.joinable())
        {
            closeThread.join();
        }

        assert(bCallbackObserved);
        assert(bWaitObserved);
        assert(lateRequestId == 0);
        assert(bCloseBlocked);
        assert(bCloseReturned.load(std::memory_order_acquire));
        assert(manager.Textures().GetPendingAsyncLoadCount() == 0);

        NorvesLib::Core::Rendering::TextureAssetRuntimeShutdownTestAccess::Reopen(*runtime);
        const uint32_t reopenedRequest = manager.Textures().LoadTextureAsync("reopen_texture_callback.png");
        assert(reopenedRequest != 0);
        NorvesLib::Core::Rendering::TextureAssetRuntimeShutdownTestAccess::Close(*runtime);
        NorvesLib::Core::Rendering::TextureAssetRuntimeShutdownTestAccess::Reopen(*runtime);
    }

    void TestTextureQueueRejectsAfterJobSystemStop()
    {
        TextureAsyncLoadQueue queue;
        TextureAssetLoadPlan plan;
        plan.RequestPath = "queue_rejected_after_stop.png";
        plan.CacheKey = "queue_rejected_after_stop";
        auto request = queue.CreateRequest(
            plan,
            TextureAssetFallbackMode::FailOnCookedFailure,
            {});
        assert(request != nullptr);
        request->Task = NorvesLib::Thread::Task::Create([]() {});

        NorvesLib::Thread::JobSystem::Get().StopAcceptingTasks();
        const TextureAsyncLoadQueue::EnqueueResult rejected = queue.EnqueueOrAppendDuplicateAndSubmit(request);
        assert(rejected.RequestId == 0);
        assert(!rejected.bSubmitted);
        assert(queue.GetPendingCount() == 0);
        TextureAsyncLoadQueue::Callback callback;
        assert(queue.TryAppendDuplicate(plan.CacheKey, callback) == 0);
        NorvesLib::Thread::JobSystem::Get().DrainAcceptedFiniteTasks();
        NorvesLib::Thread::JobSystem::Get().Shutdown();
    }
}

int main()
{
#if defined(_MSC_VER)
    _CrtSetReportMode(_CRT_ASSERT, _CRTDBG_MODE_FILE);
    _CrtSetReportFile(_CRT_ASSERT, _CRTDBG_FILE_STDERR);
#endif

    std::cout << "RenderResourcesDomainContractTest start\n";
    NorvesLib::Thread::JobSystem::Get().Initialize(2, NorvesLib::Thread::JobSystem::EXECUTION_SIMPLE);

    AssertAssetGpuFlushDecisionMatrix();
    AssertRenderWorldAssetFlushWiring();
    AssertRenderThreadAssetGpuFlushWindowWiring();
    AssertApplicationShutdownQuiescenceWiring();
    AssertVulkanWaitIdleTeardownWiring();
    AssertVulkanAddressBindingDiagnosticsWiring();

    RenderResources manager;
    const TextureCreateInfo createInfo = MakeTextureCreateInfo("OwnedTexture");
    const TextureHandle preInitializeHandle = manager.Textures().CreateTexture(createInfo);
    assert(!preInitializeHandle.IsValid());
    assert(manager.GetResourceStats().TextureCount == 0);

    auto device = MakeShared<FakeDevice>();
    assert(manager.Initialize(device));
    TestLegacyGLTFAdmission(manager);
    TestTextureAdmission(manager);
    TestTextureQueueCloseWaitHandshake();
    TestTextureProductionCallbackCloseWait(manager);

    BufferCreateInfo bufferInfo;
    bufferInfo.Size = 64;
    bufferInfo.bHostVisible = true;
    bufferInfo.UsageType = BufferCreateInfo::Usage::Vertex;
    bufferInfo.DebugName = "ContractBuffer";

    const uint32_t bufferData[4] = {1, 2, 3, 4};
    const BufferHandle bufferHandle = manager.Gpu().CreateBuffer(bufferInfo, bufferData, sizeof(bufferData));
    assert(bufferHandle.IsValid());
    assert(device->CreatedBufferDescs.size() == 1);
    assert(device->CreatedBufferDescs[0].Size == bufferInfo.Size);
    assert(device->CreatedBufferDescs[0].Usage == NorvesLib::RHI::ResourceUsage::VertexBuffer);
    assert(manager.GetResourceStats().BufferCount == 1);
    assert(manager.GetResourceStats().TotalBufferMemory == bufferInfo.Size);
    assert(manager.Gpu().GetRHIBuffer(bufferHandle) == device->LastBuffer.get());
    assert(device->LastBuffer->LastUpdateSize == sizeof(bufferData));

    const uint32_t updatedData[2] = {7, 8};
    assert(manager.Gpu().UpdateBuffer(bufferHandle, updatedData, sizeof(updatedData)));
    assert(device->LastBuffer->LastUpdateSize == sizeof(updatedData));

    manager.Gpu().ReleaseBuffer(BufferHandle::Invalid());
    assert(manager.GetResourceStats().BufferCount == 1);
    manager.Gpu().ReleaseBuffer(bufferHandle);
    assert(manager.GetResourceStats().BufferCount == 0);
    assert(manager.Gpu().GetRHIBuffer(bufferHandle) == nullptr);

    const SamplerHandle defaultSampler = manager.Gpu().GetDefaultSampler();
    assert(defaultSampler.IsValid());
    assert(device->CreatedSamplerDescs.size() == 1);
    assert(device->CreatedSamplerDescs[0].filterMin == NorvesLib::RHI::FilterMode::Anisotropic);
    assert(manager.GetResourceStats().SamplerCount == 1);
    assert(manager.Gpu().GetDefaultSampler().Id == defaultSampler.Id);
    assert(device->CreatedSamplerDescs.size() == 1);

    const SamplerHandle pointSampler = manager.Gpu().GetPointSampler();
    assert(pointSampler.IsValid());
    assert(device->CreatedSamplerDescs.size() == 2);
    assert(device->CreatedSamplerDescs[1].filterMin == NorvesLib::RHI::FilterMode::Point);
    assert(manager.GetResourceStats().SamplerCount == 2);
    manager.Gpu().ReleaseSampler(defaultSampler);
    assert(manager.GetResourceStats().SamplerCount == 1);
    manager.Gpu().ReleaseSampler(pointSampler);
    assert(manager.GetResourceStats().SamplerCount == 0);

    VertexLayout layout = VertexLayout::CreateStandard();
    const VertexLayoutHandle layoutHandle = manager.Gpu().RegisterVertexLayout(layout);
    assert(layoutHandle.IsValid());
    const VertexLayout *registeredLayout = manager.Gpu().GetVertexLayout(layoutHandle);
    assert(registeredLayout != nullptr);
    assert(registeredLayout->Stride == layout.Stride);
    assert(registeredLayout->HasSemantic(VertexSemantic::Position));
    assert(registeredLayout->HasSemantic(VertexSemantic::Normal));
    assert(manager.Gpu().GetVertexLayout(VertexLayoutHandle::Invalid()) == nullptr);

    const TextureHandle ownedHandle = manager.Textures().CreateTexture(createInfo);
    assert(ownedHandle.IsValid());
    assert(device->CreatedTextureDescs.size() == 1);
    assert(manager.GetResourceStats().TextureCount == 1);

    NorvesLib::RHI::ITexture *ownedRaw = manager.Textures().GetRHITexture(ownedHandle);
    auto ownedShared = manager.Textures().GetRHITexturePtr(ownedHandle);
    assert(ownedRaw != nullptr);
    assert(ownedShared);
    assert(ownedShared.get() == ownedRaw);
    assert(ownedRaw == device->LastTexture.get());

    manager.Textures().ReleaseTexture(TextureHandle::Invalid());
    assert(manager.GetResourceStats().TextureCount == 1);

    manager.Textures().ReleaseTexture(ownedHandle);
    assert(manager.GetResourceStats().TextureCount == 0);
    assert(manager.Textures().GetRHITexture(ownedHandle) == nullptr);
    assert(!manager.Textures().GetRHITexturePtr(ownedHandle));

    auto externalTexture = MakeShared<FakeTexture>(MakeExternalTextureDesc());
    const TextureHandle externalHandle = manager.Textures().RegisterExternalTexture(externalTexture, "ExternalTexture");
    assert(externalHandle.IsValid());
    assert(manager.GetResourceStats().TextureCount == 1);
    assert(manager.Textures().GetRHITexture(externalHandle) == externalTexture.get());
    assert(manager.Textures().GetRHITexturePtr(externalHandle).get() == externalTexture.get());

    manager.Textures().ReleaseTexture(externalHandle);
    assert(manager.GetResourceStats().TextureCount == 0);
    assert(manager.Textures().GetRHITexture(externalHandle) == nullptr);

    const TextureHandle shutdownHandle = manager.Textures().CreateTexture(createInfo);
    assert(shutdownHandle.IsValid());
    assert(manager.GetResourceStats().TextureCount == 1);
    manager.Shutdown();
    assert(manager.GetResourceStats().TextureCount == 0);
    assert(manager.Textures().GetRHITexture(shutdownHandle) == nullptr);
    assert(!manager.Textures().GetRHITexturePtr(shutdownHandle));

    TestTextureQueueRejectsAfterJobSystemStop();

    std::cout << "RenderResourcesDomainContractTest passed\n";
    return 0;
}
