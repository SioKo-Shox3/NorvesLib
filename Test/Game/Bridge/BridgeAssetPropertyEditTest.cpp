// 外部からの VFX 資産の編集・拒否・保存と資産ルートの境界を確かめる。
#include "Bridge/BridgeAssetPropertyEditor.h"
#include "Particle/VfxSchema.h"
#include "Text/JsonDocument.h"

#include <charconv>
#include <cmath>
#include <cwchar>
#include <iostream>
#include <limits>

#include <Windows.h>

namespace
{
    namespace Container = NorvesLib::Core::Container;
    namespace Particle = NorvesLib::Core::Particle;
    using Game::Bridge::BridgeAssetDocumentId;
    using Game::Bridge::BridgeAssetProperty;
    using Game::Bridge::BridgeAssetPropertyEditor;
    using Game::Bridge::BridgeAssetPropertyResult;
    using NorvesLib::Core::JsonDocument;
    using NorvesLib::Core::JsonValue;

    constexpr char kAssetJson[] = R"({
        "format":"norves.vfx", "version":1, "name":"BridgeEdit",
        "emitters":[{"name":"Particles", "properties":{"Space":"Screen"}, "modules":[
            {"type":"SpawnRate", "Rate":12},
            {"type":"InitPosition", "Offset":[0,0,0]},
            {"type":"InitColor", "Color":[1,0.5,0.25,1]}
        ]}]
    })";
    constexpr const char* kRatePath = "emitters/Particles/modules/0/Rate";
    constexpr const char* kOffsetPath = "emitters/Particles/modules/1/Offset";
    constexpr const char* kSpacePath = "emitters/Particles/Space";

    bool Check(bool bCondition, const char* message)
    {
        if (!bCondition)
        {
            std::cerr << "BridgeAssetPropertyEditTest: " << message << '\n';
        }
        return bCondition;
    }

