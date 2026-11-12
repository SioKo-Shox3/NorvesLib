// 枯れ度の保存復元のビット一致と、不正入力に対する状態の不変性を検証する。
#ifdef _MSC_VER
#pragma execution_character_set("utf-8")
#endif

#include "Vegetation/DrynessMap.h"
#include "Text/JsonDocument.h"
#include "Text/JsonWriter.h"

#include <bit>
#include <cstdint>
#include <iostream>

using namespace NorvesLib::Core::Vegetation;
using NorvesLib::Core::JsonDocument;
using NorvesLib::Core::JsonWriter;
namespace Container = NorvesLib::Core::Container;

namespace
{
    int g_FailureCount = 0;

    void Expect(bool bCondition, const char* message)
    {
        if (!bCondition)
        {
            ++g_FailureCount;
            std::cout << "失敗: " << message << "\n";
        }
    }

    uint32_t FloatBits(float value)
    {
        return std::bit_cast<uint32_t>(value);
    }

    Container::String Serialize(const DrynessMap& map)
    {
        JsonWriter writer;
        map.Serialize(writer);
        Expect(writer.IsComplete(), "JSON の書き出しが完了する");
        return writer.ToString();
    }

    void ExpectSources(const DrynessMap& expected, const DrynessMap& actual, const char* context)
    {
        Expect(expected.GetSourceCount() == actual.GetSourceCount(), "源の数が一致する");
        const auto expectedSources = expected.GetSources();
        const auto actualSources = actual.GetSources();
        for (size_t index = 0; index < expectedSources.size(); ++index)
        {
            const DrynessSource& left = expectedSources[index];
            const DrynessSource* right = actual.Find(left.Id);
            if (right == nullptr)
            {
                ++g_FailureCount;
                std::cout << "失敗: " << context << " 不在 ID=" << left.Id << "\n";
                continue;
            }
            Expect(index < actualSources.size() && actualSources[index].Id == left.Id,
                   "源の ID と追加順が一致する");
            Expect(left.Shape == right->Shape, "源の形が一致する");
            const float leftValues[] = {left.CenterX, left.CenterZ, left.RadiusA, left.RadiusB,
                left.Yaw, left.Falloff, left.CurrentLevel, left.TargetLevel,
                left.TransitionSeconds, left.RemainingSeconds};
            const float rightValues[] = {right->CenterX, right->CenterZ, right->RadiusA, right->RadiusB,
                right->Yaw, right->Falloff, right->CurrentLevel, right->TargetLevel,
                right->TransitionSeconds, right->RemainingSeconds};
            const char* fields[] = {"CenterX", "CenterZ", "RadiusA", "RadiusB", "Yaw", "Falloff",
                "CurrentLevel", "TargetLevel", "TransitionSeconds", "RemainingSeconds"};
            for (size_t field = 0; field < 10; ++field)
            {
                if (FloatBits(leftValues[field]) != FloatBits(rightValues[field]))
                {
                    ++g_FailureCount;
                    std::cout << "失敗: " << context << " ID=" << left.Id << " 欄=" << fields[field]
                              << " 期待=" << leftValues[field] << " 実測=" << rightValues[field]
                              << " 期待ビット=" << FloatBits(leftValues[field])
                              << " 実測ビット=" << FloatBits(rightValues[field]) << "\n";
                }
            }
        }
    }

    void ExpectGrid(const DrynessMap& expected, const DrynessMap& actual, const char* context)
    {
        for (int z = 0; z < 32; ++z)
        {
            for (int x = 0; x < 32; ++x)
            {
                const float worldX = -8.0f + static_cast<float>(x) * 0.5f;
                const float worldZ = -8.0f + static_cast<float>(z) * 0.5f;
                const float left = expected.Sample(worldX, worldZ);
                const float right = actual.Sample(worldX, worldZ);
                if (FloatBits(left) != FloatBits(right))
                {
                    ++g_FailureCount;
                    std::cout << "失敗: " << context << " x=" << worldX << " z=" << worldZ
                              << " 期待=" << left << " 実測=" << right
                              << " 期待ビット=" << FloatBits(left) << " 実測ビット=" << FloatBits(right) << "\n";
                }
            }
        }
        std::cout << context << ": 格子 32x32 のビット一致を検査\n";
    }

