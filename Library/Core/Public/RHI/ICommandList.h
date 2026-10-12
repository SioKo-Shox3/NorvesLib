#pragma once

#include "GPUTimestamp.h"
#include "RHITypes.h"
#include "IAccelerationStructure.h"
#include "Container/VariableArray.h"
#include <array>

namespace NorvesLib::RHI
{

    /**
     * @brief ビューポート定義
     */
    struct Viewport
    {
        float x = 0.0f;
        float y = 0.0f;
        float width = 0.0f;
        float height = 0.0f;
        float minDepth = 0.0f;
        float maxDepth = 1.0f;
    };

    /**
     * @brief シザー矩形定義
     */
    struct ScissorRect
    {
        int32_t left = 0;
        int32_t top = 0;
        int32_t right = 0;
        int32_t bottom = 0;
    };

    /**
     * @brief バッファとテクスチャの間でコピーする矩形（1ミップ・1配列レイヤー）
     *
     * 位置と大きさは texel 単位。ブロック圧縮の形式では、位置はブロックの倍数、大きさはブロックの倍数
     * （ミップの端まで届くときは端で切ってよい）にする。バッファ側は行を詰めて並べる。
     */
    struct TextureRegionCopy
    {
        uint32_t MipLevel = 0;
        uint32_t ArrayIndex = 0;
        uint32_t OffsetX = 0;
        uint32_t OffsetY = 0;
        uint32_t Width = 0;
        uint32_t Height = 0;
        uint64_t BufferOffset = 0;
    };

    /**
     * @brief テクスチャの layout（バックエンドの画像の layout を RHI の語で表したもの）
     *
     * 記録の前に layout を直列で計画するために使う。ResourceState から一意に決まる。
     */
    enum class TextureLayout : uint8_t
    {
        Undefined,
        General,
        ColorAttachment,
        DepthStencilAttachment,
        DepthStencilReadOnly,
        ShaderReadOnly,
        TransferSource,
        TransferDest,
        Present,
    };

    /**
     * @brief ResourceState をテクスチャの layout へ写す
     *
     * Vulkan のバリアが使う対応（ResourceBarrierTracker::ResourceStateToImageLayout）と同じ。
     */
    constexpr TextureLayout ResourceStateToTextureLayout(ResourceState state)
    {
        switch (state)
        {
        case ResourceState::Common:
        case ResourceState::UnorderedAccess:
        case ResourceState::RayTracingStorage:
            return TextureLayout::General;
        case ResourceState::RenderTarget:
            return TextureLayout::ColorAttachment;
        case ResourceState::DepthWrite:
            return TextureLayout::DepthStencilAttachment;
        case ResourceState::DepthRead:
            return TextureLayout::DepthStencilReadOnly;
        case ResourceState::ShaderResource:
            return TextureLayout::ShaderReadOnly;
        case ResourceState::CopySource:
            return TextureLayout::TransferSource;
        case ResourceState::CopyDest:
            return TextureLayout::TransferDest;
        case ResourceState::Present:
            return TextureLayout::Present;
        default:
            return TextureLayout::Undefined;
        }
    }

    /**
     * @brief old/new の layout を明示して張るテクスチャのバリア
     *
     * 共有の layout（Texture が持つ追跡値）を読まず、書き換えもしない。
     */
    struct ExplicitTextureBarrierDesc
    {
        ResourceState BeforeState = ResourceState::Undefined;
        ResourceState AfterState = ResourceState::Undefined;
        TextureLayout OldLayout = TextureLayout::Undefined;
        TextureLayout NewLayout = TextureLayout::Undefined;
        uint32_t MipLevel = 0;
        uint32_t ArrayIndex = 0;
        uint32_t MipCount = 0;   // 0 で残りのすべて
        uint32_t ArrayCount = 0; // 0 で残りのすべて
    };

    /**
     * @brief 提出の側から見たコマンドリストの状態
     *
     * 複数のリストを 1 回で提出する経路（ISwapChain::EndFrame の複数リストの版・SubmitBatchAndWait）が使う。
     */
    enum class CommandListSubmissionState : uint8_t
    {
        Idle,      ///< 記録していない、または提出した中身の完了を通知された
        Recording, ///< 記録の開始の後、End の前
        Recorded,  ///< End の後で、まだ提出も中止もしていない（束で提出できるのはこの状態だけ）
        Pending,   ///< 束で提出した。完了の通知を待っている
        Aborted,   ///< 記録した中身を捨てた。次の記録の開始まで提出できない
    };