#define REQUIRE(condition, message) \
    do \
    { \
        if (!Check((condition), (message))) \
        { \
            return false; \
        } \
    } while (false)

    bool CheckSuccess(const BridgeAssetPropertyResult& result, const char* operation)
    {
        if (!result.Success)
        {
            std::cerr << operation << " に失敗: " << result.Code.c_str() << " " << result.Message.c_str() << '\n';
        }
        return result.Success;
    }

    bool CheckRejection(const BridgeAssetPropertyResult& result, const char* code, const char* operation)
    {
        if (result.Success || result.Code != code)
        {
            std::cerr << operation << " の拒否が違う: 期待=" << code << " 実際="
                << (result.Success ? "success" : result.Code.c_str()) << '\n';
            return false;
        }
        return true;
    }

    bool ToUtf8(const Container::WideString& input, Container::String& output)
    {
        const int count = WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, input.c_str(), -1,
            nullptr, 0, nullptr, nullptr);
        if (count <= 0)
        {
            return false;
        }
        Container::VariableArray<char> buffer(static_cast<size_t>(count));
        if (WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, input.c_str(), -1,
            buffer.data(), count, nullptr, nullptr) != count)
        {
            return false;
        }
        output.clear();
        output.append(buffer.data(), static_cast<size_t>(count - 1));
        return true;
    }

    bool WriteAsset(const Container::WideString& path)
    {
        const HANDLE file = CreateFileW(path.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_NEW,
            FILE_ATTRIBUTE_NORMAL, nullptr);
        if (file == INVALID_HANDLE_VALUE)
        {
            return false;
        }
        constexpr DWORD size = static_cast<DWORD>(sizeof(kAssetJson) - 1);
        DWORD written = 0;
        const bool bWritten = WriteFile(file, kAssetJson, size, &written, nullptr) && written == size;
        const bool bClosed = CloseHandle(file) != 0;
        return bWritten && bClosed;
    }

    class TemporaryAssets
    {
    public:
        ~TemporaryAssets()
        {
            if (!Cleanup())
            {
                std::cerr << "一時フォルダを削除できない\n";
            }
        }

        bool Create()
        {
            Container::VariableArray<wchar_t> buffer(32768);
            const DWORD count = GetTempPathW(static_cast<DWORD>(buffer.size()), buffer.data());
            if (count == 0 || count >= buffer.size())
            {
                return false;
            }
            const Container::WideString temp(buffer.data());
            for (unsigned int attempt = 0; attempt < 64; ++attempt)
            {
                wchar_t name[128]{};
                const int length = std::swprintf(name, sizeof(name) / sizeof(name[0]),
                    L"norves-asset-edit-%lu-%llu-%u", static_cast<unsigned long>(GetCurrentProcessId()),
                    static_cast<unsigned long long>(GetTickCount64()), attempt);
                if (length <= 0 || static_cast<size_t>(length) >= sizeof(name) / sizeof(name[0]))
                {
                    return false;
                }
                const Container::WideString candidate = temp + name;
                if (CreateDirectoryW(candidate.c_str(), nullptr))
                {
                    m_Parent = candidate;
                    break;
                }
                if (GetLastError() != ERROR_ALREADY_EXISTS)
                {
                    return false;
                }
            }
            if (m_Parent.empty())
            {
                return false;
            }
            m_Root = m_Parent + L"\\assets";
            m_Outside = m_Parent + L"\\outside.vfx.json";
            if (!CreateDirectoryW(m_Root.c_str(), nullptr))
            {
                return false;
            }
            m_bRootCreated = true;
            return WriteAsset(m_Root + L"\\effect.vfx.json") && WriteAsset(m_Outside);
        }

        bool GetPaths(Container::String& root, Container::String& outside) const
        {
            return ToUtf8(m_Root, root) && ToUtf8(m_Outside, outside);
        }

        bool Cleanup()
        {
            if (m_Parent.empty())
            {
                return true;
            }
            bool bRemoved = true;
            if (m_bRootCreated)
            {
                const Container::WideString pattern = m_Root + L"\\*";
                WIN32_FIND_DATAW data{};
                const HANDLE search = FindFirstFileW(pattern.c_str(), &data);
                if (search != INVALID_HANDLE_VALUE)
                {
                    do
                    {
                        if (std::wcscmp(data.cFileName, L".") != 0 && std::wcscmp(data.cFileName, L"..") != 0)
                        {
                            const Container::WideString path = m_Root + L"\\" + data.cFileName;
                            if ((data.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) != 0 || !DeleteFileW(path.c_str()))
                            {
                                bRemoved = false;
                            }
                        }
                    } while (FindNextFileW(search, &data));
                    const DWORD searchError = GetLastError();
                    const bool bSearchClosed = FindClose(search) != 0;
                    if (searchError != ERROR_NO_MORE_FILES || !bSearchClosed)
                    {
                        bRemoved = false;
                    }
                }
                else if (GetLastError() != ERROR_FILE_NOT_FOUND)
                {
                    bRemoved = false;
                }
                if (!RemoveDirectoryW(m_Root.c_str()))
                {
                    bRemoved = false;
                }
                else
                {
                    m_bRootCreated = false;
                }
            }
            if (!m_Outside.empty() && !DeleteFileW(m_Outside.c_str()) && GetLastError() != ERROR_FILE_NOT_FOUND)
            {
                bRemoved = false;
            }
            if (!RemoveDirectoryW(m_Parent.c_str()))
            {
                bRemoved = false;
            }
            else
            {
                m_Parent.clear();
            }
            return bRemoved;
        }

    private:
        Container::WideString m_Parent;
        Container::WideString m_Root;
        Container::WideString m_Outside;
        bool m_bRootCreated = false;
    };

    const BridgeAssetProperty* FindProperty(const Container::VariableArray<BridgeAssetProperty>& properties,
        const char* path)
    {
        for (const auto& property : properties)
        {
            if (property.Path == path)
            {
                return &property;
            }
        }
        return nullptr;
    }

    bool SameJson(JsonValue actual, JsonValue expected)
    {
        if (actual.IsNumber() && expected.IsNumber())
        {
            return actual.AsNumber() == expected.AsNumber();
        }
        if (actual.IsString() && expected.IsString())
        {
            return actual.AsString() == expected.AsString();
        }
        if (actual.IsArray() && expected.IsArray() && actual.GetArraySize() == expected.GetArraySize())
        {
            for (size_t index = 0; index < actual.GetArraySize(); ++index)
            {
                if (!SameJson(actual.GetArrayElement(index), expected.GetArrayElement(index)))
                {
                    return false;
                }
            }
            return true;
        }
        return false;
    }

    bool CheckValue(const Container::VariableArray<BridgeAssetProperty>& properties,
        const char* path, const char* type, const char* expected)
    {
        const auto* property = FindProperty(properties, path);
        JsonDocument actualJson;
        JsonDocument expectedJson;
        if (property == nullptr || property->TypeName != type ||
            !JsonDocument::TryParse(property->ValueJson, actualJson) ||
            !JsonDocument::TryParse(expected, expectedJson) || !SameJson(actualJson.GetRoot(), expectedJson.GetRoot()))
        {
            std::cerr << "一覧の値が違う: " << path << '\n';
            return false;
        }
        return true;
    }

    bool CheckEditedValues(const Container::VariableArray<BridgeAssetProperty>& properties)
    {
        return CheckValue(properties, kRatePath, "Float", "24.5") &&
            CheckValue(properties, kOffsetPath, "Vector3", "[1.25,-2.5,3.75]") &&
            CheckValue(properties, kSpacePath, "Enum", R"("World")");
    }

    bool CheckUnchanged(BridgeAssetPropertyEditor& editor, BridgeAssetDocumentId document,
        const Container::VariableArray<BridgeAssetProperty>& before)
    {
        Container::VariableArray<BridgeAssetProperty> after;
        if (!CheckSuccess(editor.ListProperties(document, after), "一覧の取得") || after.size() != before.size())
        {
            return false;
        }
        for (const auto& property : before)
        {
            const auto* current = FindProperty(after, property.Path.c_str());
            if (current == nullptr || current->TypeName != property.TypeName || current->Unit != property.Unit ||
                current->Min != property.Min || current->Max != property.Max || current->ValueJson != property.ValueJson)
            {
                std::cerr << "拒否または再読込の後に一覧が変わった: " << property.Path.c_str() << '\n';
                return false;
            }
        }
        return true;
    }

    bool NumberJson(float value, Container::String& output)
    {
        char buffer[64]{};
        const auto converted = std::to_chars(buffer, buffer + sizeof(buffer), value);
        if (converted.ec != std::errc{})
        {
            return false;
        }
        output.clear();
        output.append(buffer, static_cast<size_t>(converted.ptr - buffer));
        return true;
    }

    bool RunTest()
    {
        TemporaryAssets assets;
        REQUIRE(assets.Create(), "一意の資産ルートを作れない");
        Container::String root;
        Container::String outside;
        REQUIRE(assets.GetPaths(root, outside), "資産のパスを UTF-8 に変換できない");

        BridgeAssetPropertyEditor editor(root);
        BridgeAssetDocumentId document = 0;
        REQUIRE(CheckSuccess(editor.Open("effect.vfx.json", document), "資産を開く"), "資産を開けない");
        Container::VariableArray<BridgeAssetProperty> properties;
        REQUIRE(CheckSuccess(editor.ListProperties(document, properties), "一覧の取得"), "一覧を取得できない");
        REQUIRE(CheckValue(properties, kRatePath, "Float", "12") &&
            CheckValue(properties, kOffsetPath, "Vector3", "[0,0,0]") &&
            CheckValue(properties, kSpacePath, "Enum", R"("Screen")"), "初期値が違う");

        REQUIRE(CheckSuccess(editor.SetProperty(document, kRatePath, "24.5"), "生成速度の設定"), "Float を設定できない");
        REQUIRE(CheckSuccess(editor.SetProperty(document, kOffsetPath, "[1.25,-2.5,3.75]"), "位置の設定"),
            "Vector3 を設定できない");
        REQUIRE(CheckSuccess(editor.SetProperty(document, kSpacePath, R"("World")"), "空間の設定"), "Enum を設定できない");
        REQUIRE(CheckSuccess(editor.ListProperties(document, properties), "変更後の一覧"), "一覧を取得できない");
        REQUIRE(CheckEditedValues(properties), "設定した値が一覧に反映されない");
        REQUIRE(CheckSuccess(editor.Save(document), "保存"), "保存できない");
        REQUIRE(CheckSuccess(editor.Close(document), "文書を閉じる"), "文書を閉じられない");
        REQUIRE(CheckSuccess(editor.Open("effect.vfx.json", document), "資産を再読込"), "再読込できない");
        Container::VariableArray<BridgeAssetProperty> before;
        REQUIRE(CheckSuccess(editor.ListProperties(document, before), "再読込後の一覧"), "再読込後に一覧を取得できない");
        REQUIRE(CheckEditedValues(before), "保存した値が再読込後に一致しない");

        const auto* schema = Particle::FindVfxModuleSchema(Particle::VfxModuleType::SpawnRate);
        REQUIRE(schema != nullptr, "SpawnRate のスキーマがない");
        const int32_t rateIndex = Particle::FindVfxParameterIndex(*schema, "Rate");
        REQUIRE(rateIndex >= 0, "Rate のスキーマがない");
        const auto& rate = schema->Parameters[static_cast<uint32_t>(rateIndex)];
        REQUIRE(rate.Type == Particle::VfxValueType::Float && rate.Min >= 0.0 &&
            std::isfinite(rate.Min) && std::isfinite(rate.Max), "Rate の範囲を使えない");
        const auto* rateProperty = FindProperty(before, kRatePath);
        REQUIRE(rateProperty != nullptr && rateProperty->Min == rate.Min && rateProperty->Max == rate.Max,
            "一覧の範囲がスキーマと一致しない");
        const float overMax = std::nextafter(static_cast<float>(rate.Max), (std::numeric_limits<float>::infinity)());
        const float negative = static_cast<float>(rate.Min) - 1.0f;
        REQUIRE(std::isfinite(overMax) && static_cast<double>(overMax) > rate.Max &&
            std::isfinite(negative) && negative < 0.0f && static_cast<double>(negative) < rate.Min,
            "スキーマの範囲外の値を作れない");
        Container::String overMaxJson;
        Container::String negativeJson;
        REQUIRE(NumberJson(overMax, overMaxJson) && NumberJson(negative, negativeJson), "範囲外の値を JSON にできない");

        REQUIRE(CheckRejection(editor.SetProperty(document, kRatePath, R"("invalid")"), "type_mismatch", "Float の型違い"),
            "Float の型違いを拒否できない");
        REQUIRE(CheckUnchanged(editor, document, before), "Float の型違いで一覧が変わった");
        REQUIRE(CheckRejection(editor.SetProperty(document, kOffsetPath, "[1,2]"), "type_mismatch", "Vector3 の型違い"),
            "Vector3 の型違いを拒否できない");
        REQUIRE(CheckUnchanged(editor, document, before), "Vector3 の型違いで一覧が変わった");
        REQUIRE(CheckRejection(editor.SetProperty(document, kRatePath, overMaxJson), "out_of_range", "最大値超過"),
            "最大値を超える生成速度を拒否できない");
        REQUIRE(CheckUnchanged(editor, document, before), "最大値超過で一覧が変わった");
        REQUIRE(CheckRejection(editor.SetProperty(document, kRatePath, negativeJson), "out_of_range", "負の生成速度"),
            "負の生成速度を拒否できない");
        REQUIRE(CheckUnchanged(editor, document, before), "負の生成速度で一覧が変わった");
        REQUIRE(CheckSuccess(editor.Save(document), "拒否後の保存"), "拒否後に保存できない");
        REQUIRE(CheckSuccess(editor.Close(document), "文書を閉じる"), "文書を閉じられない");
        REQUIRE(CheckSuccess(editor.Open("effect.vfx.json", document), "拒否後の再読込"), "拒否後に再読込できない");
        REQUIRE(CheckUnchanged(editor, document, before), "拒否後の保存で値が変わった");
        REQUIRE(CheckSuccess(editor.Close(document), "文書を閉じる"), "文書を閉じられない");

        BridgeAssetDocumentId outsideDocument = 0;
        REQUIRE(CheckRejection(editor.Open("../outside.vfx.json", outsideDocument), "invalid_path", "ルート外への相対パス"),
            "ルート外への相対パスを拒否できない");
        REQUIRE(CheckRejection(editor.Open(outside, outsideDocument), "invalid_path", "絶対パス"),
            "絶対パスを拒否できない");
        REQUIRE(assets.Cleanup(), "一時フォルダを削除できない");
        return true;
    }
} // namespace

int main()
{
    if (!RunTest())
    {
        return 1;
    }
    std::cout << "BridgeAssetPropertyEditTest: 合格\n";
    return 0;
}
