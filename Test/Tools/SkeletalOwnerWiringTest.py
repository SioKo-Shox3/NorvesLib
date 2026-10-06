"""実owner配線のsource境界。native helper試験やGame描画の代用ではない。"""
from pathlib import Path
import re
import unittest

ROOT = Path(__file__).resolve().parents[2]


def read(path):
    return (ROOT / path).read_text(encoding="utf-8-sig")


def block(source, signature):
    match = re.search(signature, source)
    if not match:
        raise ValueError("declaration missing: " + signature)
    start = source.find("{", match.end())
    if start < 0:
        raise ValueError("body missing")
    depth = 0
    quote = None
    line = False
    comment = False
    i = start
    while i < len(source):
        ch = source[i]
        pair = source[i:i + 2]
        if line:
            line = ch != "\n"
        elif comment:
            if pair == "*/":
                comment = False
                i += 1
        elif quote:
            if ch == "\\":
                i += 1
            elif ch == quote:
                quote = None
        elif pair == "//":
            line = True
            i += 1
        elif pair == "/*":
            comment = True
            i += 1
        elif ch in "\"'":
            quote = ch
        elif ch == "{":
            depth += 1
        elif ch == "}":
            depth -= 1
            if depth == 0:
                return source[start:i + 1]
        i += 1
    raise ValueError("unclosed body")


