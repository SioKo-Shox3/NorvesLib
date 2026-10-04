// VT（sparse の仮想テクスチャ）のフィードバック。材質のサンプルの箇所が、欲しいタイルの要求を GPU のバッファへ書く。
// 材質のシェーダー（gbuffer.frag・megageometry.frag・forward_transparent.frag）が共有する。
//
// NORVES_VT_FEEDBACK が定義されているとき（デバイスが fragmentStoresAndAtomics・shaderResourceResidency・sparse の 2D 部分常駐を有効にしたとき。
// シェーダーコンパイラが定義する）だけ、要求のバッファ（storage buffer）へ書く。定義されていないときは何もしない。
// 取り込む側のシェーダーが、この文書より前に VT_FEEDBACK_BINDING（バッファの binding）を定義する
// （VT_FEEDBACK_SET を定義しなければ set 0）。
//
// バッファの並び（C++ の VirtualTextureFeedback と同じ）:
//   [0]                             書き込もうとした件数（atomicAdd。capacity を超えた分も数える）
//   [1-3]                           予約
//   [4 .. 4 + capacity)             要求 1 件 = 1 語: テクスチャの番号 + 1（12bit）・ミップ（4bit）・y（8bit）・x（8bit）
//   [4 + capacity .. + HASH_WORDS)  重複を減らすハッシュの表（VT_FEEDBACK_HASH_WORDS 語。atomicCompSwap。フレームの先頭で 0）
//   [.. + HASH_WORDS 語 .. 末尾)    ハッシュの表の各枠の「2 件目以降の要求の件数」（同じ枠の語が既にあったときに atomicAdd。フレームの先頭で 0）。
//                                   最初の 1 件は要求の列に 1 語で残るので、タイルごとの要求した画素の数 = 要求の列の件数 + この枠の件数。
// capacity はバッファの語数からヘッダとハッシュの表と件数の表を引いて求める。
//
// 要求を書く画素は、4×4 の画素のうち、フレームごとに巡回する 1 画素（param の位相）。
// ただし、非常駐で粗いミップへ逃げた画素（bEscaped）は巡回によらず書く。

#ifdef NORVES_VT_FEEDBACK

#ifndef VT_FEEDBACK_SET
#define VT_FEEDBACK_SET 0
#endif

layout(std430, set = VT_FEEDBACK_SET, binding = VT_FEEDBACK_BINDING) buffer VirtualTextureFeedbackBuffer
{
    uint vtFeedbackWords[];
};

// フラグメントシェーダーが storage buffer へ書くと、実装によっては深度テストが後ろへ回る（早期 Z が効かなくなる）ので、
// 深度テストを先に行う指定にする。これを取り込むシェーダーは深度を書かず、discard も深度テストの結果を変えない。
// 隠れた画素は要求を書かないので、画面に映るタイルだけが要求になる。
layout(early_fragment_tests) in;

const uint VT_FEEDBACK_HEADER_WORDS = 4u;
const uint VT_FEEDBACK_HASH_WORDS = 4096u;
const uint VT_FEEDBACK_HASH_PROBE_LIMIT = 8u;

#endif

// 材質の UBO のパラメータ（C++ の VirtualTextureFeedback::PackMaterialParam）を、float の UBO 要素から取り出す。
// 24bit 以下の整数は float に正確に載るので、切り捨ての変換でそのまま戻る（0.5 を足すと 2^23 以上で丸めが狂う）。
uint DecodeVirtualTextureFeedbackParam(float value)
{
    return uint(value);
}

// 材質のテクスチャ 1 枚（tex。VT の表の番号が param に入っているテクスチャ）の uv（POM の後）から、
// 欲しいミップのタイルの要求を書く。
// param: 0 なら何もしない。描画（UBO）ごとに一様な値なので、画面微分（textureQueryLod）を壊さない。
//        下位から: タイル幅の log2（4bit）・タイル高さの log2（4bit）・フレームの巡回位相（4bit）・テクスチャの番号 + 1（12bit）
// bEscaped: このテクスチャの標本が非常駐で粗いミップへ逃げたか
// フラグメントシェーダー専用で、動的に一様な制御フローで呼ぶ。
void WriteVirtualTextureFeedback(sampler2D tex, vec2 uv, uint param, bool bEscaped)
{
#ifdef NORVES_VT_FEEDBACK
    if (param == 0u)
    {
        return;
    }

    // 欲しいミップ。textureQueryLod は画面微分を使うので、画素ごとに分かれる分岐より前に求める。
    float lod = textureQueryLod(tex, uv).y;

    uvec2 phasePixel = uvec2(gl_FragCoord.xy) & uvec2(3u);
    bool bPhasePixel = (phasePixel.y * 4u + phasePixel.x) == ((param >> 8u) & 15u);
    if (!bPhasePixel && !bEscaped)
    {
        return;
    }

    uint totalWords = uint(vtFeedbackWords.length());
    if (totalWords <= VT_FEEDBACK_HEADER_WORDS + 2u * VT_FEEDBACK_HASH_WORDS)
    {
        return;
    }
    uint capacity = totalWords - VT_FEEDBACK_HEADER_WORDS - 2u * VT_FEEDBACK_HASH_WORDS;

    int mip = clamp(int(floor(lod)), 0, min(textureQueryLevels(tex) - 1, 15));
    ivec2 mipSize = textureSize(tex, mip);
    vec2 wrappedUv = uv - floor(uv);
    uvec2 texel = min(uvec2(wrappedUv * vec2(mipSize)), uvec2(max(mipSize, ivec2(1)) - ivec2(1)));
    uvec2 tile = min(texel >> uvec2(param & 15u, (param >> 4u) & 15u), uvec2(255u));
    uint word = ((param >> 12u) << 20u) | (uint(mip) << 16u) | (tile.y << 8u) | tile.x;

    // 同じ要求は、ハッシュの表で要求の列の 1 語に減らし、2 件目以降は枠の件数へ足す（要求した画素の数を残す）。
    // 表が埋まっていて見つからないときは、重複を許して要求の列へ書く（読み戻しが重複を除く）。
    uint hashBase = VT_FEEDBACK_HEADER_WORDS + capacity;
    uint countBase = hashBase + VT_FEEDBACK_HASH_WORDS;
    uint slot = (word * 2654435761u) >> 20u;
    for (uint probe = 0u; probe < VT_FEEDBACK_HASH_PROBE_LIMIT; ++probe)
    {
        uint slotIndex = (slot + probe) & (VT_FEEDBACK_HASH_WORDS - 1u);
        uint previous = atomicCompSwap(vtFeedbackWords[hashBase + slotIndex], 0u, word);
        if (previous == word)
        {
            atomicAdd(vtFeedbackWords[countBase + slotIndex], 1u);
            return;
        }
        if (previous == 0u)
        {
            break;
        }
    }

    uint index = atomicAdd(vtFeedbackWords[0], 1u);
    if (index < capacity)
    {
        vtFeedbackWords[VT_FEEDBACK_HEADER_WORDS + index] = word;
    }
#endif
}
