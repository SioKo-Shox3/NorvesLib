#pragma once

#include "Container/Containers.h"
#include "Rendering/FrameUseRing.h"
#include "RHI/DeviceCapabilities.h"
#include "RHI/ICommandList.h"
#include "RHI/IDevice.h"
#include "RHI/RHITypes.h"

#include <cstdint>

namespace NorvesLib::Core::Rendering
{
    class ShaderManager;

    /**
     * @brief 画面上で小さい MegaGeometry のクラスタを、計算シェーダーでラスタして 64bit のバッファへ書く
     *
     * MegaGeometryPass のカリングが積んだソフトの一覧（パスごとのコマンドの位置。先頭の頭が間接 dispatch の引数）から、
     * 1 ワークグループ = 1 クラスタ、1 スレッド = 1 三角形（最大 128）で走る（visbuffer_sw_raster.comp）。
     * 3 頂点をハードのラスタと同じ式でクリップ空間へ移し、背面を省き、画素の中心を辺の関数（top-left 規則）で判定し、
     * 深度を画面空間で線形に補間して、VisibilityMerge の 64bit のバッファへ 64bit の atomicMin で書く。
     * 書いた値は VisibilityMerge が ID・深度へ合流させる。
     *
     * 64bit のバッファを使うので VisibilityMerge::IsSupported の装置、頂点を buffer_reference で引くのでバッファのアドレスに対応する装置だけで作れる。
     */
    class VisibilitySwRaster
    {
    public:
        /** @brief ワークグループのスレッド数（1 クラスタの三角形の最大数。ID の三角形のビット幅と一致する） */
        static constexpr uint32_t ThreadsPerGroup = 128;
        /** @brief 間接 dispatch の x の上限（Vulkan の保証する最小値。一覧がこれを超えるクラスタ数を持つときは y へ折り返す） */
        static constexpr uint32_t MaxGroupsX = 65535;

        VisibilitySwRaster();
        ~VisibilitySwRaster();

        VisibilitySwRaster(const VisibilitySwRaster&) = delete;
        VisibilitySwRaster& operator=(const VisibilitySwRaster&) = delete;

        /** @brief シェーダーとパイプラインを作る。対応しない装置・失敗したときは何も作らず false */
        bool Initialize(RHI::IDevice* device, ShaderManager* shaderManager);
        void Shutdown();

        bool IsReady() const { return m_Pipeline != nullptr; }

        /** @brief フレームの枠を選ぶ（定数バッファとディスクリプタセットを、飛行中のフレームごとに使い回すため） */
        void BeginFrame(uint32_t inFlightIndex, uint64_t frameSerial);

        /** @brief 1 回の dispatch の入力 */
        struct Inputs
        {
            /** @brief ハードのラスタと同じカメラの行列（view・projection の 128 バイト。visbuffer_mega.vert の VisFrame） */
            RHI::BufferPtr FrameUniform;
            /** @brief MegaGeometry のインスタンスの表（変換だけを引く） */
            RHI::BufferPtr InstanceBuffer;
            uint64_t InstanceBufferBytes = 0;
            /** @brief 描画の記録の表（記録の番号 = 1 + コマンドの位置） */
            RHI::BufferPtr RecordTable;
            uint64_t RecordTableBytes = 0;
            /** @brief 64bit のバッファ（UnorderedAccess の状態で渡す） */
            RHI::BufferPtr KeyBuffer;
            uint32_t KeyWidth = 0;
            uint32_t KeyHeight = 0;
            /** @brief ソフトの一覧（GenericRead の状態で渡す。パスごとの頭 4 語 + 8 語の余白の後に、パスごとの列が並ぶ） */
            RHI::BufferPtr List;
            /** @brief 一覧のパスごとの容量（クラスタ数。パスごとのコマンド数まで。MegaGeometryPass の SwRasterCapacity） */
            uint32_t ListCapacity = 0;
            /** @brief 統計のバッファ（MegaGeometryPass の統計。UnorderedAccess の状態で渡す）。bStatsEnabled が偽でも、結ぶ代わりのバッファが要る */
            RHI::BufferPtr Stats;
            /** @brief 真なら、矩形の上限を超えて描かなかった三角形の数と、走ったワークグループの数を Stats へ数える */
            bool bStatsEnabled = false;
            /** @brief ハードのラスタと同じビューポートとシザー */
            RHI::Viewport Viewport;
            RHI::ScissorRect Scissor;
        };

        /**
         * @brief パス passIndex（0 = 1 パス目、1 = 2 パス目）のソフトの一覧を、間接 dispatch でラスタする
         *
         * 間接 dispatch を断るコマンドリストでは、一覧の容量ぶんのワークグループを直接 dispatch する（余りは一覧の数でシェーダーが捨てる）。
         * @return 記録できたら true
         */
        bool RecordDispatch(RHI::ICommandList* commandList, uint32_t passIndex, const Inputs& inputs);

        /** @brief 間接 dispatch を断られて直接 dispatch に切り替えた回数（確認用） */
        uint32_t GetDirectFallbackCount() const { return m_DirectFallbackCount; }

    private:
        /** @brief 1 回の dispatch の中の資源（GPU が読み終わる前に書き換えない） */
        struct Use
        {
            RHI::DescriptorSetPtr DescriptorSet;
            RHI::BufferPtr Params;
        };

        RHI::IDevice* m_Device = nullptr;
        RHI::ShaderPtr m_Shader;
        RHI::PipelinePtr m_Pipeline;
        FrameUseRing<Use> m_Uses;
        uint32_t m_DirectFallbackCount = 0;
        bool m_bResourceFailureLogged = false;
    };

} // namespace NorvesLib::Core::Rendering
