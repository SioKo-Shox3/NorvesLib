#include "NorvesLibBridgeAdapter.h"

#if defined(NORVES_BRIDGE_ENABLED)

#include "Bridge/BridgeRuntimeState.h"
#include "Bridge/BridgeServerHost.h"
#include "GameApplicationHandler.h"

#include "Core/Public/Application/IWindow.h"
#include "Core/Public/Engine/Engine.h"
#include "Core/Public/Logging/LogMacros.h"
#include "Core/Public/Object/Entity.h"
#include "Core/Public/Object/IClass.h"
#include "Core/Public/Object/SchemaProjection.h"
#include "Core/Public/Object/World.h"
#include "Core/Public/Platform/NativeWindowHandle.h"

#include <cerrno>
#include <cstdint>
#include <cstdlib>
#include <optional>
#include <string>
#include <string_view>
#include <type_traits>
#include <unordered_map>

#include <Windows.h>

#include "norves/bridge/dto/common.hpp"
#include "norves/bridge/dto/methods.hpp"

namespace Game::Bridge
{
    namespace
    {
        using norves::bridge::BridgeError;
        using norves::bridge::JsonValue;
        using norves::bridge::Result;

        using AdapterResult = Result<JsonValue, BridgeError>;

        /**
         * @brief 制御下のリテラル JSON をパースして成功 Result を作る
         *
         * これらはコードが直接書いたリテラルなのでパース失敗は実装バグ。万一失敗したら
         * JSON null を返す（呼び出し側はそれでもスキーマ違反として扱える）よりは、
         * 不正でない最小値を返す。
         *
         * @param text パース対象のリテラル JSON（境界 std::string_view）
         * @return パース結果を収めた成功 Result（失敗時は空オブジェクト）
         */
        AdapterResult OkLiteral(std::string_view text)
        {
            auto parsed = JsonValue::parse(text);
            if (parsed.is_err())
            {
                // 到達しないはず（リテラルは常に妥当）。安全側として空オブジェクトを返す。
                return AdapterResult::ok(JsonValue{});
            }
            return AdapterResult::ok(std::move(parsed).value());
        }

        /**
         * @brief NorvesLib の Bridge runtime 状態を SDK の DTO enum へ変換する
         *
         * @param state NorvesLib の Bridge runtime 状態
         * @return 対応する SDK DTO の RuntimeState（未知値は Unknown）
         */
        norves::bridge::dto::RuntimeState ToDtoRuntimeState(Game::Bridge::BridgeRuntimeState state)
        {
            using DtoState = norves::bridge::dto::RuntimeState;
            switch (state)
            {
                case Game::Bridge::BridgeRuntimeState::Edit:
                    return DtoState::Edit;
                case Game::Bridge::BridgeRuntimeState::Playing:
                    return DtoState::Playing;
                case Game::Bridge::BridgeRuntimeState::Paused:
                    return DtoState::Paused;
                case Game::Bridge::BridgeRuntimeState::Stopped:
                    return DtoState::Stopped;
            }
            return DtoState::Unknown;
        }

        /**
         * @brief BridgeRuntimeState を人間可読な名前へ（状態遷移ログ用）
         *
         * @param state 名前へ変換する Bridge runtime 状態
         * @return 状態名の C 文字列（未知値は "unknown"）
         */
        const char* RuntimeStateName(Game::Bridge::BridgeRuntimeState state)
        {
            switch (state)
            {
                case Game::Bridge::BridgeRuntimeState::Edit:
                    return "edit";
                case Game::Bridge::BridgeRuntimeState::Playing:
                    return "playing";
                case Game::Bridge::BridgeRuntimeState::Paused:
                    return "paused";
                case Game::Bridge::BridgeRuntimeState::Stopped:
                    return "stopped";
            }
            return "unknown";
        }

        /**
         * @brief BridgeRuntimeState を runtime.stateChanged の wire 文字列へ
         *
         * SDK の to_wire の可視性に依存せず、ここで wire 値（edit/playing/paused/stopped）を
         * 直接綴る。
         *
         * @note これを SDK の to_wire(dto::RuntimeState) へ「DRY 解消」目的で置換しない。
         *       手書きは未知値で "edit" を返すが、to_wire 経路は未知値で "unknown" を返すため、
         *       置換は runtime.stateChanged の wire 出力を変える破壊的・可観測な挙動変更になる。
         *
         * @param state wire 値へ変換する Bridge runtime 状態
         * @return wire 文字列の C 文字列（未知値は "edit"）
         */
        const char* RuntimeStateWire(Game::Bridge::BridgeRuntimeState state)
        {
            switch (state)
            {
                case Game::Bridge::BridgeRuntimeState::Edit:
                    return "edit";
                case Game::Bridge::BridgeRuntimeState::Playing:
                    return "playing";
                case Game::Bridge::BridgeRuntimeState::Paused:
                    return "paused";
                case Game::Bridge::BridgeRuntimeState::Stopped:
                    return "stopped";
            }
            return "edit";
        }

