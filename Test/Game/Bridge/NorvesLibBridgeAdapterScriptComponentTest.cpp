#include "Game/Bridge/NorvesLibBridgeAdapter.h"

#include "Component/Component.h"
#include "Component/ScriptComponent.h"
#include "Engine/Engine.h"
#include "Engine/NorvesEngine.h"
#include "Object/Entity.h"
#include "Object/World.h"
#include "Scripting/AngelScriptEngineOwner.h"
#include "Scripting/ScriptRuntime.h"

#include <angelscript.h>

#include "Norves/Bridge/codec.hpp"
#include "Norves/Bridge/server.hpp"

#include <cstdint>
#include <cstring>
#include <iostream>
#include <optional>
#include <string>
#include <string_view>

namespace
{
    using Game::Bridge::NorvesLibBridgeAdapter;
    using Norves::Bridge::Envelope;
    using Norves::Bridge::JsonValue;
    using NorvesLib::Core::Component::Component;
    using NorvesLib::Core::Component::ScriptComponent;
    using NorvesLib::Core::Engine::Engine;
    using NorvesLib::Core::Entity;
    using NorvesLib::Core::EScriptRuntimeResult;
    using NorvesLib::Core::ScriptRuntimeDiagnostics;
    using NorvesLib::Core::World;

    constexpr const char* kMoverPath = "Scripts/Test/ScriptComponentMover.as";
    constexpr const char* kMoverClass = "ScriptComponentMover";
    constexpr const char* kRetainedPath = "Scripts/Test/ScriptComponentRetainedReference.as";
    constexpr const char* kRetainedClass = "ScriptComponentRetainedReference";

    bool Check(bool bCondition, const char* message)
    {
        if (!bCondition)
        {
            std::cerr
                << "NorvesLibBridgeAdapterScriptComponentTest mismatch: "
                << message << '\n';
        }
        return bCondition;
    }

    class ScopedEngineOverride final
    {
    public:
        explicit ScopedEngineOverride(Engine& engine)
            : m_Previous(NorvesLib::Core::Engine::GEngine)
        {
            NorvesLib::Core::Engine::GEngine = &engine;
        }

        ~ScopedEngineOverride()
        {
            NorvesLib::Core::Engine::GEngine = m_Previous;
        }

    private:
        Engine* m_Previous = nullptr;
    };