    void TestRoundTrip()
    {
        DrynessMap original;
        Expect(original.AddCircle(-3.1f, 2.3f, 1.7f, 0.9f, 0.63f) != InvalidDrynessSourceId,
               "保存用の円を追加できる");
        Expect(original.AddEllipse(3.2f, -2.7f, 2.1f, 0.8f, 0.0f, 1.3f, 0.41f) != InvalidDrynessSourceId,
               "保存用の楕円を追加できる");
        Expect(original.AddEllipse(1.2f, 3.4f, 3.7f, 1.1f, 0.6f, 1.9f, 0.77f) != InvalidDrynessSourceId,
               "保存用の回した楕円を追加できる");
        const auto transitioning = original.AddCircle(-1.3f, -3.2f, 2.4f, 1.1f, 0.13f);
        Expect(transitioning != InvalidDrynessSourceId &&
               original.SetTarget(transitioning, 0.89f, 2.3f), "保存用の遷移を設定できる");
        original.Tick(0.37f);
        const DrynessSource* source = original.Find(transitioning);
        Expect(source != nullptr && source->CurrentLevel != source->TargetLevel &&
               source->RemainingSeconds > 0.0f && source->RemainingSeconds < source->TransitionSeconds,
               "遷移の途中で保存する");
        // 発行済みの末尾 ID を除き、源の最大 ID + 1 では次の ID を復元できない状態にする。
        const auto removed = original.AddCircle(20.0f, 20.0f, 1.0f, 0.0f, 0.5f);
        Expect(removed != InvalidDrynessSourceId && original.Remove(removed), "末尾の発行済み ID を除去できる");

        const Container::String json = Serialize(original);
        JsonDocument document;
        if (!JsonDocument::TryParse(json, document))
        {
            Expect(false, "保存した JSON を解析できる");
            return;
        }
        DrynessMap restored;
        const auto result = restored.Restore(document.GetRoot());
        if (!result.Succeeded())
        {
            ++g_FailureCount;
            std::cout << "失敗: 正常な JSON の復元 期待=None 実測="
                      << GetDrynessRestoreErrorName(result.Error) << "\n";
            return;
        }
        ExpectSources(original, restored, "復元直後");
        ExpectGrid(original, restored, "復元直後");
        for (int step = 0; step < 8; ++step)
        {
            original.Tick(0.31f);
            restored.Tick(0.31f);
            ExpectSources(original, restored, "同じ dt の適用後");
            ExpectGrid(original, restored, "同じ dt の適用後");
        }
        const auto nextOriginal = original.AddCircle(30.0f, 30.0f, 1.0f, 0.0f, 0.5f);
        const auto nextRestored = restored.AddCircle(30.0f, 30.0f, 1.0f, 0.0f, 0.5f);
        if (nextOriginal == InvalidDrynessSourceId || nextRestored != nextOriginal || nextOriginal != removed + 1)
        {
            ++g_FailureCount;
            std::cout << "失敗: 次の ID 期待=" << removed + 1 << " 保存前=" << nextOriginal
                      << " 復元後=" << nextRestored << "\n";
        }
        std::cout << "正常な復元: 4 源の全欄、遷移の継続、除去した ID の後の発行を検査\n";
    }

    void SeedPreviousState(DrynessMap& map)
    {
        Expect(map.AddCircle(9.1f, -7.3f, 2.7f, 1.2f, 0.39f) == 1, "既存の円を追加できる");
        const auto ellipse = map.AddEllipse(-5.7f, 4.9f, 3.1f, 0.7f, 0.6f, 2.2f, 0.81f);
        Expect(ellipse == 2 && map.SetTarget(ellipse, 0.12f, 3.7f), "既存の楕円を遷移させる");
        map.Tick(0.43f);
        const auto removed = map.AddCircle(40.0f, 40.0f, 1.0f, 0.0f, 0.5f);
        Expect(removed == 3 && map.Remove(removed), "既存の次の ID を源の最大 ID と異ならせる");
        map.SetGpuSourceLimit(7);
    }

