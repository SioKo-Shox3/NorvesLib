#include "Library/Core/Public/Text/IdentityPool.h"

#include <cassert>
#include <iostream>
#include <string>
#include <type_traits>

// ---------------------------------------------------------------------------
// Identity のコンパイル時リテラルパス（""_id / Identity::Literal）のテスト
//
// 検証内容:
//   1. コンパイル時構築（constexpr / static_assert）が成立すること。
//   2. コンパイル時ハッシュとランタイム（IdentityPool）ハッシュがバイト一致し、
//      レジストリキーが不変であること。
//   3. 空文字列の無効性（hash == 0）が双方で一致すること。
//   4. GetView() がリテラルの元バイト列を保持すること。
// assert + std::cout 形式の CTest 実行ファイル。
// ---------------------------------------------------------------------------

using namespace NorvesLib::Core::literals;
using NorvesLib::Core::Identity;

namespace
{
    // --- コンパイル時の証明（constexpr が壊れていればコンパイルに失敗する） ---
    inline constexpr Identity kFoo = "Foo"_id;

    static_assert(kFoo.GetHash() != 0, "compile-time literal hash must be non-zero");
    static_assert(("Foo"_id).GetHash() != 0, "compile-time literal hash must be non-zero");
    static_assert(("Foo"_id).GetHash() == Identity::Literal("Foo", 3).GetHash(),
                  "UDL and Identity::Literal must produce identical hashes");

    int g_Failures = 0;

    void Check(bool condition, const char* label)
    {
        if (condition)
        {
            std::cout << "[PASS] " << label << std::endl;
        }
        else
        {
            std::cout << "[FAIL] " << label << std::endl;
            ++g_Failures;
        }
        assert(condition);
    }

    // リテラル id（コンパイル時生成）とランタイム id のハッシュ／等価性の一致を検証する。
    // literalId は呼び出し側で "..."_id（consteval）を評価して渡す。
    void CheckParity(Identity literalId, const char* name)
    {
        const Identity runtimeId = Identity(name);

        Check(literalId == runtimeId, name);
        Check(literalId.GetHash() == runtimeId.GetHash(), name);
    }
} // namespace

int main()
{
    std::cout << "=== IdentityLiteralTest ===" << std::endl;

    // --- ランタイムとのパリティ（最重要） ---
    // コンパイル時ハッシュ == ランタイムプールハッシュ → レジストリキーが不変であることを示す。
    {
        constexpr Identity kRendering = "Rendering3DTest"_id;
        constexpr Identity kMemory = "MemoryAgingTest"_id;

        Check(kRendering == Identity("Rendering3DTest"), "Rendering3DTest equality parity");
        Check(kRendering.GetHash() == Identity("Rendering3DTest").GetHash(),
              "Rendering3DTest hash parity");

        Check(kMemory == Identity("MemoryAgingTest"), "MemoryAgingTest equality parity");
        Check(kMemory.GetHash() == Identity("MemoryAgingTest").GetHash(),
              "MemoryAgingTest hash parity");
    }

    CheckParity("Foo"_id, "Foo");
    CheckParity("Bar"_id, "Bar");
    CheckParity("HelloWorld"_id, "HelloWorld");
    CheckParity("Rendering3DTest"_id, "Rendering3DTest");
    CheckParity("MemoryAgingTest"_id, "MemoryAgingTest");

    // --- 空文字列の無効性 ---
    {
        Check((""_id).GetHash() == 0, "empty literal hash is zero");
        Check(Identity("").GetHash() == 0, "empty runtime hash is zero");
        Check(Identity("").GetHash() == (""_id).GetHash(), "empty hash parity");
        Check(!(""_id).IsValid(), "empty literal is invalid");
    }

    // --- GetView() のラウンドトリップ ---
    {
        const Identity id = "Foo"_id;
        const auto view = id.GetView();
        const std::string roundTrip(view.data(), view.size());
        Check(roundTrip == std::string("Foo"), "GetView round-trip preserves bytes");
    }

    if (g_Failures == 0)
    {
        std::cout << "ALL PASSED" << std::endl;
        return 0;
    }

    std::cout << g_Failures << " CHECK(S) FAILED" << std::endl;
    return 1;
}
