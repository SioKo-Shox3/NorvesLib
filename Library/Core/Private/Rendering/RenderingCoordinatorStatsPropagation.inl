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
