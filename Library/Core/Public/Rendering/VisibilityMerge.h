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
     * @brief ソフトウェアラスタの 64bit のバッファ（深度 + ID）を、ID・深度の添付へ合流させる
     *
     * 64bit のバッファは画面の画素ごとに uint64 を 1 つ持つ storage buffer で、上位 32bit = 深度のビット、
     * 下位 32bit = ID（VisibilityBuffer.h の PackKey）。ソフトウェアラスタが atomicMin で書き、空はすべてのビットが 1。
     * 合流のパスは全画面の三角形 1 枚で、バッファを読み、空なら discard、そうでなければ gl_FragDepth と ID を出す。
     * 深度の比較は LessOrEqual で、ハードのラスタが書いた深度より手前（同じ深度を含む）の値だけが ID・深度へ書かれる。
     *
     * バッファは 1 つだけ持つ（フレームごとには持たない）。同じキューの中で、フレームの最初に空で埋め（転送書き込み）、
     * 合流が読み（フラグメントシェーダー）、次のフレームの埋めはバリアで前のフレームの読み取りの後に並ぶので、
     * フレーム間で重ならない。画面の大きさが変わって作り直すときだけ、古いバッファを数フレーム手放さずに持つ。
     * 64bit のバッファの値を使うシェーダーは 64bit 整数が要るので、装置が bShaderBufferInt64Atomics を満たすときだけ
     * 資源もパスも作る（IsSupported）。
     */
    class VisibilityMerge
    {
    public:
        VisibilityMerge();
        ~VisibilityMerge();

        VisibilityMerge(const VisibilityMerge&) = delete;
        VisibilityMerge& operator=(const VisibilityMerge&) = delete;

        /** @brief 装置が 64bit のバッファへの atomicMin（64bit 整数つき）に対応するか */
        static bool IsSupported(const RHI::DeviceCapabilities& capabilities);

        /**
         * @brief 合流の render pass の記述（ID・深度とも Load。どちらも ShaderResource の状態から始まり ShaderResource で終わる）
         *
         * ID・深度を ShaderResource の状態で渡す直前のパス（1 回目の描画の後・2 回目の描画の後）に続けて開く。
         * VisibilityRasterPass の 2 回目の描画の render pass と同じ形。
         */
        static RHI::RenderPassDesc MakeLoadRenderPassDesc();

        /**
         * @brief シェーダーとパイプラインを作る。対応しない装置・失敗したときは何も作らず false（以降の記録は何もしない）
         * @param loadRenderPass 合流が描く先の render pass（MakeLoadRenderPassDesc から作ったものと互換であること）
         */
        bool Initialize(RHI::IDevice* device, ShaderManager* shaderManager, const RHI::RenderPassPtr& loadRenderPass);
        void Shutdown();

        bool IsReady() const { return m_Pipeline != nullptr; }

        /**
         * @brief フレームの枠を選ぶ（ディスクリプタセットと定数バッファを、飛行中のフレームごとに使い回すため）
         * @param inFlightIndex 飛行中のフレームの番号（ViewRenderContext::FrameIndex）
         * @param frameSerial フレームごとに必ず増える通し番号（0 は渡せない）
         */
        void BeginFrame(uint32_t inFlightIndex, uint64_t frameSerial);

        /**
         * @brief 画面の画素数（幅 × 高さ）の 64bit のバッファを用意する。同じ大きさなら何もしない
         * @return この大きさのバッファを使えるなら true（対応しない装置・作れなかったときは false）。
         *         大きさが変わって作り直すときは、古いバッファを数フレーム持っておく（GPU が前のフレームで使っているため）
         */
        bool EnsureKeyBuffer(uint32_t width, uint32_t height);

        /** @brief 今の 64bit のバッファ（用意していなければ null） */
        const RHI::BufferPtr& GetKeyBuffer() const { return m_KeyBuffer; }
        uint32_t GetKeyWidth() const { return m_KeyWidth; }
        uint32_t GetKeyHeight() const { return m_KeyHeight; }
        uint64_t GetKeyBufferBytes() const { return m_KeyBuffer ? m_KeyBuffer->GetSize() : 0; }
        /** @brief バッファを今の大きさで作った回数（作り直しの確認用） */
        uint32_t GetKeyBufferCreateCount() const { return m_KeyBufferCreateCount; }
        /** @brief 作り直しで手放し、GPU の使い終わりを待っている古いバッファの数（確認用） */
        size_t GetRetiredBufferCount() const { return m_RetiredBuffers.size(); }

        /**
         * @brief バッファをすべてのビットが 1（空）で埋める。render pass の外で呼ぶ。終わると GenericRead の状態になる
         * @return 記録できたら true
         */
        bool RecordClear(RHI::ICommandList* commandList);

        /**
         * @brief 合流の render pass を記録する。バッファが GenericRead の状態で、ID・深度が ShaderResource の状態であること
         * @param renderPass loadRenderPass と互換の render pass
         * @param framebuffer ID（R32_UINT）と深度（D32_FLOAT）を添付とする、renderPass と互換のフレームバッファ
         * @return 記録できたら true
         */
        bool RecordMerge(RHI::ICommandList* commandList,
                         const RHI::RenderPassPtr& renderPass,
                         const RHI::FramebufferPtr& framebuffer,
                         const RHI::Viewport& viewport,
                         const RHI::ScissorRect& scissor);

    private:
        /** @brief 1 フレームの枠の中の資源（GPU が読み終わる前に書き換えない） */
        struct Use
        {
            RHI::DescriptorSetPtr DescriptorSet;
            RHI::BufferPtr Params;
        };

        struct RetiredBuffer
        {
            RHI::BufferPtr Buffer;
            uint64_t RetiredSerial = 0;
        };

        void ReleaseStaleBuffers();

        RHI::IDevice* m_Device = nullptr;
        RHI::ShaderPtr m_VertexShader;
        RHI::ShaderPtr m_FragmentShader;
        RHI::PipelinePtr m_Pipeline;
        FrameUseRing<Use> m_Uses;
        uint64_t m_FrameSerial = 0;

        RHI::BufferPtr m_KeyBuffer;
        uint32_t m_KeyWidth = 0;
        uint32_t m_KeyHeight = 0;
        uint32_t m_KeyBufferCreateCount = 0;
        RHI::ResourceState m_KeyState = RHI::ResourceState::Common;
        Container::VariableArray<RetiredBuffer> m_RetiredBuffers;
    };

} // namespace NorvesLib::Core::Rendering