    /**
     * @brief コマンドリストインターフェース
     * GPUに送信するコマンドをバッチ処理するためのオブジェクトです。
     */
    class ICommandList
    {
    public:
        virtual ~ICommandList() = default;

        /**
         * @brief コマンドリストの記録を開始
         *
         * 内部フェンスの待機・リセットを含みます。
         * Submit()による送信と対で使用してください。
         */
        virtual void Begin() = 0;

        /**
         * @brief コマンドリストの記録を開始（フェンス管理なし）
         *
         * 外部で同期制御を行う場合（SwapChain::BeginFrame/EndFrame等）に使用します。
         * フェンスの待機・リセットを行わず、コマンドバッファのリセットと記録開始のみを行います。
         * 束で提出した slot は、完了の通知（NotifySubmissionCompleted）が届くまで記録し直しません（例外を投げます）。
         */
        virtual void BeginRecording()
        {
            Begin();
        }

        /**
         * @brief 使用するフレームインデックスを設定
         *
         * ダブル/トリプルバッファリング時に、フレームごとのコマンドバッファを
         * ローテーションするために使用します。BeginRecording()の前に呼び出してください。
         * @param frameIndex フレームインデックス（SwapChain::GetCurrentFrameIndex()の値）
         */
        virtual void SetFrameIndex(uint32_t frameIndex)
        {
            (void)frameIndex;
        }

        /**
         * @brief コマンドリストの記録を終了
         */
        virtual void End() = 0;

        /**
         * @brief 提出の側から見た状態を取得
         *
         * 状態を追わない実装は、End の後を表す Recorded を返す。
         */
        virtual CommandListSubmissionState GetSubmissionState() const
        {
            return CommandListSubmissionState::Recorded;
        }

        /**
         * @brief 最後に束で提出したときの提出番号（同じ束の全リストで同じ値）。未提出は 0
         */
        virtual uint64_t GetLastSubmissionSerial() const
        {
            return 0u;
        }

        /**
         * @brief 記録中・記録済みで未提出の中身を捨て、次の記録の開始まで提出を拒む
         *
         * 提出済み（Pending）と Idle・Aborted では何もしない。GPU タイムスタンプのフレームも中止する。
         */
        virtual void AbortSubmission() noexcept
        {
        }

        /**
         * @brief 提出番号 completedSubmissionSerial までの GPU の完了を通知する
         *
         * その番号以下で束として提出した slot だけが、記録し直せるようになる。
         */
        virtual void NotifySubmissionCompleted(uint64_t completedSubmissionSerial)
        {
            (void)completedSubmissionSerial;
        }

        /**
         * @brief 複数のリストを、このリストのフェンスで 1 回に提出し、GPU の完了まで待つ
         *
         * スワップチェーンを通さない道具・テスト用。並びの順に GPU が実行する（リストをまたぐ依存は記録したバリアで成り立たせる）。
         * 全リストへ同じ提出番号を配り、完了を通知する。拒否（Recorded でないリスト・重複・バックエンドの違い）と
         * 提出の失敗では何も実行せず、並びの全リストを中止する。
         * @param commandLists 提出するリストの並び（このリストを含めなくてもよい）
         * @param commandListCount 並びの数
         * @return 配った提出番号。拒否・失敗と、対応しない実装は 0
         */
        virtual uint64_t SubmitBatchAndWait(const CommandListPtr *commandLists, uint32_t commandListCount)
        {
            (void)commandLists;
            (void)commandListCount;
            return 0u;
        }

        /**
         * @brief GPUタイムスタンプ計測に対応しているか
         *
         * デバッグ/開発ビルドのプロファイル用任意機能です。
         * 未対応バックエンドでは既定で何もしません。
         */
        virtual bool SupportsGPUTimestamps() const
        {
            return false;
        }

        virtual uint32_t GetMaximumGPUTimestampScopesPerFrame() const
        {
            return 0u;
        }

        virtual void BeginGPUTimestampFrame(uint64_t frameNumber)
        {
            (void)frameNumber;
        }

        virtual GPUTimestampScopeHandle BeginGPUTimestampScope(const char* scopeName)
        {
            (void)scopeName;
            return {};
        }

        virtual void EndGPUTimestampScope(GPUTimestampScopeHandle handle)
        {
            (void)handle;
        }