    std::string BuildRequest(
        std::string_view requestId,
        std::string_view method,
        std::string_view paramsJson)
    {
        std::string request = R"({"bridge":"norves.editor.bridge","version":"0.2","kind":"request","id":")";
        request.append(requestId);
        request += R"(","method":")";
        request.append(method);
        request += R"(","params":)";
        request.append(paramsJson);
        request += '}';
        return request;
    }

    bool HandleRequest(
        Norves::Bridge::BridgeEngineServer& server,
        std::string_view requestId,
        std::string_view method,
        std::string_view paramsJson,
        std::string& outResponse)
    {
        const std::string request = BuildRequest(requestId, method, paramsJson);
        const std::optional<std::string> response = server.handleFrame(request);
        if (!Check(response.has_value(), "server returned no response"))
        {
            return false;
        }

        outResponse = response.value();
        const auto decoded = Norves::Bridge::decode_envelope(outResponse);
        if (!Check(decoded.is_ok(), "response envelope failed to decode"))
        {
            return false;
        }

        const Envelope& envelope = decoded.value();
        return Check(envelope.kind == Norves::Bridge::Kind::Response, "response envelope kind") &&
               Check(envelope.id.has_value() && envelope.id.value() == requestId,
                     "response envelope id");
    }

    bool ExpectResult(
        const std::string& response,
        std::string_view expectedJson,
        const char* message)
    {
        const auto decoded = Norves::Bridge::decode_envelope(response);
        if (!Check(decoded.is_ok(), "result response failed to decode"))
        {
            return false;
        }

        const Envelope& envelope = decoded.value();
        const auto expected = JsonValue::parse(expectedJson);
        if (!Check(expected.is_ok(), "hand-derived expected JSON failed to parse"))
        {
            return false;
        }

        return Check(envelope.result.has_value(), "success response has result") &&
               Check(!envelope.error.has_value(), "success response has no error") &&
               Check(envelope.result.value() == expected.value(), message);
    }

    // 反映投影が返すプロパティの並び順は実装依存（ハッシュ順）で、期待値として固定すると
    // 実装と無関係な理由で落ちる。契約として意味があるのは「どの欄がどの値で載るか」なので、
    // result を dump した文字列に対する包含で検査する。JsonValue::dump はキーを正規化して
    // 綴るため、{"name":...,"value":...,"valueType":...} の並びは安定する。
    bool ExpectResultContains(
        const std::string& response,
        const std::string* expectedFragments,
        uint32_t fragmentCount,
        const char* message)
    {
        const auto decoded = Norves::Bridge::decode_envelope(response);
        if (!Check(decoded.is_ok(), "result response failed to decode"))
        {
            return false;
        }

        const Envelope& envelope = decoded.value();
        if (!Check(envelope.result.has_value(), "success response has result") ||
            !Check(!envelope.error.has_value(), "success response has no error"))
        {
            return false;
        }

        const std::string dumped = envelope.result.value().dump();
        bool bPassed = true;
        for (uint32_t index = 0; index < fragmentCount; ++index)
        {
            const bool bFound = dumped.find(expectedFragments[index]) != std::string::npos;
            if (!bFound)
            {
                std::cout << "  missing fragment: " << expectedFragments[index] << "\n"
                          << "  in result: " << dumped << "\n";
            }
            bPassed = Check(bFound, message) && bPassed;
        }
        return bPassed;
    }

    bool RequestAndExpectContains(
        Norves::Bridge::BridgeEngineServer& server,
        std::string_view requestId,
        std::string_view method,
        std::string_view paramsJson,
        const std::string* expectedFragments,
        uint32_t fragmentCount,
        const char* message)
    {
        std::string response;
        return HandleRequest(server, requestId, method, paramsJson, response) &&
               ExpectResultContains(response, expectedFragments, fragmentCount, message);
    }

    // object.getSnapshot を投げ、dump した result に fragment が含まれるかを返す。
    // 「載っていないこと」の検査に使う（含まれていれば true）。
    bool ResultContains(
        Norves::Bridge::BridgeEngineServer& server,
        std::string_view requestId,
        std::string_view paramsJson,
        std::string_view fragment)
    {
        std::string response;
        if (!HandleRequest(server, requestId, "object.getSnapshot", paramsJson, response))
        {
            return false;
        }
        const auto decoded = Norves::Bridge::decode_envelope(response);
        if (!decoded.is_ok() || !decoded.value().result.has_value())
        {
            return false;
        }
        return decoded.value().result.value().dump().find(std::string(fragment)) !=
               std::string::npos;
    }

    bool RequestAndExpect(
        Norves::Bridge::BridgeEngineServer& server,
        std::string_view requestId,
        std::string_view method,
        std::string_view paramsJson,
        std::string_view expectedJson,
        const char* message)
    {
        std::string response;
        return HandleRequest(server, requestId, method, paramsJson, response) &&
               ExpectResult(response, expectedJson, message);
    }

    std::string MakeEntityId(uint64_t objectId)
    {
        return std::to_string(static_cast<unsigned long long>(objectId));
    }

    std::string MakeComponentObjectId(uint64_t ownerId, uint64_t componentId)
    {
        return "component:" +
               std::to_string(static_cast<unsigned long long>(ownerId)) +
               ":" +
               std::to_string(static_cast<unsigned long long>(componentId));
    }

    std::string AddDecimal(std::string value, uint64_t amount)
    {
        std::string addend = std::to_string(static_cast<unsigned long long>(amount));
        std::size_t valueIndex = value.size();
        std::size_t addendIndex = addend.size();
        unsigned int carry = 0;
        while (addendIndex != 0)
        {
            const unsigned int left = static_cast<unsigned int>(value[--valueIndex] - '0');
            const unsigned int right = static_cast<unsigned int>(addend[--addendIndex] - '0');
            const unsigned int sum = left + right + carry;
            value[valueIndex] = static_cast<char>('0' + (sum % 10u));
            carry = sum / 10u;
        }
        while (carry != 0 && valueIndex != 0)
        {
            const unsigned int sum = static_cast<unsigned int>(value[--valueIndex] - '0') + carry;
            value[valueIndex] = static_cast<char>('0' + (sum % 10u));
            carry = sum / 10u;
        }
        if (carry != 0)
        {
            value.insert(value.begin(), static_cast<char>('0' + carry));
        }
        return value;
    }

    std::string EmptySnapshot(std::string_view objectId)
    {
        return R"({"objectId":")" + std::string(objectId) + R"(","properties":[]})";
    }

    // Entity のスナップショットに必ず載るもの。プロパティ本体の並びは実装依存なので、
    // ここでは「どの欄がどの値か」と components の中身だけを固定する。
    uint32_t EntitySnapshotFragments(
        std::string_view objectId,
        std::string_view name,
        const std::string& scriptComponentId,
        const std::string& baseComponentId,
        std::string* out)
    {
        out[0] = R"("objectId":")" + std::string(objectId) + R"(")";
        out[1] = R"("kind":"Entity")";
        out[2] = R"({"name":"Name","value":")" + std::string(name) + R"(","valueType":"String"})";
        // 数値プロパティは dump の表記（整数か小数か）が型に依存するので断片にしない。
        // components は Entity のときだけ載り、id は解決経路が受け付ける形と同一。
        out[3] = R"({"kind":"ScriptComponent","objectId":")" + scriptComponentId + R"("})";
        out[4] = R"({"kind":"Component","objectId":")" + baseComponentId + R"("})";
        return 5;
    }

    Entity* FindEntityByObjectId(Entity* entity, uint64_t objectId)
    {
        if (entity == nullptr)
        {
            return nullptr;
        }
        if (entity->GetObjectId() == objectId)
        {
            return entity;
        }

        for (Entity* child : entity->GetChildEntities())
        {
            if (Entity* found = FindEntityByObjectId(child, objectId))
            {
                return found;
            }
        }
        return nullptr;
    }

    Entity* FindEntityByObjectId(World& world, uint64_t objectId)
    {
        for (Entity* root : world.GetRootEntities())
        {
            if (Entity* found = FindEntityByObjectId(root, objectId))
            {
                return found;
            }
        }
        return nullptr;
    }

    bool HasChildEntity(const Entity& parent, const Entity* expectedChild)
    {
        for (Entity* child : parent.GetChildEntities())
        {
            if (child == expectedChild)
            {
                return true;
            }
        }
        return false;
    }

    // ScriptComponent のスナップショットに必ず載るもの。コンポーネントは Entity と同じ
    // 反映投影経路で綴られるので、ScriptComponent 自身の 2 欄に加えて Component 基底の
    // 欄（ComponentId 等）も載る。valueType は TypeInfo::Name 由来なので String（大文字）。
    uint32_t ScriptSnapshotFragments(
        std::string_view objectId,
        std::string_view scriptPath,
        std::string_view scriptClassName,
        std::string* out)
    {
        out[0] = R"("objectId":")" + std::string(objectId) + R"(")";
        out[1] = R"("kind":"ScriptComponent")";
        out[2] = R"({"name":"ScriptPath","value":")" + std::string(scriptPath) +
                 R"(","valueType":"String"})";
        out[3] = R"({"name":"ScriptClassName","value":")" + std::string(scriptClassName) +
                 R"(","valueType":"String"})";
        out[4] = R"("name":"ComponentId")";
        return 5;
    }

    struct Fixture final
    {
        Fixture()
            : BridgeEngine()
            , EngineOverride(BridgeEngine)
            , TestWorld(BridgeEngine.GetWorld())
            , Adapter()
            , Server(Adapter, nullptr)
        {
            TestWorld.Initialize();
            bInitialized = NorvesLib::Core::GEngine.GetScriptRuntime().Initialize(TestWorld) ==
                           EScriptRuntimeResult::Success;
            if (!bInitialized)
            {
                return;
            }

            OwnerA = TestWorld.SpawnEntity<Entity>();
            OwnerB = TestWorld.SpawnEntity<Entity>();
            if (OwnerA == nullptr || OwnerB == nullptr)
            {
                return;
            }

            Script = new ScriptComponent();
            Script->getScriptPath() = NorvesLib::Core::Container::String(kMoverPath);
            Script->getScriptClassName() = NorvesLib::Core::Container::String(kMoverClass);
            if (!OwnerA->AddComponent(Script))
            {
                delete Script;
                Script = nullptr;
                return;
            }

            Base = new Component();
            if (!OwnerA->AddComponent(Base))
            {
                delete Base;
                Base = nullptr;
                return;
            }

            bReady = NorvesLib::Core::GEngine.GetScriptRuntime().GetDiagnostics().ActiveBindingCount == 1;
        }

        ~Fixture()
        {
            if (!bCleanedUp)
            {
                Cleanup();
            }
        }

        bool IsReady() const
        {
            return bInitialized && bReady && OwnerA != nullptr && OwnerB != nullptr &&
                   Script != nullptr && Base != nullptr;
        }

        bool Cleanup()
        {
            if (bCleanedUp)
            {
                return bCleanupSucceeded;
            }

            TestWorld.Finalize();
            bCleanupSucceeded =
                NorvesLib::Core::GEngine.GetScriptRuntime().GetDiagnostics().ActiveBindingCount == 0 &&
                NorvesLib::Core::GEngine.GetScriptRuntime().Shutdown() == EScriptRuntimeResult::Success;
            bCleanedUp = true;
            return bCleanupSucceeded;
        }

        Engine BridgeEngine;
        ScopedEngineOverride EngineOverride;
        World& TestWorld;
        Entity* OwnerA = nullptr;
        Entity* OwnerB = nullptr;
        ScriptComponent* Script = nullptr;
        Component* Base = nullptr;
        NorvesLibBridgeAdapter Adapter;
        Norves::Bridge::BridgeEngineServer Server;
        bool bInitialized = false;
        bool bReady = false;
        bool bCleanedUp = false;
        bool bCleanupSucceeded = false;
    };

    bool TestComponentSurfaceLoopback()
    {
        Fixture fixture;
        bool bPassed = Check(fixture.IsReady(), "fixture initialization");
        if (!bPassed)
        {
            return fixture.Cleanup() && bPassed;
        }

        const std::string ownerAId = MakeEntityId(fixture.OwnerA->GetObjectId());
        const std::string ownerBId = MakeEntityId(fixture.OwnerB->GetObjectId());
        const std::string scriptId = MakeComponentObjectId(
            fixture.OwnerA->GetObjectId(), fixture.Script->GetComponentId());
        const std::string baseId = MakeComponentObjectId(
            fixture.OwnerA->GetObjectId(), fixture.Base->GetComponentId());

        std::string fragments[8];
        uint32_t fragmentCount = ScriptSnapshotFragments(scriptId, kMoverPath, kMoverClass, fragments);
        bPassed = RequestAndExpectContains(
            fixture.Server, "component-snapshot", "object.getSnapshot",
            R"({"objectId":")" + scriptId + R"("})",
            fragments, fragmentCount,
            "ScriptComponent snapshot surface") && bPassed;

        // 素の Component も反映投影で綴られる（以前は空のプロパティバッグだった）。
        // 入れ子は無いので components は付かない。
        std::string baseFragments[3];
        baseFragments[0] = R"("objectId":")" + baseId + R"(")";
        baseFragments[1] = R"("kind":"Component")";
        baseFragments[2] = R"("name":"ComponentId")";
        bPassed = RequestAndExpectContains(
            fixture.Server, "base-snapshot", "object.getSnapshot",
            R"({"objectId":")" + baseId + R"("})",
            baseFragments, 3,
            "base Component snapshot surface") && bPassed;
        bPassed = Check(!ResultContains(fixture.Server, "base-snapshot-components",
                                        R"({"objectId":")" + baseId + R"("})",
                                        R"("components")"),
                        "component snapshot carries no nested components") && bPassed;

        const ScriptRuntimeDiagnostics beforeSets =
            NorvesLib::Core::GEngine.GetScriptRuntime().GetDiagnostics();
        bPassed = RequestAndExpect(
            fixture.Server, "set-path", "object.setProperty",
            R"({"objectId":")" + scriptId + R"(","property":"ScriptPath","value":"Scripts/Test/ScriptComponentRetainedReference.as"})",
            R"({"accepted":true,"appliedValue":"Scripts/Test/ScriptComponentRetainedReference.as"})",
            "ScriptPath set response") && bPassed;
        bPassed = Check(static_cast<NorvesLib::Core::Container::String>(fixture.Script->getScriptPath()) == kRetainedPath,
                        "ScriptPath accessor after set") && bPassed;
        bPassed = Check(NorvesLib::Core::GEngine.GetScriptRuntime().GetDiagnostics().ReloadGeneration ==
                            beforeSets.ReloadGeneration,
                        "ScriptPath set does not reload before maintenance") && bPassed;
        bPassed = RequestAndExpect(
            fixture.Server, "set-class", "object.setProperty",
            R"({"objectId":")" + scriptId + R"(","property":"ScriptClassName","value":"ScriptComponentRetainedReference"})",
            R"({"accepted":true,"appliedValue":"ScriptComponentRetainedReference"})",
            "ScriptClassName set response") && bPassed;
        bPassed = Check(static_cast<NorvesLib::Core::Container::String>(fixture.Script->getScriptClassName()) == kRetainedClass,
                        "ScriptClassName accessor after set") && bPassed;

        const char* escapedPath = "Scripts/Test/Say\"Hi\\There.as";
        const char* escapedPathJson = "Scripts/Test/Say\\\"Hi\\\\There.as";
        bPassed = RequestAndExpect(
            fixture.Server, "set-escaped", "object.setProperty",
            R"({"objectId":")" + scriptId + R"(","property":"ScriptPath","value":"Scripts\/Test\/\u0053ay\"Hi\\There.as"})",
            R"({"accepted":true,"appliedValue":"Scripts/Test/Say\"Hi\\There.as"})",
            "escaped ScriptPath set response") && bPassed;
        bPassed = Check(static_cast<NorvesLib::Core::Container::String>(fixture.Script->getScriptPath()) == escapedPath,
                        "escaped ScriptPath accessor") && bPassed;
        fragmentCount = ScriptSnapshotFragments(scriptId, escapedPathJson, kRetainedClass, fragments);
        bPassed = RequestAndExpectContains(
            fixture.Server, "escaped-snapshot", "object.getSnapshot",
            R"({"objectId":")" + scriptId + R"("})",
            fragments, fragmentCount,
            "escaped ScriptComponent snapshot") && bPassed;

        bPassed = RequestAndExpect(
            fixture.Server, "restore-path", "object.setProperty",
            R"({"objectId":")" + scriptId + R"(","property":"ScriptPath","value":"Scripts/Test/ScriptComponentRetainedReference.as"})",
            R"({"accepted":true,"appliedValue":"Scripts/Test/ScriptComponentRetainedReference.as"})",
            "ScriptPath restore response") && bPassed;
        bPassed = RequestAndExpect(
            fixture.Server, "restore-class", "object.setProperty",
            R"({"objectId":")" + scriptId + R"(","property":"ScriptClassName","value":"ScriptComponentRetainedReference"})",
            R"({"accepted":true,"appliedValue":"ScriptComponentRetainedReference"})",
            "ScriptClassName restore response") && bPassed;
        fragmentCount = ScriptSnapshotFragments(scriptId, kRetainedPath, kRetainedClass, fragments);
        bPassed = RequestAndExpectContains(
            fixture.Server, "restored-snapshot", "object.getSnapshot",
            R"({"objectId":")" + scriptId + R"("})",
            fragments, fragmentCount,
            "restored ScriptComponent snapshot") && bPassed;
        bPassed = Check(static_cast<NorvesLib::Core::Container::String>(fixture.Script->getScriptPath()) == kRetainedPath &&
                            static_cast<NorvesLib::Core::Container::String>(fixture.Script->getScriptClassName()) == kRetainedClass,
                        "restored ScriptComponent accessors") && bPassed;
        bPassed = Check(NorvesLib::Core::GEngine.GetScriptRuntime().GetDiagnostics().ReloadGeneration ==
                            beforeSets.ReloadGeneration,
                        "restored sets do not reload before maintenance") && bPassed;

        // bEnabled / bTickEnabled は反映に載る通常のプロパティなので、汎用化後は編集できる
        // （コンポーネントの有効/無効はエディタの正当な操作）。逆に ComponentId と
        // bBegunPlay は識別子と寿命状態なのでアダプタが明示的に拒否する。
        const char* rejectedParams[] =
        {
            R"("property":"ComponentId","value":9)",
            R"("property":"bBegunPlay","value":false)",
            R"("property":"ScriptPath","value":42)",
            R"("property":"ScriptClassName","value":null)",
            R"("property":"ScriptClassName","value":{})"
        };
        for (uint32_t index = 0; index < static_cast<uint32_t>(sizeof(rejectedParams) / sizeof(rejectedParams[0])); ++index)
        {
            bPassed = RequestAndExpect(
                fixture.Server, "rejected-set-" + std::to_string(index), "object.setProperty",
                R"({"objectId":")" + scriptId + R"(",)" + rejectedParams[index] + '}',
                R"({"accepted":false})", "rejected ScriptComponent set") && bPassed;
            bPassed = Check(static_cast<NorvesLib::Core::Container::String>(fixture.Script->getScriptPath()) == kRetainedPath &&
                                static_cast<NorvesLib::Core::Container::String>(fixture.Script->getScriptClassName()) == kRetainedClass,
                            "rejected set preserves ScriptComponent") && bPassed;
        }

        // 受理側: コンポーネントの有効/無効はエディタの正当な操作として通る。ライブの
        // ScriptComponent を止めると後続の Tick 検査に波及するので、素の Component で確かめる。
        const char* acceptedBaseParams[] =
        {
            R"("property":"bEnabled","value":false)",
            R"("property":"bTickEnabled","value":false)"
        };
        for (uint32_t index = 0; index < 2u; ++index)
        {
            bPassed = RequestAndExpect(
                fixture.Server, "accepted-base-set-" + std::to_string(index), "object.setProperty",
                R"({"objectId":")" + baseId + R"(",)" + acceptedBaseParams[index] + '}',
                R"({"accepted":true,"appliedValue":false})", "accepted base Component set") && bPassed;
        }

        const std::string overflowOwnerId = AddDecimal("18446744073709551616", fixture.OwnerA->GetObjectId());
        const std::string overflowComponentId = AddDecimal("18446744073709551616", fixture.Script->GetComponentId());
        const std::string invalidEntityIds[] =
        {
            "",
            "+" + ownerAId,
            "-" + ownerAId,
            " " + ownerAId,
            ownerAId + " ",
            overflowOwnerId
        };
        const std::string invalidComponentIds[] =
        {
            "component:",
            "component:" + ownerAId + ":",
            "component::" + MakeEntityId(fixture.Script->GetComponentId()),
            "component:+" + ownerAId + ":" + MakeEntityId(fixture.Script->GetComponentId()),
            "component:" + ownerAId + ":-" + MakeEntityId(fixture.Script->GetComponentId()),
            "component: " + ownerAId + ":" + MakeEntityId(fixture.Script->GetComponentId()),
            "component:" + ownerAId + ":" + MakeEntityId(fixture.Script->GetComponentId()) + " ",
            "component:" + ownerAId + ":" + MakeEntityId(fixture.Script->GetComponentId()) + ":3",
            "component:" + overflowOwnerId + ":" + MakeEntityId(fixture.Script->GetComponentId()),
            "component:" + ownerAId + ":" + overflowComponentId
        };
        const NorvesLib::Core::Container::String entityNameBeforeInvalid =
            static_cast<NorvesLib::Core::Container::String>(fixture.OwnerA->getName());
        for (uint32_t index = 0; index < static_cast<uint32_t>(sizeof(invalidEntityIds) / sizeof(invalidEntityIds[0])); ++index)
        {
            bPassed = RequestAndExpect(
                fixture.Server, "invalid-entity-snapshot-" + std::to_string(index), "object.getSnapshot",
                R"({"objectId":")" + invalidEntityIds[index] + R"("})",
                EmptySnapshot(invalidEntityIds[index].empty() ? "0" : invalidEntityIds[index]),
                "strict invalid Entity id snapshot") && bPassed;
            bPassed = RequestAndExpect(
                fixture.Server, "invalid-entity-set-" + std::to_string(index), "object.setProperty",
                R"({"objectId":")" + invalidEntityIds[index] + R"(","property":"Name","value":"must not apply"})",
                R"({"accepted":false})", "strict invalid Entity id set") && bPassed;
            bPassed = Check(static_cast<NorvesLib::Core::Container::String>(fixture.OwnerA->getName()) == entityNameBeforeInvalid,
                            "invalid Entity id preserves live Entity property") && bPassed;
        }
        for (uint32_t index = 0; index < static_cast<uint32_t>(sizeof(invalidComponentIds) / sizeof(invalidComponentIds[0])); ++index)
        {
            bPassed = RequestAndExpect(
                fixture.Server, "invalid-component-snapshot-" + std::to_string(index), "object.getSnapshot",
                R"({"objectId":")" + invalidComponentIds[index] + R"("})",
                EmptySnapshot(invalidComponentIds[index]), "strict invalid component id snapshot") && bPassed;
            bPassed = RequestAndExpect(
                fixture.Server, "invalid-component-set-" + std::to_string(index), "object.setProperty",
                R"({"objectId":")" + invalidComponentIds[index] + R"(","property":"ScriptPath","value":"must not apply"})",
                R"({"accepted":false})", "strict invalid component id set") && bPassed;
            bPassed = Check(static_cast<NorvesLib::Core::Container::String>(fixture.Script->getScriptPath()) == kRetainedPath &&
                                static_cast<NorvesLib::Core::Container::String>(fixture.Script->getScriptClassName()) == kRetainedClass,
                            "invalid component id preserves live ScriptComponent property") && bPassed;
        }

        const std::string missingOwnerId = "999999999";
        const std::string missingIds[] =
        {
            MakeComponentObjectId(fixture.OwnerA->GetObjectId(), fixture.Base->GetComponentId() + 1000),
            MakeComponentObjectId(999999999u, fixture.Script->GetComponentId()),
            missingOwnerId,
            MakeComponentObjectId(fixture.OwnerB->GetObjectId(), fixture.Script->GetComponentId())
        };
        const std::size_t rootCountBeforeMissing = fixture.TestWorld.GetObjectCount();
        const std::size_t rootEntityCountBeforeMissing = fixture.TestWorld.GetRootEntities().size();
        const std::size_t ownerAComponentsBeforeMissing = fixture.OwnerA->GetComponents().size();
        const std::size_t ownerBComponentsBeforeMissing = fixture.OwnerB->GetComponents().size();
        for (uint32_t index = 0; index < static_cast<uint32_t>(sizeof(missingIds) / sizeof(missingIds[0])); ++index)
        {
            bPassed = RequestAndExpect(
                fixture.Server, "missing-snapshot-" + std::to_string(index), "object.getSnapshot",
                R"({"objectId":")" + missingIds[index] + R"("})",
                EmptySnapshot(missingIds[index]), "missing target snapshot") && bPassed;
            bPassed = RequestAndExpect(
                fixture.Server, "missing-set-" + std::to_string(index), "object.setProperty",
                R"({"objectId":")" + missingIds[index] + R"(","property":"ScriptPath","value":"must not apply"})",
                R"({"accepted":false})", "missing target set") && bPassed;
            bPassed = Check(static_cast<NorvesLib::Core::Container::String>(fixture.Script->getScriptPath()) == kRetainedPath &&
                                static_cast<NorvesLib::Core::Container::String>(fixture.Script->getScriptClassName()) == kRetainedClass,
                            "missing target preserves ScriptComponent property") && bPassed;
        }
        bPassed = Check(fixture.TestWorld.GetObjectCount() == rootCountBeforeMissing &&
                            fixture.TestWorld.GetRootEntities().size() == rootEntityCountBeforeMissing &&
                            fixture.OwnerA->GetComponents().size() == ownerAComponentsBeforeMissing &&
                            fixture.OwnerB->GetComponents().size() == ownerBComponentsBeforeMissing &&
                            fixture.OwnerA->GetParentEntity() == nullptr && fixture.OwnerB->GetParentEntity() == nullptr &&
                            static_cast<NorvesLib::Core::Container::String>(fixture.Script->getScriptPath()) == kRetainedPath &&
                            static_cast<NorvesLib::Core::Container::String>(fixture.Script->getScriptClassName()) == kRetainedClass,
                        "missing targets preserve world tree") && bPassed;

        const std::string structuralMethods[] =
        {
            "scene.createObject",
            "scene.deleteObject",
            "scene.reparentObject",
            "scene.reparentObject",
            "scene.duplicateObject",
            "scene.duplicateObject"
        };
        const std::string structuralParams[] =
        {
            R"({"parentId":")" + scriptId + R"("})",
            R"({"objectId":")" + scriptId + R"("})",
            R"({"objectId":")" + scriptId + R"("})",
            R"({"objectId":")" + ownerAId + R"(","newParentId":")" + scriptId + R"("})",
            R"({"objectId":")" + scriptId + R"("})",
            R"({"objectId":")" + ownerAId + R"(","newParentId":")" + scriptId + R"("})"
        };
        const std::size_t rootCountBeforeStructural = fixture.TestWorld.GetObjectCount();
        const std::size_t componentCountBeforeStructural = fixture.OwnerA->GetComponents().size();
        for (uint32_t index = 0; index < static_cast<uint32_t>(sizeof(structuralMethods) / sizeof(structuralMethods[0])); ++index)
        {
            bPassed = RequestAndExpect(
                fixture.Server, "structural-reject-" + std::to_string(index), structuralMethods[index],
                structuralParams[index], R"({"accepted":false})", "structural component id rejection") && bPassed;
        }
        bPassed = Check(fixture.TestWorld.GetObjectCount() == rootCountBeforeStructural &&
                            fixture.OwnerA->GetComponents().size() == componentCountBeforeStructural &&
                            fixture.Script->GetOwner() == fixture.OwnerA &&
                            fixture.Script->GetOuter() == fixture.OwnerA &&
                            static_cast<NorvesLib::Core::Container::String>(fixture.Script->getScriptPath()) == kRetainedPath &&
                            static_cast<NorvesLib::Core::Container::String>(fixture.Script->getScriptClassName()) == kRetainedClass,
                        "structural rejects preserve component ownership") && bPassed;

        const ScriptRuntimeDiagnostics beforeMaintenance =
            NorvesLib::Core::GEngine.GetScriptRuntime().GetDiagnostics();
        asIScriptEngine* activeEngine = NorvesLib::Core::Scripting::GetActiveAngelScriptEngine();
        bPassed = Check(activeEngine != nullptr, "active AngelScript engine") && bPassed;
        const uint32_t moduleCountBefore = activeEngine == nullptr ? 0u :
            static_cast<uint32_t>(activeEngine->GetModuleCount());
        const EScriptRuntimeResult maintenanceResult =
            NorvesLib::Core::GEngine.GetScriptRuntime().BeginFrameMaintenance(250.0f / 1000.0f);
        const ScriptRuntimeDiagnostics afterMaintenance =
            NorvesLib::Core::GEngine.GetScriptRuntime().GetDiagnostics();
        bPassed = Check(maintenanceResult == EScriptRuntimeResult::Success, "maintenance result") && bPassed;
        bPassed = Check(afterMaintenance.ReloadGeneration == beforeMaintenance.ReloadGeneration + 1,
                        "maintenance reload generation") && bPassed;
        bPassed = Check(afterMaintenance.ActiveBindingCount == beforeMaintenance.ActiveBindingCount,
                        "maintenance active binding count") && bPassed;
        bPassed = Check(activeEngine != nullptr &&
                            static_cast<uint32_t>(activeEngine->GetModuleCount()) == moduleCountBefore,
                        "maintenance module count") && bPassed;

        bPassed = RequestAndExpect(
            fixture.Server, "bad-class", "object.setProperty",
            R"({"objectId":")" + scriptId + R"(","property":"ScriptClassName","value":"MissingClass"})",
            R"({"accepted":true,"appliedValue":"MissingClass"})",
            "bad class set response") && bPassed;
        const ScriptRuntimeDiagnostics beforeBadMaintenance =
            NorvesLib::Core::GEngine.GetScriptRuntime().GetDiagnostics();
        const uint32_t moduleCountBeforeBad = activeEngine == nullptr ? 0u :
            static_cast<uint32_t>(activeEngine->GetModuleCount());
        const EScriptRuntimeResult badMaintenanceResult =
            NorvesLib::Core::GEngine.GetScriptRuntime().BeginFrameMaintenance(250.0f / 1000.0f);
        const ScriptRuntimeDiagnostics afterBadMaintenance =
            NorvesLib::Core::GEngine.GetScriptRuntime().GetDiagnostics();
        bPassed = Check(badMaintenanceResult == EScriptRuntimeResult::BindFailed,
                        "bad config maintenance result") && bPassed;
        bPassed = Check(afterBadMaintenance.ReloadGeneration == beforeBadMaintenance.ReloadGeneration &&
                            afterBadMaintenance.ActiveBindingCount == beforeBadMaintenance.ActiveBindingCount &&
                            activeEngine != nullptr &&
                            static_cast<uint32_t>(activeEngine->GetModuleCount()) == moduleCountBeforeBad,
                        "bad config preserves runtime state") && bPassed;
        const float positionBeforeTick = fixture.OwnerA->GetPosition().x;
        fixture.TestWorld.Tick(1.0f);
        bPassed = Check(fixture.OwnerA->GetPosition().x == positionBeforeTick + 1.0f,
                        "bad config preserves retained live binding") && bPassed;

        return Check(fixture.Cleanup(), "fixture cleanup") && bPassed;
    }

    bool TestNumericEntityRegression()
    {
        Fixture fixture;
        bool bPassed = Check(fixture.IsReady(), "numeric fixture initialization");
        if (!bPassed)
        {
            return fixture.Cleanup() && bPassed;
        }

        const std::string ownerAId = MakeEntityId(fixture.OwnerA->GetObjectId());
        const std::string ownerBId = MakeEntityId(fixture.OwnerB->GetObjectId());
        const std::string zeroPaddedOwnerA = "000" + ownerAId;
        const std::string numericScriptId = MakeComponentObjectId(
            fixture.OwnerA->GetObjectId(), fixture.Script->GetComponentId());
        const std::string numericBaseId = MakeComponentObjectId(
            fixture.OwnerA->GetObjectId(), fixture.Base->GetComponentId());
        std::string entityFragments[8];
        uint32_t entityFragmentCount = EntitySnapshotFragments(
            ownerAId, "", numericScriptId, numericBaseId, entityFragments);
        bPassed = RequestAndExpectContains(
            fixture.Server, "numeric-snapshot", "object.getSnapshot",
            R"({"objectId":")" + ownerAId + R"("})",
            entityFragments, entityFragmentCount,
            "numeric Entity snapshot document") && bPassed;
        entityFragmentCount = EntitySnapshotFragments(
            zeroPaddedOwnerA, "", numericScriptId, numericBaseId, entityFragments);
        bPassed = RequestAndExpectContains(
            fixture.Server, "numeric-padded-snapshot", "object.getSnapshot",
            R"({"objectId":")" + zeroPaddedOwnerA + R"("})",
            entityFragments, entityFragmentCount,
            "zero-padded numeric Entity snapshot document") && bPassed;

        bPassed = RequestAndExpect(
            fixture.Server, "numeric-set", "object.setProperty",
            R"({"objectId":")" + ownerAId + R"(","property":"Name","value":"Numeric Name"})",
            R"({"accepted":true,"appliedValue":"Numeric Name"})",
            "numeric Entity property set") && bPassed;
        bPassed = Check(static_cast<NorvesLib::Core::Container::String>(fixture.OwnerA->getName()) == "Numeric Name",
                        "numeric Entity Name accessor") && bPassed;

        const std::size_t rootEntityCountBeforeCreate = fixture.TestWorld.GetObjectCount();
        const std::size_t ownerAChildCountBeforeCreate = fixture.OwnerA->GetChildEntities().size();
        std::string createResponse;
        bPassed = HandleRequest(
            fixture.Server, "numeric-create", "scene.createObject",
            R"({"parentId":")" + ownerAId + R"("})", createResponse) && bPassed;
        const auto ownerAChildrenAfterCreate = fixture.OwnerA->GetChildEntities();
        Entity* created = ownerAChildrenAfterCreate.size() == ownerAChildCountBeforeCreate + 1 ?
            ownerAChildrenAfterCreate[ownerAChildCountBeforeCreate] : nullptr;
        const std::string createdId = created == nullptr ? std::string{} : MakeEntityId(created->GetObjectId());
        const uint64_t createdObjectId = created == nullptr ? 0u : created->GetObjectId();
        bPassed = ExpectResult(createResponse, R"({"accepted":true,"newId":")" + createdId + R"("})",
                                "numeric scene create response") && bPassed;
        bPassed = Check(created != nullptr && created->GetParentEntity() == fixture.OwnerA &&
                            fixture.TestWorld.GetObjectCount() == rootEntityCountBeforeCreate &&
                            HasChildEntity(*fixture.OwnerA, created) &&
                            FindEntityByObjectId(fixture.TestWorld, created->GetObjectId()) == created,
                        "numeric scene create world child and parent") && bPassed;
        bPassed = RequestAndExpect(
            fixture.Server, "numeric-delete", "scene.deleteObject",
            R"({"objectId":")" + createdId + R"("})", R"({"accepted":true})", "numeric scene delete") && bPassed;
        bPassed = Check(fixture.TestWorld.GetObjectCount() == rootEntityCountBeforeCreate &&
                            fixture.OwnerA->GetChildEntities().size() == ownerAChildCountBeforeCreate &&
                            FindEntityByObjectId(fixture.TestWorld, createdObjectId) == nullptr,
                        "numeric scene delete removes created Entity") && bPassed;
        bPassed = RequestAndExpect(
            fixture.Server, "numeric-reparent", "scene.reparentObject",
            R"({"objectId":")" + ownerBId + R"(","newParentId":")" + ownerAId + R"("})",
            R"({"accepted":true})", "numeric scene reparent") && bPassed;
        bPassed = Check(fixture.OwnerB->GetParentEntity() == fixture.OwnerA,
                        "numeric reparent owner") && bPassed;
        bPassed = Check(HasChildEntity(*fixture.OwnerA, fixture.OwnerB),
                        "numeric reparent world child") && bPassed;
        const std::size_t ownerAChildCountBeforeDuplicate = fixture.OwnerA->GetChildEntities().size();
        std::string duplicateResponse;
        bPassed = HandleRequest(
            fixture.Server, "numeric-duplicate", "scene.duplicateObject",
            R"({"objectId":")" + ownerBId + R"(","newParentId":")" + ownerAId + R"("})", duplicateResponse) && bPassed;
        const auto ownerAChildrenAfterDuplicate = fixture.OwnerA->GetChildEntities();
        Entity* duplicate = nullptr;
        for (Entity* child : ownerAChildrenAfterDuplicate)
        {
            if (child != fixture.OwnerB)
            {
                duplicate = child;
                break;
            }
        }
        const std::string duplicateId = duplicate == nullptr ? std::string{} :
            MakeEntityId(duplicate->GetObjectId());
        bPassed = ExpectResult(duplicateResponse, R"({"accepted":true,"newId":")" + duplicateId + R"("})",
                                "numeric scene duplicate response") && bPassed;
        bPassed = Check(duplicate != nullptr && duplicate->GetParentEntity() == fixture.OwnerA &&
                            fixture.OwnerA->GetChildEntities().size() == ownerAChildCountBeforeDuplicate + 1 &&
                            HasChildEntity(*fixture.OwnerA, duplicate) &&
                            FindEntityByObjectId(fixture.TestWorld, duplicate->GetObjectId()) == duplicate,
                        "numeric scene duplicate entity and parent") && bPassed;

        return Check(fixture.Cleanup(), "numeric fixture cleanup") && bPassed;
    }
} // namespace

int main(int argumentCount, char** arguments)
{
    const bool bNumericRegression = argumentCount == 2 &&
                                    std::strcmp(arguments[1], "--numeric-regression") == 0;
    if (argumentCount != 1 && !bNumericRegression)
    {
        std::cerr << "NorvesLibBridgeAdapterScriptComponentTest invalid arguments\n";
        return 1;
    }

    const bool bPassed = bNumericRegression ? TestNumericEntityRegression() : TestComponentSurfaceLoopback();
    if (!bPassed)
    {
        return 1;
    }

    std::cout << "NorvesLibBridgeAdapterScriptComponentTest passed\n";
    return 0;
}