        /**
         * @brief runtime.stateChanged の params を組んで host から emit する
         *
         * params リテラル {"state":...,"previous":...} を組んで emit する。state を新状態へ
         * 遷移させた直後、PlayAck 返却の前に呼ぶ。params は OkLiteral と同様 JsonValue::parse
         * で作る（リテラルなので妥当）。
         *
         * @param host イベント発火先の host（借用、nullptr なら何もしない）
         * @param previous 遷移前の状態
         * @param next 遷移後の状態
         * @note previous == next のとき、または host が nullptr のときは何もしない。
         */
        void EmitRuntimeStateChanged(Game::Bridge::BridgeServerHost* host,
                                     Game::Bridge::BridgeRuntimeState previous,
                                     Game::Bridge::BridgeRuntimeState next)
        {
            if (previous == next || host == nullptr)
            {
                return;
            }

            std::string text = R"({"state":")";
            text += RuntimeStateWire(next);
            text += R"(","previous":")";
            text += RuntimeStateWire(previous);
            text += R"("})";

            auto parsed = JsonValue::parse(text);
            if (parsed.is_err())
            {
                // 到達しないはず（リテラルは常に妥当）。発火を諦める（無言）。
                return;
            }
            host->EmitEvent("runtime.stateChanged", std::move(parsed).value());
        }

        /**
         * @brief JSON 文字列リテラルとして安全な形へエスケープして out へ追記する
         *
         * 動的文字列（型名 / プロパティ名）を JSON の文字列値へ綴る際に使う。前後の
         * 引用符は付けない（呼び出し側が付ける）。`"` `\` は対応するエスケープへ、
         * 制御文字（0x00-0x1F）は `\uXXXX` へ、よく使う制御文字は短縮形へ変換する。
         * バイトは符号なしとして扱い、UTF-8 のマルチバイト列（0x80 以上）はそのまま
         * 通す（Container::String は UTF-8 バイト列のため）。
         *
         * @param out 追記先の文字列
         * @param s エスケープ対象の入力（UTF-8 バイト列）
         */
        void AppendJsonString(std::string& out, std::string_view s)
        {
            for (const char ch : s)
            {
                const auto byte = static_cast<unsigned char>(ch);
                switch (byte)
                {
                    case '"':
                        out += "\\\"";
                        break;
                    case '\\':
                        out += "\\\\";
                        break;
                    case '\b':
                        out += "\\b";
                        break;
                    case '\f':
                        out += "\\f";
                        break;
                    case '\n':
                        out += "\\n";
                        break;
                    case '\r':
                        out += "\\r";
                        break;
                    case '\t':
                        out += "\\t";
                        break;
                    default:
                        if (byte < 0x20)
                        {
                            // 制御文字は \uXXXX（4 桁 16 進、小文字）へ。
                            static constexpr char kHex[] = "0123456789abcdef";
                            out += "\\u00";
                            out += kHex[(byte >> 4) & 0x0F];
                            out += kHex[byte & 0x0F];
                        }
                        else
                        {
                            // 印字可能 ASCII と UTF-8 継続バイトはそのまま。
                            out += ch;
                        }
                        break;
                }
            }
        }

        /**
         * @brief Container::String を境界 std::string_view として借用する
         *
         * Container::String は UTF-8 バイト列（TCHAR=char）であり data()/size() で
         * そのまま読める（BridgeServerHost の log.message 変換と同じ流儀）。空のときは
         * data() が番兵を指すため、空の view を返して data() を参照しない。
         *
         * @param s 借用元の Container::String（呼び出し中のみ有効）
         * @return s のバイト列を指す string_view（s より長生きさせないこと）
         */
        std::string_view ViewOf(const NorvesLib::Core::Container::String& s)
        {
            // Bridge アダプタは narrow（UTF-8）の Container::String 前提（TCHAR=char）。
            // ワイドビルドでは data() が char* でないため、ここで前提を固定する。
            static_assert(std::is_same_v<NorvesLib::Core::Container::String::value_type, char>,
                          "Bridge adapter assumes narrow (UTF-8) Container::String");
            if (s.empty())
            {
                return std::string_view{};
            }
            return std::string_view(s.data(), s.size());
        }

        /**
         * @brief Container::StringView を境界 std::string_view として借用する
         *
         * Identity::GetView() が返す StringView（UTF-8 バイト列、TCHAR=char）を借用で読む。
         * 空（data() が nullptr のことがある）のときは空 view を返し data() を参照しない。
         *
         * @param s 借用元の StringView（呼び出し中のみ有効）
         * @return s のバイト列を指す string_view（s より長生きさせないこと）
         */
        std::string_view ViewOf(const NorvesLib::Core::Container::StringView& s)
        {
            static_assert(std::is_same_v<NorvesLib::Core::Container::StringView::value_type, char>,
                          "Bridge adapter assumes narrow (UTF-8) Container::StringView");
            if (s.empty() || s.data() == nullptr)
            {
                return std::string_view{};
            }
            return std::string_view(s.data(), s.size());
        }

        /**
         * @brief StableTypeId を人間可読な型名へ解決する
         *
         * TypeRegistry::FindStable(StableTypeId) で TypeInfo を引き、その Name を返す。未登録
         * （InvalidSchemaId 含む）や Name が空のときは非空のフォールバック "unknown" を返す
         * （propertyEntry.valueType は minLength:1）。
         *
         * @param type 解決する StableTypeId
         * @return 型名（解決不能/空なら "unknown"）。戻り値は TypeRegistry が保持する Name を
         *         指す string_view（呼び出し直後の同期文脈でのみ有効）。
         */
        std::string_view ResolveTypeName(NorvesLib::Core::StableTypeId type)
        {
            static constexpr std::string_view kUnknownType = "unknown";
            const NorvesLib::Core::TypeInfo* info =
                NorvesLib::Core::TypeRegistry::Get().FindStable(type);
            if (info == nullptr || info->Name.empty())
            {
                return kUnknownType;
            }
            return ViewOf(info->Name);
        }

        /**
         * @brief serialized が JSON number リテラルとして妥当か軽く検証する
         *
         * NorvesLib の算術型 SerializeValue は std::ostringstream 既定で十進テキストを書く
         * （例: "60", "-10", "1.5", "3.14159"）。これは大半が JSON number として妥当だが、
         * 念のため JSON number 文法（任意の先頭 '-'、整数部、任意の小数部、任意の指数部）に
         * 照合し、妥当でなければ呼び出し側が文字列フォールバックする。整数部は JSON 仕様どおり
         * '0' 単独または非ゼロ始まりのみ許容し、複数桁の先頭ゼロ（"01" など）は弾く（裸 number 化を
         * 防ぐ）。"0" / "0.5" / "-0" は許容、"01" / "00" は不可。
         *
         * @param s 検証対象のシリアライズ済みテキスト
         * @return JSON number として綴れるなら true
         */
        bool LooksLikeJsonNumber(std::string_view s)
        {
            if (s.empty())
            {
                return false;
            }
            std::size_t i = 0;
            if (s[i] == '-')
            {
                ++i;
            }
            std::size_t digitsBefore = 0;
            const std::size_t intStart = i;
            while (i < s.size() && s[i] >= '0' && s[i] <= '9')
            {
                ++i;
                ++digitsBefore;
            }
            if (digitsBefore == 0)
            {
                return false;
            }
            // JSON 仕様: 整数部は "0" 単独か非ゼロ始まり。複数桁で先頭が '0' は不可（"01"）。
            if (digitsBefore > 1 && s[intStart] == '0')
            {
                return false;
            }
            if (i < s.size() && s[i] == '.')
            {
                ++i;
                std::size_t digitsAfter = 0;
                while (i < s.size() && s[i] >= '0' && s[i] <= '9')
                {
                    ++i;
                    ++digitsAfter;
                }
                if (digitsAfter == 0)
                {
                    return false;
                }
            }
            if (i < s.size() && (s[i] == 'e' || s[i] == 'E'))
            {
                ++i;
                if (i < s.size() && (s[i] == '+' || s[i] == '-'))
                {
                    ++i;
                }
                std::size_t expDigits = 0;
                while (i < s.size() && s[i] >= '0' && s[i] <= '9')
                {
                    ++i;
                    ++expDigits;
                }
                if (expDigits == 0)
                {
                    return false;
                }
            }
            return i == s.size();
        }

        /**
         * @brief VectorN(...) 形式のシリアライズ値を JSON 数値配列へ書き出す
         *
         * NorvesLib の Vector2/3/4・Quaternion SerializeValue は "Vector3(x,y,z)" のように
         * 型名 + 括弧 + カンマ区切り十進数で綴られる。最初の '(' から末尾の ')' までを取り出し、
         * カンマで分割して各要素を JSON number（妥当なら）として `[a,b,c]` の形に書き出す。
         * 妥当な配列を組めない場合は false を返し、呼び出し側が文字列フォールバックする。
         *
         * @param out 追記先
         * @param serialized "VectorN(a,b,c[,d])" 形式のシリアライズ済みテキスト
         * @return 配列として書き出せたら true
         */
        bool AppendVectorArray(std::string& out, std::string_view serialized)
        {
            const std::size_t open = serialized.find('(');
            const std::size_t close = serialized.rfind(')');
            if (open == std::string_view::npos || close == std::string_view::npos || close <= open)
            {
                return false;
            }
            const std::string_view inner = serialized.substr(open + 1, close - open - 1);
            if (inner.empty())
            {
                return false;
            }

            std::string assembled = "[";
            std::size_t start = 0;
            bool bFirst = true;
            while (start <= inner.size())
            {
                std::size_t comma = inner.find(',', start);
                const std::size_t end =
                    (comma == std::string_view::npos) ? inner.size() : comma;
                const std::string_view component = inner.substr(start, end - start);
                if (!LooksLikeJsonNumber(component))
                {
                    return false;
                }
                if (!bFirst)
                {
                    assembled += ',';
                }
                bFirst = false;
                assembled.append(component.data(), component.size());

                if (comma == std::string_view::npos)
                {
                    break;
                }
                start = comma + 1;
            }
            assembled += ']';

            out += assembled;
            return true;
        }

        /**
         * @brief プロパティの SerializedValue（NorvesLib 独自表記）を純 JSON 値へ変換して追記する
         *
         * 型の Kind / Name で分岐する。bool は "1"/"0" を true/false へ、算術型（Integer/Float）は
         * 十進テキストをそのまま JSON number へ（妥当性検証付き、不正なら文字列フォールバック）、
         * Vector2/3/4・Quaternion は VectorN(...) を JSON 数値配列へ、String やその他（Transform/
         * Enum/未知）は JSON 文字列（AppendJsonString でエスケープ）として書き出す。
         *
         * @param out 追記先
         * @param type プロパティの StableTypeId
         * @param serialized RuntimeSchemaProjector が返した SerializedValue
         * @note 値はすべてコピー済みテキストから生成する（live memory 非転送）。
         */
        void AppendWireValue(std::string& out, NorvesLib::Core::StableTypeId type,
                             std::string_view serialized)
        {
            using NorvesLib::Core::TypeInfo;
            using NorvesLib::Core::TypeKind;

            const TypeInfo* info = NorvesLib::Core::TypeRegistry::Get().FindStable(type);
            const TypeKind kind = (info != nullptr) ? info->Kind : TypeKind::Custom;
            const std::string_view typeName = (info != nullptr) ? ViewOf(info->Name) : std::string_view{};

            switch (kind)
            {
                case TypeKind::Bool:
                {
                    // bool の SerializeValue は std::is_arithmetic 経路で "1"/"0" を書く。
                    out += (serialized == "1" || serialized == "true") ? "true" : "false";
                    return;
                }
                case TypeKind::Integer:
                case TypeKind::Float:
                {
                    if (LooksLikeJsonNumber(serialized))
                    {
                        out.append(serialized.data(), serialized.size());
                    }
                    else
                    {
                        out += '"';
                        AppendJsonString(out, serialized);
                        out += '"';
                    }
                    return;
                }
                case TypeKind::Struct:
                {
                    // Vector2/3/4・Quaternion は数値配列へ。Transform 等は配列化できないので
                    // 下の文字列フォールバックへ落ちる。
                    if (typeName == "Math::Vector2" || typeName == "Math::Vector3" ||
                        typeName == "Math::Vector4" || typeName == "Math::Quaternion")
                    {
                        if (AppendVectorArray(out, serialized))
                        {
                            return;
                        }
                    }
                    break;
                }
                case TypeKind::String:
                case TypeKind::Void:
                case TypeKind::Object:
                case TypeKind::Resource:
                case TypeKind::Array:
                case TypeKind::Enum:
                case TypeKind::Custom:
                default:
                    break;
            }

            // String / Transform / Enum / 未知型などは JSON 文字列として綴る（schema は string 許容）。
            out += '"';
            AppendJsonString(out, serialized);
            out += '"';
        }

        // 防御的な再帰深さ上限。World 階層は実用上ごく浅いが、異常な深さ/循環に備えて
        // 走査・ノード化の両方で打ち切り基準として共有する。
        inline constexpr int kMaxSceneDepth = 64;

        /**
         * @brief Entity 部分木から ObjectId 一致の Entity を再帰で探す
         *
         * @param entity 走査起点 Entity
         * @param objectId 探す ObjectId
         * @param depth 現在の再帰深さ（上限超過で打ち切り）
         * @return 見つかった Entity（非所有借用）。無ければ nullptr
         */
        NorvesLib::Core::Entity* FindEntityInSubtree(NorvesLib::Core::Entity& entity,
                                                     uint64_t objectId, int depth)
        {
            if (entity.GetObjectId() == objectId)
            {
                return &entity;
            }
            if (depth >= kMaxSceneDepth)
            {
                return nullptr;
            }
            const auto children = entity.GetChildEntities();
            for (NorvesLib::Core::Entity* child : children)
            {
                if (child == nullptr)
                {
                    continue;
                }
                if (NorvesLib::Core::Entity* found = FindEntityInSubtree(*child, objectId, depth + 1))
                {
                    return found;
                }
            }
            return nullptr;
        }

        /**
         * @brief World のルートから ObjectId 一致の Entity を再帰で逆引きする
         *
         * 走査は読み取りのみで、戻り値の Entity ポインタはこの .cpp 内（同期・同スレッド文脈）で
         * スナップショット DTO の生成にのみ使い、JsonValue へは一切入れない（live memory 非転送）。
         *
         * @param world 走査対象 World
         * @param objectId 探す ObjectId
         * @return 見つかった Entity（非所有借用）。無ければ nullptr
         */
        NorvesLib::Core::Entity* FindEntityByObjectId(const NorvesLib::Core::World& world,
                                                      uint64_t objectId)
        {
            const auto roots = world.GetRootEntities();
            for (NorvesLib::Core::Entity* root : roots)
            {
                if (root == nullptr)
                {
                    continue;
                }
                if (NorvesLib::Core::Entity* found = FindEntityInSubtree(*root, objectId, 0))
                {
                    return found;
                }
            }
            return nullptr;
        }

        /**
         * @brief コンパクトな JSON オブジェクトテキストから、トップレベルのフィールド値を生の
         *        JSON テキストで取り出す（mock の extract_json_field を dump 前提で移植）
         *
         * SDK の JsonValue には構造的読み取り API が無いため、params.dump() のコンパクト表現を
         * 読む最小スキャナ。括弧/引用符のバランスを取りながら値トークン終端を求める。見つからなければ
         * nullopt。汎用 JSON パーサではない（dump 出力前提）。
         *
         * @param objectText params.dump() のコンパクト JSON テキスト
         * @param key トップレベルのフィールド名
         * @return 生の値テキスト（文字列なら引用符込み）。無ければ nullopt
         */
        std::optional<std::string> extract_json_field(const std::string& objectText,
                                                      std::string_view key)
        {
            std::string needle = "\"";
            needle.append(key.data(), key.size());
            needle += "\":";
            const std::size_t keyPos = objectText.find(needle);
            if (keyPos == std::string::npos)
            {
                return std::nullopt;
            }
            std::size_t pos = keyPos + needle.size();
            if (pos >= objectText.size())
            {
                return std::nullopt;
            }
            const std::size_t start = pos;
            int depth = 0;
            bool inString = false;
            bool escaped = false;
            for (; pos < objectText.size(); ++pos)
            {
                const char c = objectText[pos];
                if (inString)
                {
                    if (escaped)
                    {
                        escaped = false;
                    }
                    else if (c == '\\')
                    {
                        escaped = true;
                    }
                    else if (c == '"')
                    {
                        inString = false;
                    }
                    continue;
                }
                if (c == '"')
                {
                    inString = true;
                }
                else if (c == '{' || c == '[')
                {
                    ++depth;
                }
                else if (c == '}' || c == ']')
                {
                    if (depth == 0)
                    {
                        break;  // 親オブジェクトの閉じ括弧に到達。
                    }
                    --depth;
                }
                else if ((c == ',' || c == ':') && depth == 0)
                {
                    break;  // トップレベルの値区切りに到達。
                }
            }
            return objectText.substr(start, pos - start);
        }

        /**
         * @brief コンパクトな JSON オブジェクトテキストから、トップレベルの文字列フィールドの値
         *        （引用符なし）を取り出す（mock の extract_string_field を移植）
         *
         * 値が JSON 文字列（前後が引用符）のときのみ中身を返す。最小スキャナのため、エスケープ
         * シーケンスのデコードはしない（objectId は単純な ASCII 数字列を想定）。
         *
         * @param objectText params.dump() のコンパクト JSON テキスト
         * @param key トップレベルのフィールド名
         * @return 引用符を外した文字列値。文字列でない/無いなら nullopt
         */
        std::optional<std::string> extract_string_field(const std::string& objectText,
                                                        std::string_view key)
        {
            const std::optional<std::string> raw = extract_json_field(objectText, key);
            if (!raw.has_value())
            {
                return std::nullopt;
            }
            const std::string& value = raw.value();
            if (value.size() >= 2 && value.front() == '"' && value.back() == '"')
            {
                return value.substr(1, value.size() - 2);
            }
            return std::nullopt;
        }

        /**
         * @brief Entity を sceneNode JSON へ再帰でノード化して out へ追記する
         *
         * 出力: { "id":"<ObjectId>", "kind":"<クラス名>", "children":[ <子ノード>... ] }。
         * name は付けない（Entity に表示名アクセサが無いため任意フィールドは省略）。id は ObjectId
         * の 10 進文字列、kind はクラス名（空なら "Entity" にフォールバックして空文字を出さない）。
         * 子が無い/深さ上限到達時は children を省略（葉ノード）。Entity ポインタや生ポインタは
         * 一切 JSON へ入れず、ObjectId 値とクラス名文字列のコピーだけを綴る（live memory 非転送）。
         *
         * @param out 追記先
         * @param entity ノード化する Entity
         * @param depth 現在の再帰深さ（上限到達で children を打ち切る）
         */
        void AppendEntityNode(std::string& out, const NorvesLib::Core::Entity& entity, int depth)
        {
            out += R"({"id":")";
            out += std::to_string(static_cast<unsigned long long>(entity.GetObjectId()));
            out += R"(","kind":")";

            // クラス名。空なら "Entity" にフォールバック（kind は minLength:1）。
            std::string_view kind = "Entity";
            const NorvesLib::Core::IClass* cls = entity.GetClass();
            if (cls != nullptr)
            {
                const std::string_view className = ViewOf(cls->GetClassName().GetView());
                if (!className.empty())
                {
                    kind = className;
                }
            }
            AppendJsonString(out, kind);
            out += '"';

            // 子 Entity。深さ上限未満かつ子があるときだけ children を出す。
            if (depth < kMaxSceneDepth)
            {
                const auto children = entity.GetChildEntities();
                bool bFirst = true;
                for (const NorvesLib::Core::Entity* child : children)
                {
                    if (child == nullptr)
                    {
                        continue;
                    }
                    if (bFirst)
                    {
                        out += R"(,"children":[)";
                        bFirst = false;
                    }
                    else
                    {
                        out += ',';
                    }
                    AppendEntityNode(out, *child, depth + 1);
                }
                if (!bFirst)
                {
                    out += ']';
                }
            }

            out += '}';
        }

        /**
         * @brief std::string_view の前後の JSON 空白（space/tab/CR/LF）を取り除いた view を返す
         *
         * extract_json_field が返す生 JSON テキストは dump 出力なので通常は前後空白を含まないが、
         * 防御的に trim してから number/bool/array/string 判定に回す。
         *
         * @param s trim 対象（借用）
         * @return 前後空白を除いた view（s の部分ビュー）
         */
        std::string_view TrimJsonWhitespace(std::string_view s)
        {
            const auto isWs = [](char c) { return c == ' ' || c == '\t' || c == '\n' || c == '\r'; };
            std::size_t begin = 0;
            std::size_t end = s.size();
            while (begin < end && isWs(s[begin]))
            {
                ++begin;
            }
            while (end > begin && isWs(s[end - 1]))
            {
                --end;
            }
            return s.substr(begin, end - begin);
        }

        /**
         * @brief JSON 文字列リテラル（引用符込み）を引用符を外して JSON アンエスケープし out へ書く
         *
         * AppendJsonString の逆。前後が `"` で囲まれていることを要求し、内側の `\"` `\\` `\/`
         * `\b` `\f` `\n` `\r` `\t` `\uXXXX` を対応するバイトへ復号する。`\uXXXX` は BMP の範囲を
         * UTF-8 へエンコードする（サロゲートペアは \uXXXX\uXXXX の連結として復号する）。不正な
         * エスケープや未終端は false。out は成功時のみ意味を持つ（失敗時は途中まで書くため
         * 呼び出し側は破棄すること）。
         *
         * @param quoted 引用符込みの JSON 文字列テキスト（借用）
         * @param out 復号結果の追記先
         * @return 正しく復号できたら true
         */
        bool DecodeJsonString(std::string_view quoted, std::string& out)
        {
            if (quoted.size() < 2 || quoted.front() != '"' || quoted.back() != '"')
            {
                return false;
            }
            const std::string_view body = quoted.substr(1, quoted.size() - 2);

            const auto hexDigit = [](char c, unsigned& outVal) -> bool {
                if (c >= '0' && c <= '9')
                {
                    outVal = static_cast<unsigned>(c - '0');
                }
                else if (c >= 'a' && c <= 'f')
                {
                    outVal = static_cast<unsigned>(c - 'a' + 10);
                }
                else if (c >= 'A' && c <= 'F')
                {
                    outVal = static_cast<unsigned>(c - 'A' + 10);
                }
                else
                {
                    return false;
                }
                return true;
            };

            const auto readHex4 = [&](std::size_t at, unsigned& outCode) -> bool {
                if (at + 4 > body.size())
                {
                    return false;
                }
                unsigned code = 0;
                for (std::size_t k = 0; k < 4; ++k)
                {
                    unsigned digit = 0;
                    if (!hexDigit(body[at + k], digit))
                    {
                        return false;
                    }
                    code = (code << 4) | digit;
                }
                outCode = code;
                return true;
            };

            const auto encodeUtf8 = [&out](unsigned cp) {
                // BMP + 補助平面の UTF-8 エンコード。
                if (cp <= 0x7F)
                {
                    out += static_cast<char>(cp);
                }
                else if (cp <= 0x7FF)
                {
                    out += static_cast<char>(0xC0 | (cp >> 6));
                    out += static_cast<char>(0x80 | (cp & 0x3F));
                }
                else if (cp <= 0xFFFF)
                {
                    out += static_cast<char>(0xE0 | (cp >> 12));
                    out += static_cast<char>(0x80 | ((cp >> 6) & 0x3F));
                    out += static_cast<char>(0x80 | (cp & 0x3F));
                }
                else
                {
                    out += static_cast<char>(0xF0 | (cp >> 18));
                    out += static_cast<char>(0x80 | ((cp >> 12) & 0x3F));
                    out += static_cast<char>(0x80 | ((cp >> 6) & 0x3F));
                    out += static_cast<char>(0x80 | (cp & 0x3F));
                }
            };

            for (std::size_t i = 0; i < body.size(); ++i)
            {
                const char c = body[i];
                if (c != '\\')
                {
                    // 生の制御文字（エスケープされていない 0x00-0x1F）は JSON では不正。
                    if (static_cast<unsigned char>(c) < 0x20)
                    {
                        return false;
                    }
                    out += c;
                    continue;
                }
                // エスケープシーケンス。
                if (i + 1 >= body.size())
                {
                    return false;  // 末尾の単独バックスラッシュ。
                }
                const char esc = body[++i];
                switch (esc)
                {
                    case '"':
                        out += '"';
                        break;
                    case '\\':
                        out += '\\';
                        break;
                    case '/':
                        out += '/';
                        break;
                    case 'b':
                        out += '\b';
                        break;
                    case 'f':
                        out += '\f';
                        break;
                    case 'n':
                        out += '\n';
                        break;
                    case 'r':
                        out += '\r';
                        break;
                    case 't':
                        out += '\t';
                        break;
                    case 'u':
                    {
                        unsigned code = 0;
                        if (!readHex4(i + 1, code))
                        {
                            return false;
                        }
                        i += 4;
                        // 上位サロゲートなら下位サロゲートを連結して 1 コードポイントへ。
                        if (code >= 0xD800 && code <= 0xDBFF)
                        {
                            if (i + 2 < body.size() && body[i + 1] == '\\' && body[i + 2] == 'u')
                            {
                                unsigned low = 0;
                                if (!readHex4(i + 3, low) || low < 0xDC00 || low > 0xDFFF)
                                {
                                    return false;
                                }
                                i += 6;
                                code = 0x10000 + (((code - 0xD800) << 10) | (low - 0xDC00));
                            }
                            else
                            {
                                return false;  // 孤立した上位サロゲート。
                            }
                        }
                        else if (code >= 0xDC00 && code <= 0xDFFF)
                        {
                            return false;  // 孤立した下位サロゲート。
                        }
                        encodeUtf8(code);
                        break;
                    }
                    default:
                        return false;  // 未知のエスケープ。
                }
            }
            return true;
        }

        /**
         * @brief wire JSON 値（純 JSON 値テキスト）を NorvesLib 内部シリアライズ表記へ逆変換する
         *
         * AppendWireValue の厳密な逆。エンジン側プロパティ型（TypeInfo）の Kind / Name に基づき、
         * wire の JSON 値テキストを SerializeValue / DeserializeStable が期待する内部テキストへ写す。
         * wire の valueType は信用せず、必ず呼び出し側がエンジン側プロパティ型から渡す TypeInfo を使う。
         *   - Bool: wire JSON `true`/`false` → "1"/"0"。数値・文字列など他は false（型不一致）。
         *   - Integer/Float: wire JSON number テキスト → trim したテキストをそのまま（number 文法
         *     検証付き、非 number は false）。
         *   - String: wire JSON 文字列 `"..."` → 引用符を剥がし JSON アンエスケープして out。
         *   - Vector2/3/4・Quaternion（TypeInfo.Name 判定）: wire JSON 配列 `[a,b,c]` → 要素数を確認し
         *     `Vector3(a,b,c)` 形式（接頭辞 Math:: なし、SerializeValue と同形）へ。要素数不一致は false。
         *   - 上記以外（Transform/enum/object/null/未知）: false。
         *
         * @param type エンジン側プロパティ型の TypeInfo（借用）
         * @param wireJson extract_json_field が返した生の wire JSON 値テキスト（借用）
         * @param out 内部シリアライズ表記の出力先（成功時のみ意味を持つ）
         * @return 逆変換できたら true（accepted 可）。型不一致や不正値は false
         */
        bool WireJsonToSerialized(const NorvesLib::Core::TypeInfo& type, std::string_view wireJson,
                                  std::string& out)
        {
            using NorvesLib::Core::TypeKind;

            const std::string_view trimmed = TrimJsonWhitespace(wireJson);
            if (trimmed.empty())
            {
                return false;
            }

            switch (type.Kind)
            {
                case TypeKind::Bool:
                {
                    if (trimmed == "true")
                    {
                        out += '1';
                        return true;
                    }
                    if (trimmed == "false")
                    {
                        out += '0';
                        return true;
                    }
                    return false;  // 数値や文字列は bool として受けない（型不一致）。
                }
                case TypeKind::Integer:
                case TypeKind::Float:
                {
                    if (!LooksLikeJsonNumber(trimmed))
                    {
                        return false;  // JSON number でなければ受けない。
                    }
                    out.append(trimmed.data(), trimmed.size());
                    return true;
                }
                case TypeKind::String:
                {
                    return DecodeJsonString(trimmed, out);  // "..." を剥がしてアンエスケープ。
                }
                case TypeKind::Struct:
                {
                    // Vector2/3/4・Quaternion のみ受ける（接頭辞は付けない＝SerializeValue と同形）。
                    std::string_view ctorName;
                    std::size_t expectedCount = 0;
                    const std::string_view typeName = ViewOf(type.Name);
                    if (typeName == "Math::Vector2")
                    {
                        ctorName = "Vector2";
                        expectedCount = 2;
                    }
                    else if (typeName == "Math::Vector3")
                    {
                        ctorName = "Vector3";
                        expectedCount = 3;
                    }
                    else if (typeName == "Math::Vector4")
                    {
                        ctorName = "Vector4";
                        expectedCount = 4;
                    }
                    else if (typeName == "Math::Quaternion")
                    {
                        ctorName = "Quaternion";
                        expectedCount = 4;
                    }
                    else
                    {
                        return false;  // Transform 等 struct は配列化対象外。
                    }

                    // wire は JSON 配列 `[a,b,...]`。括弧を確認し、カンマ区切りで要素を読む。
                    if (trimmed.front() != '[' || trimmed.back() != ']')
                    {
                        return false;
                    }
                    const std::string_view inner =
                        TrimJsonWhitespace(trimmed.substr(1, trimmed.size() - 2));
                    if (inner.empty())
                    {
                        return false;
                    }

                    std::string assembled;
                    assembled.append(ctorName.data(), ctorName.size());
                    assembled += '(';
                    std::size_t count = 0;
                    std::size_t start = 0;
                    while (start <= inner.size())
                    {
                        const std::size_t comma = inner.find(',', start);
                        const std::size_t end =
                            (comma == std::string_view::npos) ? inner.size() : comma;
                        const std::string_view component =
                            TrimJsonWhitespace(inner.substr(start, end - start));
                        if (!LooksLikeJsonNumber(component))
                        {
                            return false;  // 各要素は JSON number でなければならない。
                        }
                        if (count != 0)
                        {
                            assembled += ',';
                        }
                        assembled.append(component.data(), component.size());
                        ++count;

                        if (comma == std::string_view::npos)
                        {
                            break;
                        }
                        start = comma + 1;
                    }
                    assembled += ')';

                    if (count != expectedCount)
                    {
                        return false;  // 要素数不一致。
                    }
                    out += assembled;
                    return true;
                }
                case TypeKind::Void:
                case TypeKind::Object:
                case TypeKind::Resource:
                case TypeKind::Array:
                case TypeKind::Enum:
                case TypeKind::Custom:
                default:
                    return false;  // Transform / enum / object / null / 未知は未対応。
            }
        }
    } // namespace

    AdapterResult
    NorvesLibBridgeAdapter::hello(const JsonValue& /*params*/,
                                  std::string_view selectedProtocolVersion)
    {
        // 一意な sessionId を採番する（プロセス id + 単調カウンタ）。
        ++m_NextSessionSeq;
        std::string sessionId = "norveslib-";
        sessionId += std::to_string(static_cast<unsigned long>(::GetCurrentProcessId()));
        sessionId += "-";
        sessionId += std::to_string(static_cast<unsigned long long>(m_NextSessionSeq));

        norves::bridge::dto::HelloResult result;
        result.sessionId = std::move(sessionId);
        result.protocolVersion = std::string(selectedProtocolVersion);
        result.server = norves::bridge::dto::ServerInfo{
            "NorvesLib",
            std::optional<std::string>{"1.0"},
            std::optional<std::string>{"NorvesLib"}};
        return AdapterResult::ok(result.to_json());
    }

    AdapterResult
    NorvesLibBridgeAdapter::getCapabilities(const JsonValue& /*params*/)
    {
        // runtime.control / log.stream / viewport.focus / scene.query / object.query / object.edit を
        // 広告する。scene.query は scene.getTree と schema.getSnapshot を束ねる token（両者とも実装済み）。
        // object.query は object.getSnapshot 用（実装済み）。object.edit は object.setProperty 用
        // （実装済み）。viewport.thumbnail と scene.liveUpdate は本実装範囲外のため広告しない。
        // 実エンジンの capability 検証は superset（部分集合包含）方針なので、実装済み token のみ
        // 広告すればよい（mock の 8 token fixture には合わせない）。
        return OkLiteral(
            R"({"capabilities":[)"
            R"({"name":"runtime.control","version":"0.1","description":"Play/pause/stop control."},)"
            R"({"name":"log.stream"},)"
            R"({"name":"viewport.focus"},)"
            R"({"name":"scene.query"},)"
            R"({"name":"object.query"},)"
            R"({"name":"object.edit"}]})");
    }

    AdapterResult
    NorvesLibBridgeAdapter::getStatus(const JsonValue& /*params*/)
    {
        // 実エンジン状態を DTO スナップショットへ変換して返す。GEngine が走行中
        // （かつ終了要求なし）なら Running、そうでなければ Initializing として扱う。
        norves::bridge::dto::StatusSnapshot snap;
        const bool bRunning = (NorvesLib::Core::Engine::GEngine != nullptr) &&
                              NorvesLib::Core::Engine::GEngine->IsRunning() &&
                              !NorvesLib::Core::Engine::GEngine->IsExitRequested();
        snap.engineState = bRunning ? norves::bridge::dto::EngineState::Running
                                    : norves::bridge::dto::EngineState::Initializing;
        snap.runtimeState = (m_Handler != nullptr)
                                ? ToDtoRuntimeState(m_Handler->GetBridgeRuntimeState())
                                : norves::bridge::dto::RuntimeState::Unknown;
        snap.engineName = "NorvesLib";
        snap.engineVersion = "1.0";
        return AdapterResult::ok(snap.to_json());
    }

    AdapterResult
    NorvesLibBridgeAdapter::launchInfo(const JsonValue& /*params*/)
    {
        // スキーマ準拠 {pid, title}。{launched:true} は返さない（mock の非準拠点）。
        std::string payload = R"({"pid":)";
        payload += std::to_string(static_cast<unsigned long>(::GetCurrentProcessId()));
        payload += R"(,"title":"NorvesLib Game"})";
        return OkLiteral(payload);
    }

    AdapterResult
    NorvesLibBridgeAdapter::runtimePlay(const JsonValue& /*params*/)
    {
        // runtime 状態を Playing へ遷移させる（Tick ゲートが進行を再開する）。
        // previous は Set の前に読む。
        const auto previous = (m_Handler != nullptr) ? m_Handler->GetBridgeRuntimeState()
                                                      : Game::Bridge::BridgeRuntimeState::Edit;
        const auto next = Game::Bridge::BridgeRuntimeState::Playing;
        if (m_Handler != nullptr)
        {
            m_Handler->SetBridgeRuntimeState(next);
        }

        // 状態遷移を INFO ログへ（Logger 経由で sink -> log.message に流れる）。
        // drain の send 失敗は無言なので、この INFO ログが増幅ループになることはない。
        NORVES_LOG_INFO("Bridge", "runtime state changed: %s -> %s",
                        RuntimeStateName(previous), RuntimeStateName(next));

        // イベント先・response 後の順で runtime.stateChanged を発火する（PlayAck の前）。
        EmitRuntimeStateChanged(m_Host, previous, next);

        norves::bridge::dto::PlayAck ack;
        ack.accepted = true;
        ack.requestedState = norves::bridge::dto::RuntimeState::Playing;
        return AdapterResult::ok(ack.to_json());
    }

    AdapterResult
    NorvesLibBridgeAdapter::runtimePause(const JsonValue& /*params*/)
    {
        // runtime 状態を Paused へ遷移させる（Tick ゲートが進行を止める）。
        // previous は Set の前に読む。
        const auto previous = (m_Handler != nullptr) ? m_Handler->GetBridgeRuntimeState()
                                                      : Game::Bridge::BridgeRuntimeState::Edit;
        const auto next = Game::Bridge::BridgeRuntimeState::Paused;
        if (m_Handler != nullptr)
        {
            m_Handler->SetBridgeRuntimeState(next);
        }

        NORVES_LOG_INFO("Bridge", "runtime state changed: %s -> %s",
                        RuntimeStateName(previous), RuntimeStateName(next));

        EmitRuntimeStateChanged(m_Host, previous, next);

        norves::bridge::dto::PlayAck ack;
        ack.accepted = true;
        ack.requestedState = norves::bridge::dto::RuntimeState::Paused;
        return AdapterResult::ok(ack.to_json());
    }

    AdapterResult
    NorvesLibBridgeAdapter::runtimeStop(const JsonValue& /*params*/)
    {
        // runtime 状態を Stopped へ遷移させる（Tick ゲートが進行を止める）。
        // previous は Set の前に読む。
        const auto previous = (m_Handler != nullptr) ? m_Handler->GetBridgeRuntimeState()
                                                      : Game::Bridge::BridgeRuntimeState::Edit;
        const auto next = Game::Bridge::BridgeRuntimeState::Stopped;
        if (m_Handler != nullptr)
        {
            m_Handler->SetBridgeRuntimeState(next);
        }

        NORVES_LOG_INFO("Bridge", "runtime state changed: %s -> %s",
                        RuntimeStateName(previous), RuntimeStateName(next));

        EmitRuntimeStateChanged(m_Host, previous, next);

        norves::bridge::dto::PlayAck ack;
        ack.accepted = true;
        ack.requestedState = norves::bridge::dto::RuntimeState::Stopped;
        return AdapterResult::ok(ack.to_json());
    }

    AdapterResult
    NorvesLibBridgeAdapter::runtimeFocusViewport(const JsonValue& /*params*/)
    {
        // best-effort でエンジンのメインウィンドウを前面化する。Win32 ハンドルが
        // 有効に取得できたときだけ操作し、それ以外は {focused:false} を返す。
        bool bFocused = false;
        auto* engine = NorvesLib::Core::Engine::GEngine;
        if (engine != nullptr)
        {
            auto* window = engine->GetMainWindow();
            if (window != nullptr)
            {
                const NorvesLib::Core::Platform::NativeWindowHandle handle = window->GetNativeHandle();
                if (handle.IsValid() &&
                    handle.WindowType == NorvesLib::Core::Platform::NativeWindowHandle::Type::Win32 &&
                    handle.Handle1 != nullptr)
                {
                    HWND hwnd = reinterpret_cast<HWND>(handle.Handle1);
                    ::ShowWindow(hwnd, SW_RESTORE);
                    ::BringWindowToTop(hwnd);
                    bFocused = (::SetForegroundWindow(hwnd) != 0);
                }
            }
        }
        return OkLiteral(bFocused ? R"({"focused":true})" : R"({"focused":false})");
    }

    AdapterResult
    NorvesLibBridgeAdapter::logSubscribe(const JsonValue& /*params*/)
    {
        // Logger -> Bridge のログ転送を開始する（host が中継 sink を Logger へ登録）。
        // 単一購読前提（複数同時購読は非対応）。filter.minLevel は honor しない：
        // SDK の JsonValue には構造的な読み取りアクセス API が無い（parse/dump/is_null
        // のみ）ため、サーバー側レベルフィルタは既定 Trace（全転送）で進め、レベル
        // 絞り込みは editor 側 client フィルタへ委ねる。
        if (m_Host != nullptr)
        {
            m_Host->StartLogForwarding(NorvesLib::Core::Logging::LogLevel::Trace);
        }

        // スキーマ準拠 {subscriptionId}。{subscribed:true} は返さない（mock の非準拠点）。
        ++m_NextSubscriptionSeq;
        std::string payload = R"({"subscriptionId":"norveslib-sub-)";
        payload += std::to_string(static_cast<unsigned long long>(m_NextSubscriptionSeq));
        payload += R"("})";
        return OkLiteral(payload);
    }

    AdapterResult
    NorvesLibBridgeAdapter::logUnsubscribe(const JsonValue& /*params*/)
    {
        // ログ転送を停止する（冪等）。subscriptionId 照合はしない（単一購読前提）。
        if (m_Host != nullptr)
        {
            m_Host->StopLogForwarding();
        }

        // log.unsubscribe.result スキーマの必須フィールドは ok(boolean)。
        return OkLiteral(R"({"ok":true})");
    }

    AdapterResult
    NorvesLibBridgeAdapter::schemaGetSnapshot(const JsonValue& /*params*/)
    {
        // class スキーマ投影を値コピー済み DTO として取得する。Entity ポインタや Object
        // ポインタ、生ポインタは一切触らず、DTO（ClassSchemaSnapshot）の値だけを読む
        // （live memory 非転送）。
        const NorvesLib::Core::ClassSchemaSnapshot snapshot =
            NorvesLib::Core::RuntimeSchemaProjector::BuildClassSchemaSnapshot();

        // StableTypeId -> 型名 の対応を Types から作る。これで各プロパティの Type を人間可読な
        // 型名へ解決する。名前が空の TypeInfo は登録しない（解決不能扱いでフォールバックさせる）。
        std::unordered_map<uint64_t, std::string_view> typeNameByStableId;
        typeNameByStableId.reserve(snapshot.Types.size());
        for (const NorvesLib::Core::TypeInfo& type : snapshot.Types)
        {
            if (type.StableId != NorvesLib::Core::InvalidSchemaId && !type.Name.empty())
            {
                typeNameByStableId.emplace(static_cast<uint64_t>(type.StableId), ViewOf(type.Name));
            }
        }

        // 解決できない Type のフォールバック型名（propertyDefinition.valueType は必須・minLength:1）。
        static constexpr std::string_view kUnknownType = "unknown";

        // result wire: { "types": [ { typeName, kind, properties:[{name, valueType}] }, ... ] }。
        // typeName / プロパティ名 / 型名はすべて AppendJsonString でエスケープして綴る。
        std::string text = R"({"types":[)";
        bool bFirstClass = true;
        for (const NorvesLib::Core::ClassSchemaProjection& cls : snapshot.Classes)
        {
            if (!bFirstClass)
            {
                text += ',';
            }
            bFirstClass = false;

            text += R"({"typeName":")";
            AppendJsonString(text, ViewOf(cls.Name));
            // class 投影なので kind は "object" 固定。
            text += R"(","kind":"object","properties":[)";

            bool bFirstProp = true;
            for (const NorvesLib::Core::PropertySchemaProjection& prop : cls.Properties)
            {
                if (!bFirstProp)
                {
                    text += ',';
                }
                bFirstProp = false;

                // Type（StableTypeId）を型名へ解決する。未登録/InvalidSchemaId は "unknown"。
                std::string_view valueType = kUnknownType;
                const auto it = typeNameByStableId.find(static_cast<uint64_t>(prop.Type));
                if (it != typeNameByStableId.end())
                {
                    valueType = it->second;
                }

                text += R"({"name":")";
                AppendJsonString(text, ViewOf(prop.Name));
                text += R"(","valueType":")";
                AppendJsonString(text, valueType);
                text += R"("})";
            }

            text += R"(]})";
        }
        text += R"(]})";

        return OkLiteral(text);
    }

    AdapterResult
    NorvesLibBridgeAdapter::sceneGetTree(const JsonValue& /*params*/)
    {
        // 合成ルート（id="scene-root", kind="Scene"）配下に各ルート Entity を再帰でノード化する。
        // ObjectId 値とクラス名文字列のコピーのみを綴り、Entity ポインタや生ポインタは一切
        // JsonValue へ入れない（live memory 非転送）。params.rootId/maxDepth は無視して全ツリーを
        // 返す（本実装段の方針）。GEngine 未生成時は子なしの空ツリーを返す（scene.query 広告と整合）。
        std::string text = R"({"root":{"id":"scene-root","kind":"Scene","children":[)";

        auto* engine = NorvesLib::Core::Engine::GEngine;
        if (engine != nullptr)
        {
            const NorvesLib::Core::World& world = engine->GetWorld();
            const auto roots = world.GetRootEntities();
            bool bFirst = true;
            for (const NorvesLib::Core::Entity* root : roots)
            {
                if (root == nullptr)
                {
                    continue;
                }
                if (!bFirst)
                {
                    text += ',';
                }
                bFirst = false;
                AppendEntityNode(text, *root, 0);
            }
        }

        text += R"(]}})";
        return OkLiteral(text);
    }

    AdapterResult
    NorvesLibBridgeAdapter::objectGetSnapshot(const JsonValue& params)
    {
        // params.objectId（文字列）を読む。SDK の JsonValue に構造的読み取り API は無いため
        // dump() のコンパクト表現を最小スキャナで読む（mock と同手法）。
        const std::string paramsText = params.dump();
        const std::optional<std::string> objectIdField = extract_string_field(paramsText, "objectId");
        const std::string objectId = objectIdField.value_or(std::string{});

        // 入力 objectId をエコーする空スナップショットを組むヘルパ（graceful フォールバック用）。
        // schema 準拠の最小形 { "objectId":<入力>, "properties":[] }。objectId は minLength:1 のため
        // 入力が空でも非空のプレースホルダ "0" を出す。
        const auto buildEmpty = [](std::string_view id) -> std::string {
            std::string out = R"({"objectId":")";
            AppendJsonString(out, id.empty() ? std::string_view{"0"} : id);
            out += R"(","properties":[]})";
            return out;
        };

        // objectId 文字列 -> uint64_t。パース不可なら空スナップショット。
        uint64_t parsedId = 0;
        bool bParsed = false;
        if (!objectId.empty())
        {
            errno = 0;
            char* end = nullptr;
            const unsigned long long v = std::strtoull(objectId.c_str(), &end, 10);
            // 全体が数字で消費され、かつ少なくとも 1 桁あること（end が進んだこと）を要求。
            if (end != nullptr && *end == '\0' && end != objectId.c_str() && errno == 0)
            {
                parsedId = static_cast<uint64_t>(v);
                bParsed = true;
            }
        }

        auto* engine = NorvesLib::Core::Engine::GEngine;
        NorvesLib::Core::Entity* entity = nullptr;
        if (bParsed && engine != nullptr)
        {
            entity = FindEntityByObjectId(engine->GetWorld(), parsedId);
        }
        if (entity == nullptr)
        {
            // パース不可 or 該当 Entity 無し: graceful な空スナップショット。
            return OkLiteral(buildEmpty(objectId));
        }

        const NorvesLib::Core::IClass* cls = entity->GetClass();
        if (cls == nullptr)
        {
            return OkLiteral(buildEmpty(objectId));
        }

        // クラスのプロパティスキーマ投影から StablePropertyId -> 名前 のマップを作る。
        // ここで得る Name は値コピー済み DTO（live memory 非転送）。wire 出力の value/valueType は
        // snapshot 側の pv.Type を使うため、ここでは名前だけ引ければよい。
        const NorvesLib::Core::ClassSchemaProjection classProjection =
            NorvesLib::Core::RuntimeSchemaProjector::ProjectClass(*cls, "NorvesLib");
        std::unordered_map<uint64_t, std::string_view> nameByPropertyId;
        nameByPropertyId.reserve(classProjection.Properties.size());
        for (const NorvesLib::Core::PropertySchemaProjection& prop : classProjection.Properties)
        {
            if (prop.StableId == NorvesLib::Core::InvalidSchemaId || prop.Name.empty())
            {
                continue;  // 名前を引けないプロパティは propertyEntry を出せないので除外。
            }
            nameByPropertyId.emplace(static_cast<uint64_t>(prop.StableId), ViewOf(prop.Name));
        }

        // オブジェクトのシリアライズ済みプロパティ値スナップショットを取得する。ref は wire 出力に
        // 使わないので Id に ObjectId を入れる最小構築でよい。
        NorvesLib::Core::StableObjectRef ref;
        ref.Id = entity->GetObjectId();
        const NorvesLib::Core::ObjectSnapshot snapshot =
            NorvesLib::Core::RuntimeSchemaProjector::BuildObjectSnapshot(*entity, ref, "NorvesLib");

        // wire: { "objectId":<入力>, "kind":<クラス名>, "properties":[ {name,value,valueType} ... ] }。
        // name は省略（Entity に表示名アクセサが無い）。
        std::string text = R"({"objectId":")";
        AppendJsonString(text, objectId.empty() ? std::string_view{"0"} : std::string_view{objectId});
        text += R"(","kind":")";

        std::string_view kind = "Entity";
        const std::string_view className = ViewOf(cls->GetClassName().GetView());
        if (!className.empty())
        {
            kind = className;
        }
        AppendJsonString(text, kind);
        text += R"(","properties":[)";

        bool bFirstProp = true;
        for (const NorvesLib::Core::ProjectedPropertyValue& pv : snapshot.Properties)
        {
            const auto it = nameByPropertyId.find(static_cast<uint64_t>(pv.Property));
            if (it == nameByPropertyId.end())
            {
                continue;  // スキーマ投影に名前が無いプロパティはスキップ（name 必須）。
            }
            const std::string_view propName = it->second;

            if (!bFirstProp)
            {
                text += ',';
            }
            bFirstProp = false;

            text += R"({"name":")";
            AppendJsonString(text, propName);
            text += R"(","value":)";
            AppendWireValue(text, pv.Type, ViewOf(pv.SerializedValue));
            text += R"(,"valueType":")";
            AppendJsonString(text, ResolveTypeName(pv.Type));
            text += R"("})";
        }

        text += R"(]})";
        return OkLiteral(text);
    }

    AdapterResult
    NorvesLibBridgeAdapter::objectSetProperty(const JsonValue& params)
    {
        // 適用失敗・型不一致・該当なし等はすべて graceful に {"accepted":false} を返す
        // （エラー Result ではなく成功 Result に accepted:false を載せる＝result schema の必須形）。
        static constexpr std::string_view kRejected = R"({"accepted":false})";

        // params を dump して objectId / property / value を読む（SDK の JsonValue に構造的読み取り
        // API が無いため最小スキャナ。S2/S3 と同手法）。
        const std::string paramsText = params.dump();
        const std::optional<std::string> objectIdField = extract_string_field(paramsText, "objectId");
        const std::optional<std::string> propertyField = extract_string_field(paramsText, "property");
        const std::optional<std::string> valueField = extract_json_field(paramsText, "value");
        if (!objectIdField.has_value() || !propertyField.has_value() || !valueField.has_value())
        {
            return OkLiteral(kRejected);  // 必須 params 欠落。
        }
        const std::string& objectId = objectIdField.value();
        const std::string& propertyName = propertyField.value();
        const std::string& wireValue = valueField.value();
        if (objectId.empty() || propertyName.empty())
        {
            return OkLiteral(kRejected);
        }

        // objectId 文字列 -> uint64_t（全桁数字を要求。objectGetSnapshot と同手法）。
        uint64_t parsedId = 0;
        bool bParsed = false;
        {
            errno = 0;
            char* end = nullptr;
            const unsigned long long v = std::strtoull(objectId.c_str(), &end, 10);
            if (end != nullptr && *end == '\0' && end != objectId.c_str() && errno == 0)
            {
                parsedId = static_cast<uint64_t>(v);
                bParsed = true;
            }
        }
        if (!bParsed)
        {
            return OkLiteral(kRejected);
        }

        // World から Entity を逆引き（GEngine null / 該当なしは reject）。
        auto* engine = NorvesLib::Core::Engine::GEngine;
        if (engine == nullptr)
        {
            return OkLiteral(kRejected);
        }
        NorvesLib::Core::Entity* entity = FindEntityByObjectId(engine->GetWorld(), parsedId);
        if (entity == nullptr)
        {
            return OkLiteral(kRejected);
        }

        const NorvesLib::Core::IClass* cls = entity->GetClass();
        if (cls == nullptr)
        {
            return OkLiteral(kRejected);
        }

        // クラスから ClassProperty を引く（プロパティ名 -> Identity）。IdentityPool 経由で
        // 登録済み Identity と同じハッシュを得る（GetProperty はハッシュ一致で引く）。
        const NorvesLib::Core::Container::String propertyString(propertyName);
        const NorvesLib::Core::Identity propIdentity =
            NorvesLib::Core::IdentityPool::Get().CreateIdentity(propertyString);
        const NorvesLib::Core::ClassProperty* prop = cls->GetProperty(propIdentity);
        if (prop == nullptr)
        {
            return OkLiteral(kRejected);  // 未知プロパティ。
        }

        // プロパティ型を解決する。GetRuntimeTypeId() は実行時 TypeId なので Find(TypeId) で
        // TypeInfo* を引き、その StableId / Kind / Name を使う（wire の valueType は信用しない）。
        const NorvesLib::Core::TypeId runtimeTypeId = prop->GetRuntimeTypeId();
        const NorvesLib::Core::TypeInfo* typeInfo =
            NorvesLib::Core::TypeRegistry::Get().Find(runtimeTypeId);
        if (typeInfo == nullptr)
        {
            return OkLiteral(kRejected);  // 型未登録（適用先の型が決まらない）。
        }

        // wire JSON 値 -> NorvesLib 内部シリアライズ表記へ逆変換（AppendWireValue の逆）。
        std::string internalText;
        if (!WireJsonToSerialized(*typeInfo, wireValue, internalText))
        {
            return OkLiteral(kRejected);  // 型不一致 / 不正値 / 未対応型。
        }

        // DeserializeStable に渡す型は必ずエンジン側プロパティ型由来の StableId。
        NorvesLib::Core::PropertyValue pv;
        const NorvesLib::Core::Container::String internalString(internalText);
        if (!pv.DeserializeStable(typeInfo->StableId, internalString))
        {
            return OkLiteral(kRejected);  // 内部表記のパース失敗。
        }

        // Entity を IUnknown* として渡して適用する（Entity : Object : UnknownImpl : IUnknown）。
        // ApplyValue は PropertyValue の型がプロパティ型と一致しなければ false を返す。
        if (!prop->ApplyValue(static_cast<NorvesLib::Core::IUnknown*>(entity), pv))
        {
            return OkLiteral(kRejected);  // 適用失敗（型不一致含む）。
        }

        // Transform 系（Position/Rotation/Scale 等）を変えた場合、ワールド変換は World::Tick の
        // UpdateWorldTransforms が次フレームで反映する（既存挙動）。ここで明示呼び出しはしない。
        // 同フレーム即時の getSnapshot ではローカル値が反映される。

        // appliedValue: Entity を再投影して、実際に格納された値を読み戻して wire JSON 値へ。
        // 該当プロパティの StablePropertyId を classProjection から名前一致で引き、再 BuildObjectSnapshot
        // の SerializedValue を AppendWireValue で wire 値へ変換する（M-6 往復一致）。読み戻せない
        // 場合は appliedValue を省略する（accepted:true は維持）。
        std::string appliedValueWire;
        bool bHaveApplied = false;
        {
            const NorvesLib::Core::ClassSchemaProjection classProjection =
                NorvesLib::Core::RuntimeSchemaProjector::ProjectClass(*cls, "NorvesLib");
            NorvesLib::Core::StablePropertyId targetStableId = NorvesLib::Core::InvalidSchemaId;
            for (const NorvesLib::Core::PropertySchemaProjection& schemaProp :
                 classProjection.Properties)
            {
                if (schemaProp.StableId == NorvesLib::Core::InvalidSchemaId)
                {
                    continue;
                }
                if (ViewOf(schemaProp.Name) == std::string_view{propertyName})
                {
                    targetStableId = schemaProp.StableId;
                    break;
                }
            }

            if (targetStableId != NorvesLib::Core::InvalidSchemaId)
            {
                NorvesLib::Core::StableObjectRef ref;
                ref.Id = entity->GetObjectId();
                const NorvesLib::Core::ObjectSnapshot snapshot =
                    NorvesLib::Core::RuntimeSchemaProjector::BuildObjectSnapshot(*entity, ref,
                                                                                 "NorvesLib");
                for (const NorvesLib::Core::ProjectedPropertyValue& projected : snapshot.Properties)
                {
                    if (projected.Property == targetStableId)
                    {
                        AppendWireValue(appliedValueWire, projected.Type,
                                        ViewOf(projected.SerializedValue));
                        bHaveApplied = true;
                        break;
                    }
                }
            }
        }

        // 結果を組む。appliedValue は読み戻せたときのみ載せる（accepted は常に true）。
        std::string out = R"({"accepted":true)";
        if (bHaveApplied)
        {
            out += R"(,"appliedValue":)";
            out += appliedValueWire;
        }
        out += '}';
        return OkLiteral(out);
    }

} // namespace Game::Bridge

#endif // NORVES_BRIDGE_ENABLED