        virtual void EndGPUTimestampFrame()
        {
        }

        virtual void CommitGPUTimestampSubmission(uint32_t frameSlotIndex,
                                                  uint64_t submissionSerial)
        {
            (void)frameSlotIndex;
            (void)submissionSerial;
        }

        virtual void AbortGPUTimestampFrame(uint32_t frameSlotIndex) noexcept
        {
            (void)frameSlotIndex;
        }

        virtual void NotifyGPUTimestampFrameSlotCompleted(
            uint32_t frameSlotIndex,
            uint64_t completedSubmissionSerial)
        {
            (void)frameSlotIndex;
            (void)completedSubmissionSerial;
        }

        virtual void ConsumeCompletedGPUTimestampResults(
            Core::Container::VariableArray<GPUTimestampResult>& outResults)
        {
            outResults.clear();
        }

        /**
         * @brief GPUタイムスタンプ計測を開始
         * @param markerName 計測区間名（実装側で必要な場合のみ使用）
         */
        virtual void BeginGPUTimestamp(const char* markerName = nullptr)
        {
            (void)markerName;
        }

        /**
         * @brief GPUタイムスタンプ計測を終了
         */
        virtual void EndGPUTimestamp()
        {
        }

        /**
         * @brief 直近で取得できたGPU計測時間（ミリ秒）
         */
        virtual float GetLastGPUTimestampDurationMs() const
        {
            return 0.0f;
        }

        /**
         * @brief GPU debug marker scopeを開始
         */
        virtual void BeginDebugMarker(const char* name)
        {
            (void)name;
        }

        /**
         * @brief GPU debug marker scopeを終了
         */
        virtual void EndDebugMarker()
        {
        }

        /**
         * @brief コマンドリストの実行
         * コマンドリストをGPUに送信して実行します。
         * @param waitForCompletion 完了を待つかどうか
         */
        virtual void Submit(bool waitForCompletion = false) = 0;

        /**
         * @brief レンダーパスの開始
         * @param renderPass レンダーパス
         * @param framebuffer フレームバッファ
         */
        virtual void BeginRenderPass(RenderPassPtr renderPass, FramebufferPtr framebuffer) = 0;

        /**
         * @brief レンダーパスの終了
         */
        virtual void EndRenderPass() = 0;

        /**
         * @brief ビューポートの設定
         * @param viewport ビューポート
         */
        virtual void SetViewport(const Viewport &viewport) = 0;

        /**
         * @brief シザー矩形の設定
         * @param scissor シザー矩形
         */
        virtual void SetScissor(const ScissorRect &scissor) = 0;

        /**
         * @brief パイプラインの設定
         * @param pipeline パイプラインオブジェクト
         */
        virtual void SetPipeline(PipelinePtr pipeline) = 0;

        /**
         * @brief 頂点バッファの設定
         * @param buffer 頂点バッファ
         * @param offset バッファ内のオフセット
         * @param slot 頂点バッファスロット
         */
        virtual void SetVertexBuffer(BufferPtr buffer, uint64_t offset = 0, uint32_t slot = 0) = 0;

        /**
         * @brief インデックスバッファの設定
         * @param buffer インデックスバッファ
         * @param offset バッファ内のオフセット
         * @param type インデックス要素型（既定は従来挙動の Uint32）
         */
        virtual void SetIndexBuffer(BufferPtr buffer, uint64_t offset = 0, IndexType type = IndexType::Uint32) = 0;

        /**
         * @brief 定数バッファの設定
         * @param buffer 定数バッファ
         * @param slot レジスタスロット
         * @param stage シェーダーステージ
         */
        virtual void SetConstantBuffer(BufferPtr buffer, uint32_t slot, ShaderStage stage) = 0;

        /**
         * @brief テクスチャリソースの設定
         * @param texture テクスチャ
         * @param slot レジスタスロット
         * @param stage シェーダーステージ
         */
        virtual void SetTexture(TexturePtr texture, uint32_t slot, ShaderStage stage) = 0;

        /**
         * @brief サンプラーの設定
         * @param sampler サンプラー
         * @param slot レジスタスロット
         * @param stage シェーダーステージ
         */
        virtual void SetSampler(SamplerPtr sampler, uint32_t slot, ShaderStage stage) = 0;

