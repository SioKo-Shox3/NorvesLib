#pragma once

/**
 * @file EcsSlotGate.h
 * @brief 席を再利用してよいかを決める、描画側の進み具合（EcsGpuSceneSlots.h の表と SceneView が共有する）
 */

#include <cstdint>

namespace NorvesLib::Core::Ecs
{
    /**
     * @brief 席を再利用してよいかを決める、描画側の進み具合
     *
     * 番号は FramePacketManager / PacketConfirmation の公開値。GameThread が毎フレーム SceneView へ書き、World が席の表へ渡す。
     * bValid が false の間は消費者がいない（席を直ちに再利用してよい）。
     */
    struct EcsSlotGate
    {
        bool bValid = false;
        /// 今のシーンの世代
        uint64_t SceneGeneration = 0;
        /// 送り終えた最後のパケットの番号。次に送るパケットが運ぶ解放の番号はこの +1
        uint64_t LastSentSerial = 0;
        /// RenderThread が適用した最後のパケットの番号と、その世代
        uint64_t ConfirmedSerial = 0;
        uint64_t ConfirmedGeneration = 0;
        /// 適用したパケットのうち、GPU の提出が完了した（または提出が無かった）最後の番号
        uint64_t CompletedSerial = 0;
    };
}
