#include "Save/SaveMigration.h"
#include "Platform/PlatformFile.h"
#include "Text/JsonDocument.h"
#include <cstdio>
#include <cstring>

using namespace NorvesLib::Core;
using namespace NorvesLib::Core::Save;

namespace
{
#define SAVE_MIGRATION_CHECK(condition) \
    do { if (!(condition)) { std::fprintf(stderr, "確認失敗: %s (%s:%d)\n", #condition, __FILE__, __LINE__); return false; } } while (false)

    using Bytes = Container::VariableArray<uint8_t>;

    Container::Span<const uint8_t> TextBytes(const char* text)
    {
        return {reinterpret_cast<const uint8_t*>(text), std::strlen(text)};
    }

    bool ReadFixture(const char* name, Bytes& bytes)
    {
        Container::String path(NORVES_SAVE_FIXTURE_DIR);
        path.append("/");
        path.append(name);
        return Platform::ReadFileBytes(path, bytes).bSuccess;
    }

    bool EqualTrees(const JsonValue& left, const JsonValue& right)
    {
        if (left.GetType() != right.GetType())
        {
            return false;
        }
        if (left.IsObject())
        {
            if (left.GetObjectSize() != right.GetObjectSize())
            {
                return false;
            }
            for (size_t index = 0; index < left.GetObjectSize(); ++index)
            {
                const Container::String& name = left.GetMemberName(index);
                const JsonValue other = right.FindMember(name.c_str());
                if (!other.IsValid() || !EqualTrees(left.GetMemberValue(index), other))
                {
                    return false;
                }
            }
            return true;
        }
        if (left.IsArray())
        {
            if (left.GetArraySize() != right.GetArraySize())
            {
                return false;
            }
            for (size_t index = 0; index < left.GetArraySize(); ++index)
            {
                if (!EqualTrees(left.GetArrayElement(index), right.GetArrayElement(index)))
                {
                    return false;
                }
            }
            return true;
        }
        if (left.IsString())
        {
            return left.AsString() == right.AsString();
        }
        if (left.IsNumber())
        {
            return left.AsNumber() == right.AsNumber();
        }
        if (left.IsBoolean())
        {
            return left.AsBool() == right.AsBool();
        }
        return left.IsNull();
    }

    bool HasText(const Bytes& bytes, const char* text)
    {
        Container::String json;
        json.append(reinterpret_cast<const char*>(bytes.data()), bytes.size());
        return json.find(text) != Container::String::npos;
    }

    bool TestFixtures()
    {
        Bytes input;
        Bytes expected;
        SAVE_MIGRATION_CHECK(ReadFixture("save_v1.json", input));
        SAVE_MIGRATION_CHECK(ReadFixture("save_v1_expected_v2.json", expected));
        SAVE_MIGRATION_CHECK(input.size() >= 3 && !(input[0] == 0xef && input[1] == 0xbb && input[2] == 0xbf));
        SAVE_MIGRATION_CHECK(expected.size() >= 3 &&
                             !(expected[0] == 0xef && expected[1] == 0xbb && expected[2] == 0xbf));
        Bytes output;
        Container::String error;
        SAVE_MIGRATION_CHECK(MigrateSavePayload(input, output, &error) == SaveMigrationResult::Migrated);
        SAVE_MIGRATION_CHECK(error.empty());
        JsonDocument actualDocument;
        JsonDocument expectedDocument;
        SAVE_MIGRATION_CHECK(JsonDocument::TryParseUtf8(output, actualDocument));
        SAVE_MIGRATION_CHECK(JsonDocument::TryParseUtf8(expected, expectedDocument));
        SAVE_MIGRATION_CHECK(EqualTrees(actualDocument.GetRoot(), expectedDocument.GetRoot()));

        output.clear();
        SAVE_MIGRATION_CHECK(MigrateSavePayload(expected, output, &error) == SaveMigrationResult::UpToDate);
        SAVE_MIGRATION_CHECK(output.size() == expected.size() &&
                             std::memcmp(output.data(), expected.data(), expected.size()) == 0);
        SAVE_MIGRATION_CHECK(MigrateSavePayload(output, output, &error) == SaveMigrationResult::UpToDate);
        SAVE_MIGRATION_CHECK(output.size() == expected.size() &&
                             std::memcmp(output.data(), expected.data(), expected.size()) == 0);
        return true;
    }

    bool TestPreservationAndDefaults()
    {
        const char* input = "{\"formatVersion\":1,\"savedAt\":-9007199254740991,"
            "\"appVersion\":\"existing\",\"text\":\"__norves_save_number_i42\","
            "\"other\":{\"numbers\":[9007199254740991,-9007199254740991,0.5]},"
            "\"precise\":1.234567890123456789,\"quote\":\"a\\\"123\\\\b\"}";
        Bytes output;
        SAVE_MIGRATION_CHECK(MigrateSavePayload(TextBytes(input), output) == SaveMigrationResult::Migrated);
        SAVE_MIGRATION_CHECK(HasText(output, "\"savedAtUnix\":-9007199254740991"));
        SAVE_MIGRATION_CHECK(HasText(output, "[9007199254740991,-9007199254740991,0.5]"));
        SAVE_MIGRATION_CHECK(HasText(output, "\"playSeconds\":0"));
        JsonDocument document;
        SAVE_MIGRATION_CHECK(JsonDocument::TryParseUtf8(output, document));
        const JsonValue root = document.GetRoot();
        SAVE_MIGRATION_CHECK(root.FindMember("appVersion").AsString() == "existing");
        SAVE_MIGRATION_CHECK(root.FindMember("text").AsString() == "__norves_save_number_i42");
        SAVE_MIGRATION_CHECK(root.FindMember("precise").AsNumber() == 1.234567890123456789);
        SAVE_MIGRATION_CHECK(root.FindMember("other").FindMember("numbers").GetArraySize() == 3);
        return true;
    }

    bool ExpectResult(const char* input, SaveMigrationResult expected)
    {
        Bytes output = {4, 5, 6};
        Container::String error;
        SAVE_MIGRATION_CHECK(MigrateSavePayload(TextBytes(input), output, &error) == expected);
        SAVE_MIGRATION_CHECK(output.empty() && !error.empty());
        return true;
    }

    bool TestRejectedInputs()
    {
        SAVE_MIGRATION_CHECK(ExpectResult("{\"formatVersion\":3,\"player\":false}",
                                          SaveMigrationResult::RejectedNewerVersion));
        SAVE_MIGRATION_CHECK(ExpectResult("{\"formatVersion\":18446744073709551616}",
                                          SaveMigrationResult::RejectedNewerVersion));
        SAVE_MIGRATION_CHECK(ExpectResult("{\"formatVersion\":3,\"unknown\":1e400}",
                                          SaveMigrationResult::Invalid));
        SAVE_MIGRATION_CHECK(ExpectResult("{\"formatVersion\":1,\"bad\":9007199254740993}", SaveMigrationResult::Invalid));
        SAVE_MIGRATION_CHECK(ExpectResult("{\"formatVersion\":1,\"bad\":9007199254740992}", SaveMigrationResult::Invalid));
        SAVE_MIGRATION_CHECK(ExpectResult("{}", SaveMigrationResult::Invalid));
        SAVE_MIGRATION_CHECK(ExpectResult("{\"formatVersion\":0}", SaveMigrationResult::Invalid));
        SAVE_MIGRATION_CHECK(ExpectResult("{\"formatVersion\":-1}", SaveMigrationResult::Invalid));
        SAVE_MIGRATION_CHECK(ExpectResult("{\"formatVersion\":1.0}", SaveMigrationResult::Invalid));
        SAVE_MIGRATION_CHECK(ExpectResult("not json", SaveMigrationResult::Invalid));
        SAVE_MIGRATION_CHECK(ExpectResult("{\"formatVersion\":1,\"bad\":01}", SaveMigrationResult::Invalid));
        SAVE_MIGRATION_CHECK(ExpectResult("{\"formatVersion\":1,\"bad\":1e+}", SaveMigrationResult::Invalid));
        SAVE_MIGRATION_CHECK(ExpectResult("{\"formatVersion\":1.0,\"unknown\":1e400}", SaveMigrationResult::Invalid));
        SAVE_MIGRATION_CHECK(ExpectResult("{\"formatVersion\":1,\"formatVersion\":3}", SaveMigrationResult::Invalid));
        SAVE_MIGRATION_CHECK(ExpectResult("{\"formatVersion\":1,\"savedAt\":1,\"savedAtUnix\":2}", SaveMigrationResult::Invalid));
        SAVE_MIGRATION_CHECK(ExpectResult("{\"formatVersion\":1,\"playSeconds\":2}", SaveMigrationResult::Invalid));
        const uint8_t withBom[] = {0xef, 0xbb, 0xbf, '{', '}', '\n'};
        JsonDocument document;
        SAVE_MIGRATION_CHECK(!JsonDocument::TryParseUtf8(withBom, document));
        Bytes output;
        SAVE_MIGRATION_CHECK(MigrateSavePayload(withBom, output) == SaveMigrationResult::Invalid);
        return true;
    }
} // namespace

int main()
{
    return TestFixtures() && TestPreservationAndDefaults() && TestRejectedInputs() ? 0 : 1;
}
