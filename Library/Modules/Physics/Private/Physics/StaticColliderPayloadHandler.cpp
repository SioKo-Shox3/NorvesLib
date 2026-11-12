// セルごとの JSON をワーカーで検証し、GameThread で静的コライダー群を登録する。
#include "Physics/StaticColliderPayloadHandler.h"

#include "Asset/AssetFileReader.h"
#include "Logging/LogMacros.h"
#include "Scatter/ScatterSystem.h"
#include "Text/JsonDocument.h"

#include <cfloat>
#include <cmath>
#include <utility>

namespace NorvesLib::Modules::Physics
{
    namespace
    {
        using namespace Core::Streaming;
        using Core::JsonValue;

        uint64_t StateKey(const StreamingPayloadContext& context)
        {
            return (uint64_t(context.Cell) << 32) | context.PayloadIndex;
        }

        // ネイティブ文字列を UTF-8 にし、名前やパスの途中の NUL を拒む。
        template<typename Char>
        bool ReadText(const Core::Container::TString<Char>& text, Core::Container::AnsiString& out)
        {
            if (text.empty())
            {
                return false;
            }
            for (const Char ch : text)
            {
                if (ch == 0)
                {
                    return false;
                }
            }
            if constexpr (sizeof(Char) == 1)
            {
                out.clear();
                out.append(text.data(), text.size());
            }
            else
            {
                const int count = WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, text.data(),
                    static_cast<int>(text.size()), nullptr, 0, nullptr, nullptr);
                if (count <= 0)
                {
                    return false;
                }
                out = Core::Container::AnsiString(static_cast<size_t>(count), '\0');
                if (WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, text.data(),
                    static_cast<int>(text.size()), out.data(), count, nullptr, nullptr) != count)
                {
                    return false;
                }
            }
            return true;
        }

        bool ReadText(JsonValue value, Core::Container::AnsiString& out)
        {
            return value.IsString() && ReadText(value.AsString(), out);
        }

        bool ReadFloat(JsonValue value, float& out)
        {
            const double number = value.AsNumber();
            if (!value.IsNumber() || !std::isfinite(number) || number < -FLT_MAX || number > FLT_MAX)
            {
                return false;
            }
            out = static_cast<float>(number);
            return true;
        }

        bool ReadPositive(JsonValue value, float& out)
        {
            return ReadFloat(value, out) && out > 0.0f;
        }

        bool ReadVector(JsonValue value, Math::Vector3& out)
        {
            return value.IsArray() && value.GetArraySize() == 3 &&
                ReadFloat(value.GetArrayElement(0), out.x) && ReadFloat(value.GetArrayElement(1), out.y) &&
                ReadFloat(value.GetArrayElement(2), out.z);
        }

        bool PositiveVector(const Math::Vector3& value)
        {
            return value.x > 0.0f && value.y > 0.0f && value.z > 0.0f;
        }

        bool ReadShape(JsonValue item, EStaticColliderShape& out, bool bAllowBox)
        {
            const JsonValue shape = item.FindMember("shape");
            if (!shape.IsString())
            {
                return false;
            }
            if (shape.AsString() == "sphere")
            {
                out = EStaticColliderShape::Sphere;
            }
            else if (shape.AsString() == "capsule")
            {
                out = EStaticColliderShape::Capsule;
            }
            else if (bAllowBox && shape.AsString() == "box")
            {
                out = EStaticColliderShape::Box;
            }
            else
            {
                return false;
            }
            return true;
        }

        bool ReadRotation(JsonValue value, Math::Quaternion& out)
        {
            if (!value.IsArray() || value.GetArraySize() != 4)
            {
                return false;
            }
            float components[4];
            double squared = 0.0;
            for (size_t i = 0; i < 4; ++i)
            {
                if (!ReadFloat(value.GetArrayElement(i), components[i]))
                {
                    return false;
                }
                squared += double(components[i]) * components[i];
            }
            if (squared <= 0.0 || !std::isfinite(squared))
            {
                return false;
            }
            const double length = std::sqrt(squared);
            out = Math::Quaternion(static_cast<float>(components[0] / length),
                static_cast<float>(components[1] / length), static_cast<float>(components[2] / length),
                static_cast<float>(components[3] / length));
            return true;
        }

        bool ReadCollider(JsonValue item, StaticColliderDesc& out)
        {
            if (!item.IsObject() || !ReadShape(item, out.Shape, true) ||
                !ReadVector(item.FindMember("position"), out.WorldTransform.position))
            {
                return false;
            }
            if (out.Shape == EStaticColliderShape::Box)
            {
                if (!ReadVector(item.FindMember("halfExtents"), out.HalfExtents) || !PositiveVector(out.HalfExtents))
                {
                    return false;
                }
            }
            else if (!ReadPositive(item.FindMember("radius"), out.Radius) ||
                (out.Shape == EStaticColliderShape::Capsule &&
                    !ReadPositive(item.FindMember("halfHeight"), out.CapsuleHalfHeight)))
            {
                return false;
            }
            const JsonValue rotation = item.FindMember("rotation");
            const JsonValue scale = item.FindMember("scale");
            const JsonValue surface = item.FindMember("surface");
            if ((rotation.IsValid() && !ReadRotation(rotation, out.WorldTransform.rotation)) ||
                (scale.IsValid() && (!ReadVector(scale, out.WorldTransform.scale) ||
                    !PositiveVector(out.WorldTransform.scale))))
            {
                return false;
            }
            if (surface.IsValid())
            {
                if (!surface.IsIntegerLiteral() || !std::isfinite(surface.AsNumber()) ||
                    surface.AsNumber() < 0.0 || surface.AsNumber() > UINT8_MAX)
                {
                    return false;
                }
                out.SurfaceId = static_cast<uint8_t>(surface.AsUInt32());
            }
            return true;
        }
    } // namespace

    StaticColliderPayloadHandler::StaticColliderPayloadHandler(IPhysicsModule& physics)
        : m_Physics(physics)
    {
    }

    void StaticColliderPayloadHandler::SetScatterSystem(Core::Scatter::ScatterSystem* pScatterSystem)
    {
        Thread::ScopedLock lock(m_Mutex);
        m_pScatterSystem = pScatterSystem;
    }

    void StaticColliderPayloadHandler::SetAssetRoot(Core::Container::AnsiString root)
    {
        Thread::ScopedLock lock(m_Mutex);
        m_AssetRoot = std::move(root);
    }

    StaticColliderChunkKey StaticColliderPayloadHandler::MakeChunkKey(StreamingCellId cell, uint32_t payloadIndex)
    {
        return {(kStreamingColliderKeyTag << 56) | (uint64_t(cell) << 24) | (payloadIndex & 0xFFFFFFu)};
    }

    const char* StaticColliderPayloadHandler::GetKind() const
    {
        return kStaticCollidersPayloadKind;
    }

    EStreamingPrepareResult StaticColliderPayloadHandler::Prepare(const StreamingPayloadContext& context,
        const StreamingCancelToken& cancel)
    {
        if (cancel.IsCancelled())
        {
            return EStreamingPrepareResult::Cancelled;
        }
        Core::Asset::AssetReadRequest request;
        const auto fail = [&](const char* reason)
        {
            LOG_WARNING("STREAMING_COLLIDERS_INVALID path=%s cell=%u %s", request.InputPath.c_str(), context.Cell, reason);
            return EStreamingPrepareResult::Failed;
        };
        if (context.Desc == nullptr || !ReadText(context.Desc->LogicalPath, request.InputPath))
        {
            return fail("論理パスがありません、または不正です");
        }
        {
            Thread::ScopedLock lock(m_Mutex);
            request.AssetRoot = m_AssetRoot;
        }
        request.bAllowAbsolutePath = false;
        request.MaxReadBytes = 4u * 1024u * 1024u;
        Core::Asset::AssetFileReader reader;
        const Core::Asset::AssetReadResult result = reader.Read(request);
        if (cancel.IsCancelled())
        {
            return EStreamingPrepareResult::Cancelled;
        }
        if (!result.Succeeded())
        {
            return fail("ファイルを 4 MiB 以内で読み込めません");
        }
        Core::Container::Span<const uint8_t> text(result.Blob.GetData(), result.Blob.GetSize());
        if (text.size() >= 3 && text[0] == 0xefu && text[1] == 0xbbu && text[2] == 0xbfu)
        {
            text = text.subspan(3);
        }
        Core::JsonDocument document;
        if (!Core::JsonDocument::TryParseUtf8(text, document))
        {
            return fail("JSON を解析できません");
        }
        const JsonValue root = document.GetRoot();
        const JsonValue format = root.FindMember("format");
        if (!root.IsObject() || !format.IsString() || format.AsString() != "norves.static-colliders.v1")
        {
            return fail("format が norves.static-colliders.v1 ではありません");
        }
        PayloadState state;
        const JsonValue colliders = root.FindMember("colliders");
        const JsonValue scatter = root.FindMember("scatter");
        if ((colliders.IsValid() && !colliders.IsArray()) || (scatter.IsValid() && !scatter.IsArray()))
        {
            return fail("colliders と scatter は配列で指定してください");
        }
        for (size_t i = 0; i < colliders.GetArraySize(); ++i)
        {
            StaticColliderDesc desc;
            if (!ReadCollider(colliders.GetArrayElement(i), desc))
            {
                return fail("コライダーの形状・位置・寸法・回転・拡大・表面が不正です");
            }
            state.Colliders.push_back(desc);
        }
        for (size_t i = 0; i < scatter.GetArraySize(); ++i)
        {
            const JsonValue item = scatter.GetArrayElement(i);
            ScatterEntry entry;
            const JsonValue centerHeight = item.FindMember("centerHeight");
            if (!item.IsObject() || !ReadText(item.FindMember("name"), entry.Name) ||
                !ReadText(item.FindMember("species"), entry.Species) || !ReadShape(item, entry.Shape, false) ||
                !ReadPositive(item.FindMember("radius"), entry.Radius) ||
                (entry.Shape == EStaticColliderShape::Capsule && !ReadPositive(item.FindMember("halfHeight"), entry.HalfHeight)) ||
                (centerHeight.IsValid() && !ReadFloat(centerHeight, entry.CenterHeight)))
            {
                return fail("散布の名前・種・形状・寸法・中心の高さが不正です");
            }
            state.Scatter.push_back(std::move(entry));
        }
        // 完成した状態だけを公開する。破棄は呼び手が Prepare の終了を待ってから行う。
        Thread::ScopedLock lock(m_Mutex);
        if (cancel.IsCancelled())
        {
            return EStreamingPrepareResult::Cancelled;
        }
        if (!m_States.emplace(StateKey(context), std::move(state)).second)
        {
            return fail("同じペイロードの準備済み状態が残っています");
        }
        return EStreamingPrepareResult::Success;
    }

    bool StaticColliderPayloadHandler::AppendScatter(const StreamingPayloadContext& context, const PayloadState& state,
        Core::Container::VariableArray<StaticColliderDesc>& outColliders) const
    {
        const auto fail = [&](const char* reason)
        {
            LOG_WARNING("STREAMING_COLLIDERS_SCATTER_FAILED cell=%u %s", context.Cell, reason);
            return false;
        };
        for (const ScatterEntry& entry : state.Scatter)
        {
            if (m_pScatterSystem == nullptr || context.CellDesc == nullptr)
            {
                return fail("散布システムまたはセルの境界がありません");
            }
            const auto handle = m_pScatterSystem->FindScatter(Core::Container::AnsiStringView(entry.Name));
            const auto data = m_pScatterSystem->GetData(handle);
            if (!Core::Container::IsValid(data))
            {
                return fail("指定された散布が見つかりません");
            }
            uint32_t speciesIndex = 0;
            for (; speciesIndex < data->Species.size(); ++speciesIndex)
            {
                if (data->Species[speciesIndex].Name == entry.Species)
                {
                    break;
                }
            }
            if (speciesIndex == data->Species.size())
            {
                return fail("指定された散布の種が見つかりません");
            }
            const auto& bounds = context.CellDesc->Bounds;
            for (uint32_t chunkIndex = 0; chunkIndex < data->Chunks.size(); ++chunkIndex)
            {
                const auto& chunk = data->Chunks[chunkIndex];
                if (chunk.AabbMax[0] < bounds.Min.x || chunk.AabbMin[0] >= bounds.Max.x ||
                    chunk.AabbMax[2] < bounds.Min.z || chunk.AabbMin[2] >= bounds.Max.z)
                {
                    continue;
                }
                const auto instances = data->GetInstances(chunkIndex, speciesIndex);
                for (size_t offset = 0; offset < instances.size(); offset += sizeof(Core::Asset::InstanceRecord16))
                {
                    Core::Asset::InstanceRecord16 record;
                    Core::Asset::ScatterInstanceValue value;
                    if (!Core::Asset::ReadScatterInstance(instances.subspan(offset, sizeof(record)), record) ||
                        !Core::Asset::DecodeScatterInstance(record, chunk, data->Species[speciesIndex],
                            data->ChunkSizeMeters, data->OriginX, data->OriginZ, value))
                    {
                        return fail("散布の個体を復号できません");
                    }
                    const float x = static_cast<float>(value.PositionX);
                    const float z = static_cast<float>(value.PositionZ);
                    if (!ContainsHorizontalPoint(bounds, x, z))
                    {
                        continue;
                    }
                    StaticColliderDesc desc;
                    desc.Shape = entry.Shape;
                    desc.Radius = entry.Radius * value.Scale;
                    desc.CapsuleHalfHeight = entry.HalfHeight * value.Scale;
                    desc.WorldTransform.position = Math::Vector3(x,
                        static_cast<float>(value.PositionY + double(entry.CenterHeight) * value.Scale), z);
                    if (!std::isfinite(desc.WorldTransform.position.y) || !std::isfinite(desc.Radius) || desc.Radius <= 0.0f ||
                        (entry.Shape == EStaticColliderShape::Capsule &&
                            (!std::isfinite(desc.CapsuleHalfHeight) || desc.CapsuleHalfHeight <= 0.0f)))
                    {
                        return fail("散布から作った中心または寸法が有限の正しい値ではありません");
                    }
                    outColliders.push_back(desc);
                }
            }
        }
        return true;
    }

    bool StaticColliderPayloadHandler::Register(const StreamingPayloadContext& context, PayloadState& state)
    {
        if (state.bRegistered)
        {
            return true;
        }
        const EPhysicsResult result = m_Physics.RegisterStaticColliderChunk(MakeChunkKey(context.Cell, context.PayloadIndex),
            {state.Colliders.data(), state.Colliders.size()});
        if (result != EPhysicsResult::Success)
        {
            LOG_WARNING("STREAMING_COLLIDERS_REGISTER_FAILED cell=%u result=%u 静的コライダー群を登録できません",
                context.Cell, static_cast<unsigned>(result));
            return false;
        }
        state.bRegistered = true;
        return true;
    }

    bool StaticColliderPayloadHandler::Unregister(const StreamingPayloadContext& context, PayloadState& state)
    {
        if (!state.bRegistered)
        {
            return true;
        }
        const EPhysicsResult result = m_Physics.UnregisterStaticColliderChunk(MakeChunkKey(context.Cell, context.PayloadIndex));
        if (result != EPhysicsResult::Success && result != EPhysicsResult::NotRegistered && result != EPhysicsResult::InvalidState)
        {
            LOG_WARNING("STREAMING_COLLIDERS_UNREGISTER_FAILED cell=%u result=%u 静的コライダー群を解除できません",
                context.Cell, static_cast<unsigned>(result));
            return false;
        }
        state.bRegistered = false;
        return true;
    }

    EStreamingStepResult StaticColliderPayloadHandler::CommitStep(const StreamingPayloadContext& context,
        StreamingStepCursor& cursor, uint64_t& outCommittedBytes)
    {
        static_cast<void>(cursor);
        outCommittedBytes = 0;
        Thread::ScopedLock lock(m_Mutex);
        const auto found = m_States.find(StateKey(context));
        if (found == m_States.end())
        {
            return EStreamingStepResult::Failed;
        }
        PayloadState& state = found->second;
        if (!state.bScatterResolved)
        {
            auto colliders = state.Colliders;
            if (!AppendScatter(context, state, colliders))
            {
                return EStreamingStepResult::Failed;
            }
            state.Colliders = std::move(colliders);
            state.bScatterResolved = true;
        }
        if (!Register(context, state))
        {
            return EStreamingStepResult::Failed;
        }
        outCommittedBytes = uint64_t(state.Colliders.size()) * sizeof(StaticColliderDesc);
        return EStreamingStepResult::Done;
    }

    void StaticColliderPayloadHandler::SetDormant(const StreamingPayloadContext& context, bool bDormant)
    {
        Thread::ScopedLock lock(m_Mutex);
        const auto found = m_States.find(StateKey(context));
        if (found != m_States.end())
        {
            if (bDormant)
            {
                Unregister(context, found->second);
            }
            else
            {
                Register(context, found->second);
            }
        }
    }

    EStreamingStepResult StaticColliderPayloadHandler::UnloadStep(const StreamingPayloadContext& context,
        StreamingStepCursor& cursor)
    {
        static_cast<void>(cursor);
        Thread::ScopedLock lock(m_Mutex);
        const auto found = m_States.find(StateKey(context));
        if (found != m_States.end())
        {
            if (!Unregister(context, found->second))
            {
                return EStreamingStepResult::Failed;
            }
            m_States.erase(found);
        }
        return EStreamingStepResult::Done;
    }

    void StaticColliderPayloadHandler::DiscardPrepared(const StreamingPayloadContext& context)
    {
        StreamingStepCursor cursor;
        UnloadStep(context, cursor);
    }

    StreamingPayloadBytes StaticColliderPayloadHandler::GetResidentBytes(const StreamingPayloadContext& context) const
    {
        Thread::ScopedLock lock(m_Mutex);
        const auto found = m_States.find(StateKey(context));
        return {found != m_States.end() ? uint64_t(found->second.Colliders.size()) * sizeof(StaticColliderDesc) : 0, 0};
    }
} // namespace NorvesLib::Modules::Physics