    void TestRejectedInput(const Container::String& json, DrynessRestoreError expectedError)
    {
        DrynessMap previous;
        DrynessMap map;
        SeedPreviousState(previous);
        SeedPreviousState(map);
        const Container::String before = Serialize(map);
        JsonDocument document;
        if (!JsonDocument::TryParse(json, document))
        {
            ++g_FailureCount;
            std::cout << "失敗: 拒否試験の JSON は解析可能であるべき 期待="
                      << GetDrynessRestoreErrorName(expectedError) << " 入力=" << json << "\n";
            return;
        }
        const auto result = map.Restore(document.GetRoot());
        if (result.Succeeded() || result.Error != expectedError)
        {
            ++g_FailureCount;
            std::cout << "失敗: 不正入力の拒否 入力=" << json
                      << " 期待=" << GetDrynessRestoreErrorName(expectedError)
                      << " 実測=" << GetDrynessRestoreErrorName(result.Error) << "\n";
        }
        ExpectSources(previous, map, GetDrynessRestoreErrorName(expectedError));
        Expect(Serialize(map) == before, "復元失敗後の全保存状態と次の ID が不変");
        Expect(map.GetGpuSourceLimit() == 7, "復元失敗後の GPU 上限が不変");
        const auto expectedId = previous.AddCircle(1.0f, 1.0f, 1.0f, 0.0f, 0.5f);
        const auto actualId = map.AddCircle(1.0f, 1.0f, 1.0f, 0.0f, 0.5f);
        if (expectedId != 4 || actualId != expectedId)
        {
            ++g_FailureCount;
            std::cout << "失敗: 拒否後の次の ID 入力=" << json
                      << " 期待=4 実測=" << actualId << "\n";
        }
        std::cout << "不正入力: " << GetDrynessRestoreErrorName(expectedError)
                  << " の拒否と源・次の ID の不変性を検査\n";
    }

    void TestRejections()
    {
        DrynessMap valid;
        Expect(valid.AddCircle(0.0f, 0.0f, 2.0f, 1.0f, 0.5f) == 1, "有効な先頭の源を作る");
        Expect(valid.AddEllipse(1.0f, 2.0f, 3.0f, 1.0f, 0.6f, 2.0f, 0.625f) == 2,
               "置換対象の後続の源を作る");
        const Container::String baseline = Serialize(valid);
        struct InvalidCase
        {
            const TCHAR* Token;
            const TCHAR* Replacement;
            DrynessRestoreError Error;
        };
        // 後続の源を 1 か所だけ壊し、先頭の源の検証後も既存状態が保存されるか確かめる。
        const InvalidCase cases[] = {
            {_T("\"shape\":\"ellipse\""), _T("\"shape\":\"square\""), DrynessRestoreError::UnknownShape},
            {_T("\"radiusA\":3"), _T("\"radiusA\":-3"), DrynessRestoreError::NonPositiveRadius},
            {_T("\"level\":0.625"), _T("\"level\":1.5"), DrynessRestoreError::OutOfRange},
            {_T("\"id\":2,"), _T(""), DrynessRestoreError::MissingField},
            {_T("\"radiusA\":3"), _T("\"radiusA\":\"3\""), DrynessRestoreError::WrongType},
            {_T("\"id\":2"), _T("\"id\":1"), DrynessRestoreError::DuplicateId}
        };
        for (const InvalidCase& invalid : cases)
        {
            Container::String json = baseline;
            const Container::String token(invalid.Token);
            const size_t position = json.find(token);
            if (position == Container::String::npos || json.find(token, position + token.size()) != Container::String::npos)
            {
                Expect(false, "壊す対象の欄が一意に見つかる");
                continue;
            }
            json.replace(position, token.size(), invalid.Replacement);
            TestRejectedInput(json, invalid.Error);
        }
        TestRejectedInput(Container::String(_T("[]")), DrynessRestoreError::NotObject);

        // NaN は JSON 数値ではなく、1e999 も TryParse が拒否するため Restore の数値試験から省く。
        JsonDocument nonFinite;
        Expect(!JsonDocument::TryParse(Container::String(_T("{\"level\":1e999}")), nonFinite),
               "非有限となる JSON 数値は解析時に拒否される");
    }
}

int main()
{
    std::cout.precision(9);
    TestRoundTrip();
    TestRejections();
    if (g_FailureCount != 0)
    {
        std::cout << "失敗数: " << g_FailureCount << "\n";
        return 1;
    }
    std::cout << "DrynessMapSerializeTest: すべての検査に合格\n";
    return 0;
}
