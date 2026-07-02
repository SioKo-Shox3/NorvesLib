#include "Text/JsonWriter.h"
#include "Text/JsonDocument.h"
#include <cassert>
#include <iostream>

using namespace NorvesLib::Core;

namespace
{
    void TestCompactObjectOutput()
    {
        JsonWriter writer;
        writer.BeginObject();
        writer.WriteString("name", "Norves");
        writer.WriteInt64("count", -42);
        writer.WriteUInt64("big", 9007199254740991ull); // 2^53-1。JSON数値で安全な上限（64bit IDは10進文字列で書く方針）
        writer.WriteBool("flag", true);
        writer.WriteNull("none");
        writer.BeginArray("items");
        writer.WriteNumberElement(1.5);
        writer.WriteStringElement(Container::String("two"));
        writer.WriteBoolElement(false);
        writer.WriteNullElement();
        writer.BeginObject();
        writer.WriteString("k", "v");
        writer.EndObject();
        writer.EndArray();
        writer.BeginObject("child");
        writer.EndObject();
        writer.EndObject();

        assert(writer.IsComplete());
        assert(!writer.HasError());
        const Container::String json = writer.ToString();
        assert(json ==
            "{\"name\":\"Norves\",\"count\":-42,\"big\":9007199254740991,"
            "\"flag\":true,\"none\":null,\"items\":[1.5,\"two\",false,null,{\"k\":\"v\"}],\"child\":{}}");
    }

    void TestStringEscapes()
    {
        JsonWriter writer;
        writer.BeginObject();
        writer.WriteString("text", Container::String("He said \"hi\"\\ \n\t\x01"));
        writer.EndObject();

        assert(writer.IsComplete());
        const Container::String json = writer.ToString();
        assert(json == "{\"text\":\"He said \\\"hi\\\"\\\\ \\n\\t\\u0001\"}");

        JsonDocument document;
        Container::String error;
        assert(JsonDocument::TryParse(json, document, &error));
        const JsonValue text = document.GetRoot().FindMember("text");
        assert(text.IsString());
        assert(text.AsString() == "He said \"hi\"\\ \n\t\x01");
    }

    void TestPrettyOutput()
    {
        JsonWriter writer(true);
        writer.BeginObject();
        writer.WriteString("a", "b");
        writer.BeginArray("c");
        writer.WriteBoolElement(true);
        writer.EndArray();
        writer.EndObject();

        assert(writer.IsComplete());
        assert(writer.ToString() == "{\n  \"a\": \"b\",\n  \"c\": [\n    true\n  ]\n}");
    }

    void TestRoundTripThroughJsonDocument()
    {
        JsonWriter writer(true);
        writer.BeginObject();
        writer.WriteUInt64("formatVersion", 1);
        writer.BeginArray("roots");
        writer.BeginObject();
        writer.WriteString("alias", "1");
        writer.WriteNumber("range", 64.5);
        writer.WriteBool("visible", true);
        writer.BeginArray("tags");
        writer.WriteStringElement(Container::String("alpha"));
        writer.WriteStringElement(Container::String("beta"));
        writer.EndArray();
        writer.EndObject();
        writer.EndArray();
        writer.EndObject();
        assert(writer.IsComplete());

        JsonDocument document;
        Container::String error;
        assert(JsonDocument::TryParse(writer.ToString(), document, &error));

        const JsonValue root = document.GetRoot();
        assert(root.IsObject());
        assert(root.FindMember("formatVersion").AsUInt32() == 1u);
        const JsonValue roots = root.FindMember("roots");
        assert(roots.IsArray());
        assert(roots.GetArraySize() == 1);
        const JsonValue first = roots.GetArrayElement(0);
        assert(first.FindMember("alias").AsString() == "1");
        assert(first.FindMember("range").AsNumber() == 64.5);
        assert(first.FindMember("visible").AsBool());
        const JsonValue tags = first.FindMember("tags");
        assert(tags.GetArraySize() == 2);
        assert(tags.GetArrayElement(0).AsString() == "alpha");
        assert(tags.GetArrayElement(1).AsString() == "beta");
    }

    void TestUInt64NumberPrecisionPolicy()
    {
        // JsonDocumentは数値をdoubleで保持するため、JSON数値としてround-trip可能なのは2^53まで。
        // 64bitのStableId/aliasはWriteUInt64ではなく10進文字列（WriteString）で書く（シーンJSONスキーマ方針）。
        JsonWriter writer;
        writer.BeginObject();
        writer.WriteUInt64("safe", 9007199254740991ull); // 2^53-1
        writer.WriteString("id", "18446744073709551615"); // uint64最大値は文字列でのみ無劣化
        writer.EndObject();
        assert(writer.IsComplete());

        JsonDocument document;
        Container::String error;
        assert(JsonDocument::TryParse(writer.ToString(), document, &error));
        assert(document.GetRoot().FindMember("safe").AsNumber() == 9007199254740991.0);
        assert(document.GetRoot().FindMember("id").AsString() == "18446744073709551615");
    }

    void TestMisuseSetsError()
    {
        {
            JsonWriter writer;
            writer.BeginArray();
            writer.WriteString("key", "value"); // 配列内でキー付き書き込みはエラー
            assert(writer.HasError());
            assert(!writer.IsComplete());
        }
        {
            JsonWriter writer;
            writer.BeginObject();
            writer.WriteStringElement(Container::String("value")); // オブジェクト内でキー無しはエラー
            assert(writer.HasError());
        }
        {
            JsonWriter writer;
            writer.BeginObject();
            writer.EndArray(); // 種別不一致はエラー
            assert(writer.HasError());
        }
        {
            JsonWriter writer;
            writer.BeginObject();
            assert(!writer.IsComplete()); // 未クローズは未完成
            writer.EndObject();
            assert(writer.IsComplete());
            writer.BeginObject(); // ルート値は1回だけ
            assert(writer.HasError());
        }
    }
}

int main()
{
    std::cout << "JsonWriterTest start\n";

    TestCompactObjectOutput();
    TestStringEscapes();
    TestPrettyOutput();
    TestRoundTripThroughJsonDocument();
    TestUInt64NumberPrecisionPolicy();
    TestMisuseSetsError();

    std::cout << "JsonWriterTest passed\n";
    return 0;
}