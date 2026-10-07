void AccumulateViewStats(FrameStatsSnapshot& out, const SceneView::SceneViewStats& in)
{
    out.VisibleObjects += in.VisibleProxies;
    out.BatchCount += in.BatchCount;
    out.InstancedDrawCalls += in.InstancedDrawCalls;
    out.SavedDrawCalls += in.SavedDrawCalls;
    out.CullingTimeMs += in.CullingTimeMs;
    out.BatchingTimeMs += in.BatchingTimeMs;
}

void AccumulateViewStats(FramePacket* packet, const SceneView::SceneViewStats& in)
{
    if (!packet)
    {
        return;
    }

    AccumulateViewStats(packet->Stats, in);
}

// 計算スキニングから外したインスタンスの数（フレームごと）。このフレームの通し番号で Declare されたパスの数だけを足す
// （1 つの SceneView が複数のビューポートを描くときは、パスの中で合算済み）。無効・描ける大きさが無い・描画の失敗で
// Declare が呼ばれなかったビューは通し番号が古いので足さない。
uint32_t SumSkinningComputeDroppedInstances(const Container::VariableArray<Container::TSharedPtr<View>>& views,
                                            uint64_t frameSerial)
{
    uint32_t total = 0;
    for (const auto& view : views)
    {
        if (!view)
        {
            continue;
        }
        const auto* skinningPass = dynamic_cast<const SkinningComputePass*>(view->FindPass("SkinningComputePass"));
        if (skinningPass)
        {
            total += skinningPass->GetDroppedInstanceCountForFrame(frameSerial);
        }
    }
    return total;
}

void AssignSkinningComputeStats(Debug::RenderingStats& out,
                                const Container::VariableArray<Container::TSharedPtr<View>>& views,
                                uint64_t frameSerial)
{
    out.SkinningComputeDroppedInstances = SumSkinningComputeDroppedInstances(views, frameSerial);
}