        /**
         * @brief ディスクリプタセットの設定
         * @param descriptorSet ディスクリプタセット
         * @param slot セットスロット
         */
        virtual void SetDescriptorSet(DescriptorSetPtr descriptorSet, uint32_t slot = 0) = 0;

        /**
         * @brief インデックス付き描画
         * @param indexCount インデックス数
         * @param startIndexLocation 開始インデックス
         * @param baseVertexLocation ベース頂点位置
         */
        virtual void DrawIndexed(uint32_t indexCount, uint32_t startIndexLocation = 0, int32_t baseVertexLocation = 0) = 0;

        /**
         * @brief インデックスなし描画
         * @param vertexCount 頂点数
         * @param startVertexLocation 開始頂点位置
         */
        virtual void Draw(uint32_t vertexCount, uint32_t startVertexLocation = 0) = 0;

        /**
         * @brief インスタンシング描画（インデックス付き）
         * @param indexCount インデックス数
         * @param instanceCount インスタンス数
         * @param startIndexLocation 開始インデックス
         * @param baseVertexLocation ベース頂点位置
         * @param startInstanceLocation 開始インスタンス位置
         */
        virtual void DrawIndexedInstanced(uint32_t indexCount, uint32_t instanceCount,
                                          uint32_t startIndexLocation = 0, int32_t baseVertexLocation = 0, uint32_t startInstanceLocation = 0) = 0;

        /**
         * @brief インスタンシング描画（インデックスなし）
         * @param vertexCount 頂点数
         * @param instanceCount インスタンス数
         * @param startVertexLocation 開始頂点位置
         * @param startInstanceLocation 開始インスタンス位置
         */
        virtual void DrawInstanced(uint32_t vertexCount, uint32_t instanceCount,
                                   uint32_t startVertexLocation = 0, uint32_t startInstanceLocation = 0) = 0;

        /**
         * @brief 間接描画（インデックス付き）
         *
         * GPU側バッファからDrawIndexedIndirectCommandを読み取って描画します。
         * @param indirectBuffer 間接引数バッファ（VkDrawIndexedIndirectCommand配列）
         * @param offset バッファ内のオフセット（バイト）
         * @param drawCount 描画コマンド数
         * @param stride コマンド間のストライド（バイト）
         */
        virtual void DrawIndexedIndirect(BufferPtr indirectBuffer, uint64_t offset,
                                         uint32_t drawCount, uint32_t stride) = 0;

        /**
         * @brief 間接描画（インデックス付き、GPU側カウント参照）
         *
         * GPU側バッファから実際のドローコール数を読み取り、
         * 必要な分だけDrawIndexedIndirectCommandを実行します。
         * Vulkan 1.2コア機能（vkCmdDrawIndexedIndirectCount）に対応。
         *
         * @param indirectBuffer 間接引数バッファ（VkDrawIndexedIndirectCommand配列）
         * @param indirectOffset 間接バッファ内のオフセット（バイト）
         * @param countBuffer ドローコール数を格納するバッファ（uint32_t）
         * @param countOffset カウントバッファ内のオフセット（バイト）
         * @param maxDrawCount 最大ドローコール数（安全上限）
         * @param stride コマンド間のストライド（バイト）
         */
        virtual void DrawIndexedIndirectCount(BufferPtr indirectBuffer, uint64_t indirectOffset,
                                              BufferPtr countBuffer, uint64_t countOffset,
                                              uint32_t maxDrawCount, uint32_t stride) = 0;

        /**
         * @brief バッファを指定値でフィル
         *
         * Indirect Drawのカウンタリセット等に使用します。
         * @param buffer 対象バッファ
         * @param offset オフセット（バイト）
         * @param size サイズ（バイト）
         * @param value フィル値（32bit）
         */
        virtual void FillBuffer(BufferPtr buffer, uint64_t offset, uint64_t size, uint32_t value) = 0;

        /**
         * @brief コンピュートシェーダーディスパッチ
         * @param threadGroupCountX Xスレッドグループ数
         * @param threadGroupCountY Yスレッドグループ数
         * @param threadGroupCountZ Zスレッドグループ数
         */
        virtual void Dispatch(uint32_t threadGroupCountX, uint32_t threadGroupCountY, uint32_t threadGroupCountZ) = 0;

