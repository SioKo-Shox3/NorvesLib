// neural_material_decode.slang
//
// Cooperative Vector (VK_NV_cooperative_vector) を使用した
// ニューラルマテリアルデコードコンピュートシェーダー
//
// MLP推論: UV座標 → 複数PBRプロパティ（Albedo, Normal, ARM等）
//
// 1つのMLPで全PBRプロパティを一括推論し、
// 出力チャンネルをスロット別テクスチャに分配書き込みします。
//
// レイアウト:
//   set=0, binding=0: StorageBuffer (MLP重み/バイアス)
//   set=0, binding=1: RWTexture2D   (出力スロット0 - Albedo)
//   set=0, binding=2: RWTexture2D   (出力スロット1 - Normal)
//   set=0, binding=3: RWTexture2D   (出力スロット2 - ARM)
//   ... binding N+1 まで拡張可能
//
// スレッドグループサイズ: 16x16

// ========================================
// バインディング
// ========================================

[[vk::binding(0, 0)]]
RWStructuredBuffer<float> weightBuffer;

// 出力テクスチャスロット（最大8スロット）
[[vk::binding(1, 0)]]
RWTexture2D<float4> outputSlot0; // Albedo (RGBA8)

[[vk::binding(2, 0)]]
RWTexture2D<float4> outputSlot1; // Normal (RG16F → float4でアクセス)

[[vk::binding(3, 0)]]
RWTexture2D<float4> outputSlot2; // ARM (RGBA8)

// ========================================
// push constants
// ========================================

struct PushConstants
{
    uint outputWidth;
    uint outputHeight;
    uint inputChannels;   // 入力チャンネル数（通常2=UV）
    uint totalOutputChannels; // 全スロットの出力チャンネル合計
    uint hiddenWidth;
    uint hiddenLayers;
    uint slot0Channels;   // スロット0のチャンネル数
    uint slot1Channels;   // スロット1のチャンネル数
    uint slot2Channels;   // スロット2のチャンネル数
    uint slotCount;       // 有効なスロット数
};

[[vk::push_constant]]
PushConstants params;

// ========================================
// MLP推論（スタブ実装）
// ========================================

// 将来的にはCooperativeVectorMatrixMultiplyAddを使用して
// ハードウェアアクセラレーションされたMLP推論を実行する
//
// 出力: 全スロットのチャンネルを結合した1次元配列
//       [Albedo.R, Albedo.G, Albedo.B, Albedo.A, Normal.X, Normal.Y, ARM.R, ARM.G, ARM.B]
//
// 最大出力チャンネル数: 16（float4 x 4スロット分）

static const uint MAX_OUTPUT_CHANNELS = 16;

void EvaluateMLP(float2 uv, out float mlpOutput[MAX_OUTPUT_CHANNELS])
{
    // TODO: Cooperative Vector APIを使用した本実装
    // CooperativeVectorMatrixMultiplyAdd() でレイヤーごとの推論
    //
    // 現在のスタブ: UV座標からダミーPBRプロパティを生成

    // 全チャンネルをゼロ初期化
    [unroll]
    for (uint i = 0; i < MAX_OUTPUT_CHANNELS; ++i)
    {
        mlpOutput[i] = 0.0;
    }

    uint ch = 0;

    // スロット0 (Albedo): UVからグラデーション生成
    if (ch < params.totalOutputChannels)
    {
        mlpOutput[ch++] = uv.x;                    // R
    }
    if (ch < params.totalOutputChannels)
    {
        mlpOutput[ch++] = uv.y;                    // G
    }
    if (ch < params.totalOutputChannels)
    {
        mlpOutput[ch++] = 0.5;                     // B
    }
    if (ch < params.totalOutputChannels)
    {
        mlpOutput[ch++] = 1.0;                     // A
    }

    // スロット1 (Normal): デフォルト法線 (0, 0, 1) → RG = (0.5, 0.5)
    if (ch < params.totalOutputChannels)
    {
        mlpOutput[ch++] = 0.5;                     // Normal.X
    }
    if (ch < params.totalOutputChannels)
    {
        mlpOutput[ch++] = 0.5;                     // Normal.Y
    }

    // スロット2 (ARM): デフォルトPBRプロパティ
    if (ch < params.totalOutputChannels)
    {
        mlpOutput[ch++] = 1.0;                     // AO
    }
    if (ch < params.totalOutputChannels)
    {
        mlpOutput[ch++] = 0.5;                     // Roughness
    }
    if (ch < params.totalOutputChannels)
    {
        mlpOutput[ch++] = 0.0;                     // Metallic
    }
}

// ========================================
// エントリーポイント
// ========================================

[numthreads(16, 16, 1)]
void main(uint3 dispatchThreadID : SV_DispatchThreadID)
{
    uint2 pixelCoord = dispatchThreadID.xy;

    if (pixelCoord.x >= params.outputWidth || pixelCoord.y >= params.outputHeight)
    {
        return;
    }

    // ピクセル座標からUV座標を計算
    float2 uv = float2(
        (float(pixelCoord.x) + 0.5) / float(params.outputWidth),
        (float(pixelCoord.y) + 0.5) / float(params.outputHeight)
    );

    // MLP推論（全スロット一括）
    float mlpOutput[MAX_OUTPUT_CHANNELS];
    EvaluateMLP(uv, mlpOutput);

    // ========================================
    // 出力チャンネルをスロット別テクスチャに分配書き込み
    // ========================================
    uint offset = 0;

    // スロット0: Albedo
    if (params.slotCount > 0)
    {
        float4 value = float4(0, 0, 0, 1);
        for (uint i = 0; i < min(params.slot0Channels, 4); ++i)
        {
            value[i] = mlpOutput[offset + i];
        }
        outputSlot0[pixelCoord] = value;
        offset += params.slot0Channels;
    }

    // スロット1: Normal
    if (params.slotCount > 1)
    {
        float4 value = float4(0, 0, 0, 1);
        for (uint i = 0; i < min(params.slot1Channels, 4); ++i)
        {
            value[i] = mlpOutput[offset + i];
        }
        outputSlot1[pixelCoord] = value;
        offset += params.slot1Channels;
    }

    // スロット2: ARM
    if (params.slotCount > 2)
    {
        float4 value = float4(0, 0, 0, 1);
        for (uint i = 0; i < min(params.slot2Channels, 4); ++i)
        {
            value[i] = mlpOutput[offset + i];
        }
        outputSlot2[pixelCoord] = value;
        offset += params.slot2Channels;
    }
}
