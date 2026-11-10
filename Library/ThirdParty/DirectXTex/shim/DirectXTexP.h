// upstream の内部ヘッダの代わり。BC の .cpp が要る分だけを宣言する。
#pragma once

#include <algorithm>
#include <cassert>
#include <cfloat>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <utility>

// upstream と同じく、内部の計算ではベクトル演算子のオーバーロードを使わない。
#define _XM_NO_XMVECTOR_OVERLOADS_
#include <DirectXPackedVector.h>

#ifndef UNREFERENCED_PARAMETER
#define UNREFERENCED_PARAMETER(parameter) static_cast<void>(parameter)
#endif

namespace DirectX
{
    // 同じ翻訳単位に含まれる BC7 の実装が参照する、upstream と同じフラグ値。
    inline constexpr uint32_t TEX_COMPRESS_BC7_QUICK = 0x100000;
} // namespace DirectX

#if defined(_WIN32) && defined(_DEBUG)
// 不正なブロックの診断に使う Windows API だけを宣言し、Windows.h 全体を引かない。
extern "C" __declspec(dllimport) void __stdcall OutputDebugStringA(const char* outputString);
#endif