        /**
         * @brief コンピュートシェーダーの間接ディスパッチ
         *
         * GPU 側のバッファから VkDispatchIndirectCommand（x・y・z のスレッドグループ数。3 つの uint32_t）を読んで
         * ディスパッチします。引数のバッファは ResourceUsage::IndirectBuffer で作り、書き込んだ後は
         * ResourceState::IndirectArgument か GenericRead（どちらも間接引数の読み取りの段とアクセスを含む）へ遷移させてください。
         * 引数の各成分は maxComputeWorkGroupCount を超えてはいけません（x は保証される最小値 65535 まで）。
         * 対応しないコマンドリスト（既定の実装）は何も記録せず false を返します。
         *
         * @param indirectBuffer 間接引数バッファ
         * @param offset バッファ内のオフセット（バイト。4 の倍数。引数 12 バイトがバッファに収まること）
         * @return 記録したら true。引数が不正（バッファが無い・ResourceUsage::IndirectBuffer が無い・オフセットが 4 の倍数でない・範囲外）か未対応なら false で何も記録しない
         */
        virtual bool DispatchIndirect(BufferPtr indirectBuffer, uint64_t offset)
        {
            (void)indirectBuffer;
            (void)offset;
            return false;
        }

        /**
         * @brief TLASのBuildコマンドを記録
         * @param desc 構築内容
         * @return RT未対応、入力不正、または記録失敗時はfalse
         */
        virtual bool BuildAccelerationStructure(const AccelerationStructureBuildDesc& desc)
        {
            (void)desc;
            return false;
        }

        /**
         * @brief TLASのUpdateコマンドを記録。sourceのBuild数が不明な場合はdestinationを再Buildすることがあります
         * @param desc 更新内容
         * @return RT未対応、入力不正、または記録失敗時はfalse
         */
        virtual bool UpdateAccelerationStructure(const AccelerationStructureBuildDesc& desc)
        {
            (void)desc;
            return false;
        }

        /**
         * @brief ray tracing pipelineを実行
         * @param width trace領域の幅
         * @param height trace領域の高さ
         * @param depth trace領域の深さ
         * @return traceを記録できた場合true
         */
        virtual bool TraceRays(uint32_t width, uint32_t height, uint32_t depth)
        {
            (void)width;
            (void)height;
            (void)depth;
            return false;
        }

        /**
         * @brief バッファコピー
         * @param src コピー元バッファ
         * @param dst コピー先バッファ
         * @param size コピーサイズ。0の場合はsrcのサイズ
         * @param srcOffset コピー元オフセット
         * @param dstOffset コピー先オフセット
         */
        virtual void CopyBuffer(BufferPtr src, BufferPtr dst, uint64_t size = 0,
                                uint64_t srcOffset = 0, uint64_t dstOffset = 0) = 0;

        /**
         * @brief バッファからテクスチャへのコピー
         * @param src コピー元バッファ
         * @param dst コピー先テクスチャ
         * @param width コピー幅
         * @param height コピー高さ
         * @param bufferOffset バッファ内のオフセット
         * @param mipLevel コピー先のミップレベル
         * @param arrayIndex コピー先の配列インデックス
         */
        virtual void CopyBufferToTexture(BufferPtr src, TexturePtr dst,
                                         uint32_t width, uint32_t height, uint64_t bufferOffset = 0,
                                         uint32_t mipLevel = 0, uint32_t arrayIndex = 0) = 0;

        /**
         * @brief バッファからテクスチャの矩形へのコピー（タイルなどの部分書き込み用）
         *
         * 記録時点でテクスチャが TransferDst の状態であることが前提（呼び出し側が TextureBarrier で遷移する）。
         * @param src コピー元バッファ（領域は BufferOffset から行を詰めて並ぶ）
         * @param dst コピー先テクスチャ
         * @param region コピー先の矩形とバッファ内のオフセット
         * @return 記録できたら true。未対応の実装は false
         */
        virtual bool CopyBufferToTextureRegion(BufferPtr src, TexturePtr dst, const TextureRegionCopy& region)
        {
            (void)src;
            (void)dst;
            (void)region;
            return false;
        }

        /**
         * @brief テクスチャの矩形からバッファへのコピー（部分書き込みの読み戻し用）
         *
         * 記録時点でテクスチャが TransferSrc の状態であることが前提。
         * @return 記録できたら true。未対応の実装は false
         */
        virtual bool CopyTextureRegionToBuffer(TexturePtr src, BufferPtr dst, const TextureRegionCopy& region)
        {
            (void)src;
            (void)dst;
            (void)region;
            return false;
        }