class OwnerWiring(unittest.TestCase):
    def ordered(self, source, *tokens):
        cursor = 0
        for token in tokens:
            found = source.find(token, cursor)
            self.assertGreaterEqual(found, 0, token)
            cursor = found + len(token)

    def test_begin_and_shutdown_real_processor(self):
        source = read("Library/Core/Private/Engine/ApplicationProcessor.cpp")
        initialize = block(source, r"bool ApplicationProcessor::Initialize\(")
        self.ordered(initialize, "GetSkeletalAssetSession().IsActive()", "GApplicationLifecycleState = {}",
                     "JobSystem::Get().Initialize()", "CreateEngine()", "bSkeletalSession = true",
                     "GetSkeletalAssetSession().Begin(", "handler->OnPreInitialize")
        shutdown = block(source, r"void ApplicationProcessor::Shutdown\(")
        self.ordered(shutdown, "skeletal.Close()", "skeletal.Drain()", "SkeletalRuntimeStatus::Deferred",
                     "DisconnectInputWindow()", "WaitForRender()", "StopAcceptingTasks()", "handler->OnPreShutdown()",
                     "stateMachine->Shutdown()", "GetWorld().Finalize()", "GetRenderWorld().Shutdown()",
                     "handler->OnShutdown()", "SetApplicationHandler(nullptr)", "GetSkeletalAssetSession().End()",
                     "DestroyEngine()", "JobSystem::Get().Shutdown()")
        deferred = block(shutdown, r"if \(drained == SkeletalRuntimeStatus::Deferred\)")
        self.ordered(deferred, "RequestExit()", "return;")

    def test_tick_before_simulation_and_settle(self):
        source = read("Library/Core/Private/Engine/ApplicationProcessor.cpp")
        tick = block(source, r"void ApplicationProcessor::Tick\(")
        self.ordered(tick, "HasPendingSkeletalConsumers(skeletalSession, handler)",
                     "TickSkeletalOwnerAssetsAndHandler(skeletalSession, handler, deltaTime)",
                     "TickSimulationAndHaptics", "SyncToSceneView", "EvaluateSettledRenderedExit")
        self.assertEqual(tick.count("HasPendingSkeletalConsumers(skeletalSession, handler)"), 3)
        helper = block(read("Library/Core/Private/Engine/SkeletalAssetSession.cpp"),
                       r"SkeletalFlushResult TickSkeletalOwnerAssetsAndHandler\(")
        self.ordered(helper, "session.Flush()", "result.Status != Status::Success", "handler->OnUpdate(deltaTime)")

    def test_owned_borrowed_and_receipt(self):
        source = read("Library/Core/Private/Engine/SkeletalAssetSession.cpp")
        begin = block(source, r"Status SkeletalAssetSession::Begin\(")
        self.ordered(begin, "registry.GetResourceCount()", "m_pRegistry = &registry", "m_bOwnsRegistry = true",
                     "registry.Initialize()", "m_PrepareHook", "MakeUnique<SkeletalAssetRuntime>")
        end = block(source, r"bool SkeletalAssetSession::End\(")
        self.ordered(end, "!m_bClosed || !m_bDrained", "m_Runtime.reset()", "m_Snapshot.reset()",
                     "if (m_bOwnsRegistry)", "m_pRegistry->Shutdown()", "ResetReceipt()")
        self.assertNotIn("JobSystem::Get()", source)
        self.assertIn("SkeletalAssetSession m_SkeletalAssetSession", read("Library/Core/Public/Engine/NorvesEngine.h"))

    def test_snapshot_pin_precedes_renderer_change(self):
        source = read("Game/GameApplicationHandler.cpp")
        reload = block(source, r"bool GameApplicationHandler::ReloadConfiguredAssetManifest\(")
        self.ordered(reload, "HasPinnedSnapshot()", "skeletal_snapshot_pinned", "ReloadAssetRuntimeSnapshot(",
                     "m_AssetSystemSnapshot = immutableCandidate")
        prepare = block(source, r"bool GameApplicationHandler::PrepareM9WorldAssets\(")
        self.assertNotIn("CreateTransient", prepare)
        self.assertNotIn("ParseCookedSkeletal", prepare)
        self.assertNotIn("Clips[0]", prepare)
        self.ordered(prepare, "bRequested", "Preparation.CanPrepare()", "ParseCookedAudio", "EffectClip = effectClip", "BindSnapshot(m_AssetSystemSnapshot)",
                     "Preparation.Start(")
        self.assertIn("stateMachine->Start(Rendering3DTest, params)", source)

    def test_weak_event_and_explicit_selection(self):
        source = read("Game/GameModes/Rendering3DTest/M9SkeletalPreparation.cpp")
        start = block(source, r"SkeletalAdmissionResult M9SkeletalPreparation::Start\(")
        self.ordered(start, "!CanPrepare()", "Status::Busy", "TWeakPtr<State> weak", "runtime.LoadAsync", "weak.lock()", "bEventPending = true",
                     "GetClip(", "bReady = true")
        self.assertNotIn("GameModeContext", source)
        self.assertNotIn("[this]", source)
        attach = block(read("Game/GameModes/Rendering3DTest/M9WorldSkeletal.cpp"), r"bool AttachM9SkeletalAsset\(")
        self.ordered(attach, "outObject || outComponent", "SpawnObject", "TrackObject", "CreateComponent",
                     "SetSkeletalAsset", "SetAnimationClip", "outObject = object")

    def test_pending_enter_cleanup_and_once_attach(self):
        source = read("Game/GameModes/Rendering3DTest/Rendering3DTestRoutine.cpp")
        enter = block(source, r"GameModeEnterResult Rendering3DTestRoutine::Enter\(")
        self.ordered(enter, "M9EnterFailureGuard", "!data.m_M9WorldAcceptance->Preparation.CanEnter()",
                     "GameModeEnterResult::Failed", "m_M8MinimalPhysicsSmoke.Enter", "InitializeCameraPath",
                     "m9FailureGuard.bCommitted = true", "GameModeEnterResult::Succeeded")
        self.assertNotIn("InitializeM9WorldSkeletal", enter)
        consume = block(source, r"bool ConsumeM9WorldCompletion\(")
        self.ordered(consume, "m_bM9Attached", "Preparation.TakeEvent(event)", "bClipSelectionFailed",
                     "SelectedClip =", "InitializeM9WorldSkeletal", "CreateVoice", "m_bM9Attached = true")
        tick = block(source, r"void Rendering3DTestRoutine::Tick\(")
        self.ordered(tick, "ConsumeM9WorldCompletion", "!data.m_bM9Attached", "++data.m_M9TickCount")
        cleanup = block(source, r"void CleanupM9WorldAcceptance\(")
        self.ordered(cleanup, "Preparation.Cancel()", "UnregisterController", "audio.Shutdown()",
                     "SelectedClip.reset()", "SkeletalAsset.reset()")

    def test_component_legacy_and_override_separation(self):
        source = read("Library/Core/Private/Component/SkinnedMeshComponent.cpp")
        getter = block(source, r"SkinnedMeshComponent::GetAnimationClip\(\) const")
        self.ordered(getter, "!m_SelectedClip", "m_SkeletalAsset->GetAnimationClip()", "GetClip(i) == m_SelectedClip", "return {};")
        setter = block(source, r"bool SkinnedMeshComponent::SetAnimationClip\(")
        self.ordered(setter, "IsLoaded()", "GetClip(i) == clip", "if (!bMember)", "m_SelectedClip = clip", "MarkRenderStateDirty()")
        reset = block(source, r"void SkinnedMeshComponent::SetSkeletalAsset\(")
        self.assertIn("m_SelectedClip.reset()", reset)

    def test_body_reader_ignores_literals_and_comments(self):
        source = 'void Example() { const char* x="}"; /* } */ if (true) { } // }\n } tail'
        self.assertEqual(block(source, r"void Example\(\)"), source[source.index("{"):source.index(" tail")])
        with self.assertRaises(ValueError):
            block("void Example() {", r"void Example\(\)")


if __name__ == "__main__":
    unittest.main()