        /**
         * @brief テクスチャからバッファへのコピー
         * @param src コピー元テクスチャ
         * @param dst コピー先バッファ
         * @param width コピー幅
         * @param height コピー高さ
         * @param bufferOffset バッファ内のオフセット
         * @param mipLevel コピー元のミップレベル
         * @param arrayIndex コピー元の配列インデックス
         */
        virtual void CopyTextureToBuffer(TexturePtr src, BufferPtr dst,
                                         uint32_t width, uint32_t height, uint64_t bufferOffset = 0,
                                         uint32_t mipLevel = 0, uint32_t arrayIndex = 0) = 0;

        /**
         * @brief テクスチャからテクスチャへのコピー
         * @param src コピー元テクスチャ
         * @param dst コピー先テクスチャ
         * @param width コピー幅
         * @param height コピー高さ
         * @param srcMipLevel コピー元のミップレベル
         * @param srcArrayIndex コピー元の配列インデックス
         * @param dstMipLevel コピー先のミップレベル
         * @param dstArrayIndex コピー先の配列インデックス
         */
        virtual void CopyTexture(TexturePtr src, TexturePtr dst,
                                 uint32_t width, uint32_t height,
                                 uint32_t srcMipLevel = 0, uint32_t srcArrayIndex = 0,
                                 uint32_t dstMipLevel = 0, uint32_t dstArrayIndex = 0) = 0;

        /**
         * @brief テクスチャのミップチェーンを生成
         * @param texture 対象テクスチャ
         */
        virtual void GenerateMipmaps(TexturePtr texture) = 0;

        /**
         * @brief バッファのリソースバリア（状態遷移）
         * @param buffer バッファ
         * @param beforeState 遷移前の状態
         * @param afterState 遷移後の状態
         * @param offset バッファ内のオフセット
         * @param size 対象のサイズ
         */
        virtual void BufferBarrier(BufferPtr buffer, ResourceState beforeState, ResourceState afterState,
                                   uint64_t offset = 0, uint64_t size = 0) = 0;

        /**
         * @brief テクスチャのリソースバリア（状態遷移）
         * @param texture テクスチャ
         * @param beforeState 遷移前の状態
         * @param afterState 遷移後の状態
         * @param mipLevel 対象のミップレベル
         * @param arrayIndex 対象の配列インデックス
         * @param mipCount 対象のミップレベル数（0で全て）
         * @param arrayCount 対象の配列数（0で全て）
         */
        virtual void TextureBarrier(TexturePtr texture, ResourceState beforeState, ResourceState afterState,
                                    uint32_t mipLevel = 0, uint32_t arrayIndex = 0, uint32_t mipCount = 0, uint32_t arrayCount = 0) = 0;

        /**
         * @brief old/new の layout を明示してテクスチャのバリアを張る
         *
         * 共有の layout を読まず、書き換えもしない（最終の layout は記録の後に CommitTextureLayout で書き戻す）。
         * layout を追跡しないバックエンドは状態だけの TextureBarrier に委ねる。
         */
        virtual void TextureBarrierExplicit(TexturePtr texture, const ExplicitTextureBarrierDesc &desc)
        {
            TextureBarrier(texture, desc.BeforeState, desc.AfterState,
                           desc.MipLevel, desc.ArrayIndex, desc.MipCount, desc.ArrayCount);
        }

        /**
         * @brief テクスチャの共有の layout を読む（記録の前の計画の起点と、Serial の区間の記録の後の確定に使う）
         */
        virtual TextureLayout GetTextureLayout(const ITexture *texture) const
        {
            (void)texture;
            return TextureLayout::Undefined;
        }

        /**
         * @brief 計画した最終の layout をテクスチャの共有の layout へ書き戻す
         */
        virtual void CommitTextureLayout(ITexture *texture, TextureLayout layout)
        {
            (void)texture;
            (void)layout;
        }

        /**
         * @brief このリストの記録が共有の layout を書き換えるかを切り替える
         *
         * false の間は EndRenderPass・TextureBarrier などが共有の layout を更新しない。
         * layout を計画した区間を記録するときに使い、区間の後に CommitTextureLayout で書き戻す。
         */
        virtual void SetSharedTextureLayoutWrites(bool bEnabled)
        {
            (void)bEnabled;
        }
    };

} // namespace NorvesLib::RHI
