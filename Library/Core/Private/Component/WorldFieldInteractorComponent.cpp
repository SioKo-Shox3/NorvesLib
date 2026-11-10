// ワールド空間の押し形を収集し、直前の位置とともに描画へ渡す。
#include "Component/WorldFieldInteractorComponent.h"

#include "Component/SkinnedMeshComponent.h"
#include "Engine/Engine.h"
#include "Object/World.h"
#include "Text/JsonDocument.h"
#include "Text/JsonWriter.h"
#include <algorithm>
#include <cmath>
#include <limits>

namespace NorvesLib::Core::Component
{
    IMPLEMENT_CLASS(WorldFieldInteractorComponent, Component)

    namespace
    {
        bool IsFinite(const Math::Vector3& value)
        {
            return std::isfinite(value.x) && std::isfinite(value.y) && std::isfinite(value.z);
        }

        bool IsValidShape(const WorldFieldInteractorShape& shape)
        {
            return (shape.Kind == WorldFieldShapeKind::Sphere || shape.Kind == WorldFieldShapeKind::Capsule) &&
                IsFinite(shape.P0) && IsFinite(shape.P1) && std::isfinite(shape.Radius) && shape.Radius >= 0.0f &&
                std::isfinite(shape.Strength) && shape.Strength >= 0.0f && shape.Strength <= 1.0f;
        }

        void WriteVector(JsonWriter& writer, const char* name, const Math::Vector3& value)
        {
            writer.BeginArray(name);
            writer.WriteNumberElement(value.x);
            writer.WriteNumberElement(value.y);
            writer.WriteNumberElement(value.z);
            writer.EndArray();
        }

        bool ReadFloat(JsonValue value, float& out)
        {
            if (!value.IsNumber())
            {
                return false;
            }
            const double number = value.AsNumber();
            if (!std::isfinite(number) || std::abs(number) > std::numeric_limits<float>::max())
            {
                return false;
            }
            out = static_cast<float>(number);
            return true;
        }

        bool ReadVector(JsonValue value, Math::Vector3& out)
        {
            return value.IsArray() && value.GetArraySize() == 3 &&
                ReadFloat(value.GetArrayElement(0), out.x) &&
                ReadFloat(value.GetArrayElement(1), out.y) &&
                ReadFloat(value.GetArrayElement(2), out.z);
        }

        bool ResolveSocket(const Entity& owner, Identity socket, Math::Vector3& position, void*)
        {
            const auto* mesh = owner.GetComponent<SkinnedMeshComponent>();
            Math::Transform transform;
            if (!mesh || mesh->IsPendingDestroy() || !mesh->GetSocketWorldTransform(socket, transform))
            {
                return false;
            }
            position = transform.position;
            return true;
        }

        void CopyPosition(float (&target)[3], const Math::Vector3& position)
        {
            target[0] = position.x;
            target[1] = position.y;
            target[2] = position.z;
        }
    }

    WorldFieldInteractorShapes::WorldFieldInteractorShapes(const WorldFieldInteractorShapes& other)
    {
        *this = other;
    }

    WorldFieldInteractorShapes& WorldFieldInteractorShapes::operator=(const WorldFieldInteractorShapes& other)
    {
        if (this != &other)
        {
            m_Entries.clear();
            m_Entries.reserve(other.m_Entries.size());
            for (const auto& entry : other.m_Entries)
            {
                m_Entries.push_back({m_NextId++, entry.Shape});
            }
        }
        return *this;
    }

    WorldFieldInteractorShapes& WorldFieldInteractorShapes::operator=(WorldFieldInteractorShapes&& other)
    {
        return *this = static_cast<const WorldFieldInteractorShapes&>(other);
    }

    const WorldFieldInteractorShape* WorldFieldInteractorShapes::Get(size_t index) const
    {
        return index < m_Entries.size() ? &m_Entries[index].Shape : nullptr;
    }

    bool WorldFieldInteractorShapes::Add(const WorldFieldInteractorShape& shape)
    {
        if (!IsValidShape(shape))
        {
            return false;
        }
        m_Entries.push_back({m_NextId++, shape});
        return true;
    }

    bool WorldFieldInteractorShapes::Set(size_t index, const WorldFieldInteractorShape& shape)
    {
        if (index >= m_Entries.size() || !IsValidShape(shape))
        {
            return false;
        }
        m_Entries[index].Shape = shape;
        return true;
    }

    bool WorldFieldInteractorShapes::Remove(size_t index)
    {
        if (index >= m_Entries.size())
        {
            return false;
        }
        m_Entries.erase(m_Entries.begin() + index);
        return true;
    }

    void WorldFieldInteractorShapes::Clear()
    {
        m_Entries.clear();
    }

    bool WorldFieldInteractorShapes::SerializeToString(Container::String& out) const
    {
        JsonWriter writer;
        writer.BeginArray();
        for (const auto& entry : m_Entries)
        {
            const auto& shape = entry.Shape;
            writer.BeginObject();
            writer.WriteInt64("kind", static_cast<int64_t>(shape.Kind));
            WriteVector(writer, "p0", shape.P0);
            WriteVector(writer, "p1", shape.P1);
            writer.WriteString("socket", shape.Socket.ToString());
            writer.WriteNumber("radius", shape.Radius);
            writer.WriteNumber("strength", shape.Strength);
            writer.WriteUInt64("channels", shape.ChannelMask);
            writer.EndObject();
        }
        writer.EndArray();
        if (!writer.IsComplete())
        {
            return false;
        }
        out = writer.ToString();
        return true;
    }

    bool WorldFieldInteractorShapes::DeserializeFromString(const Container::String& source)
    {
        JsonDocument document;
        if (!JsonDocument::TryParse(source, document) || !document.GetRoot().IsArray())
        {
            return false;
        }
        WorldFieldInteractorShapes candidate;
        const auto root = document.GetRoot();
        for (size_t i = 0; i < root.GetArraySize(); ++i)
        {
            const auto value = root.GetArrayElement(i);
            const auto kind = value.FindMember("kind");
            const auto socket = value.FindMember("socket");
            const auto channels = value.FindMember("channels");
            if (!value.IsObject() || !kind.IsIntegerLiteral() || kind.AsNumber() < 0 || kind.AsNumber() > 1 ||
                !socket.IsString() || !channels.IsIntegerLiteral() || channels.AsNumber() < 0 ||
                channels.AsNumber() > std::numeric_limits<uint32_t>::max())
            {
                return false;
            }
            WorldFieldInteractorShape shape;
            shape.Kind = static_cast<WorldFieldShapeKind>(kind.AsInt64());
            shape.Socket = Identity(socket.AsString());
            shape.ChannelMask = channels.AsUInt32();
            if (!ReadVector(value.FindMember("p0"), shape.P0) || !ReadVector(value.FindMember("p1"), shape.P1) ||
                !ReadFloat(value.FindMember("radius"), shape.Radius) ||
                !ReadFloat(value.FindMember("strength"), shape.Strength) || !candidate.Add(shape))
            {
                return false;
            }
        }
        *this = candidate;
        return true;
    }

    WorldFieldInteractorComponent::WorldFieldInteractorComponent() : Component()
    {
        SetTickEnabled(false);
    }

    WorldFieldInteractorComponent::WorldFieldInteractorComponent(const FieldInitializer* initializer)
        : Component(initializer)
    {
        SetTickEnabled(false);
    }

    WorldFieldInteractorComponent::WorldFieldInteractorComponent(const IUnknown* sourceObject)
        : Component(sourceObject)
    {
        SetTickEnabled(false);
    }

    WorldFieldInteractorComponent::~WorldFieldInteractorComponent()
    {
        Unregister();
    }

    void WorldFieldInteractorComponent::Finalize()
    {
        Unregister();
        Component::Finalize();
    }

    void WorldFieldInteractorComponent::OnEcsRowAttached()
    {
        const Entity* owner = GetOwner();
        if (!m_Gatherer && Engine::GEngine && owner && owner->GetWorld() == &Engine::GEngine->GetWorld())
        {
            Engine::GEngine->GetWorldFieldInteractorGatherer().Register(*this);
        }
    }

    void WorldFieldInteractorComponent::OnEcsRowDetaching()
    {
        Unregister();
    }

    void WorldFieldInteractorComponent::Unregister()
    {
        if (m_Gatherer)
        {
            m_Gatherer->Unregister(*this);
        }
    }

    WorldFieldInteractorGatherer::~WorldFieldInteractorGatherer()
    {
        for (auto& entry : m_Components)
        {
            entry.Component->m_Gatherer = nullptr;
        }
    }

    void WorldFieldInteractorGatherer::Register(WorldFieldInteractorComponent& component)
    {
        if (component.m_Gatherer == this)
        {
            return;
        }
        component.Unregister();
        m_Components.push_back({&component, {}});
        component.m_Gatherer = this;
    }

    void WorldFieldInteractorGatherer::Unregister(WorldFieldInteractorComponent& component)
    {
        for (auto it = m_Components.begin(); it != m_Components.end(); ++it)
        {
            if (it->Component == &component)
            {
                m_Components.erase(it);
                component.m_Gatherer = nullptr;
                return;
            }
        }
    }

    void WorldFieldInteractorGatherer::SetSocketResolver(SocketResolver resolver, void* user)
    {
        m_SocketResolver = resolver;
        m_SocketResolverUser = user;
    }

    void WorldFieldInteractorGatherer::Gather(const Math::Vector3& cameraPosition,
        Container::VariableArray<Rendering::WorldFieldInteractor>& out)
    {
        struct Candidate
        {
            Rendering::WorldFieldInteractor Interactor;
            double DistanceSquared = 0.0;
            size_t Order = 0;
        };
        Container::VariableArray<Candidate> candidates;
        Container::VariableArray<World*> updatedWorlds;
        out.clear();
        for (auto& registered : m_Components)
        {
            auto& component = *registered.Component;
            const Entity* owner = component.GetOwner();
            World* world = owner ? owner->GetWorld() : nullptr;
            if (world && std::find(updatedWorlds.begin(), updatedWorlds.end(), world) == updatedWorlds.end())
            {
                // 最後の Tick 群や停止中に動いた親の変換も、子の収集より先に反映する。
                world->UpdateWorldTransforms();
                updatedWorlds.push_back(world);
            }
            if (!owner || !world || !owner->IsActiveInHierarchy() ||
                !component.IsEnabled() || component.IsPendingDestroy())
            {
                registered.Previous.clear();
                continue;
            }
            const Math::Transform transform = owner->GetWorldTransform();
            const float radiusScale = std::max({std::abs(transform.scale.x), std::abs(transform.scale.y),
                                                std::abs(transform.scale.z)});
            Container::VariableArray<History> current;
            for (const auto& entry : component.Shapes.Get().m_Entries)
            {
                const auto& shape = entry.Shape;
                Math::Vector3 p0;
                Math::Vector3 p1;
                if (shape.Socket.IsValid())
                {
                    const auto resolver = m_SocketResolver ? m_SocketResolver : &ResolveSocket;
                    if (!resolver(*owner, shape.Socket, p0, m_SocketResolverUser))
                    {
                        p0 = transform.position;
                    }
                    p1 = shape.Kind == WorldFieldShapeKind::Capsule ? p0 + transform.TransformVector(shape.P1) : p0;
                }
                else
                {
                    p0 = transform.TransformPoint(shape.P0);
                    p1 = shape.Kind == WorldFieldShapeKind::Capsule ? transform.TransformPoint(shape.P1) : p0;
                }
                const float radius = shape.Radius * radiusScale;
                if (!IsFinite(p0) || !IsFinite(p1) || !std::isfinite(radius))
                {
                    continue;
                }
                Rendering::WorldFieldInteractor interactor;
                CopyPosition(interactor.P0, p0);
                CopyPosition(interactor.P1, p1);
                interactor.Radius = radius;
                interactor.Strength = shape.Strength;
                interactor.ChannelMask = shape.ChannelMask;
                const History* previous = nullptr;
                for (const auto& history : registered.Previous)
                {
                    if (history.ShapeId == entry.Id)
                    {
                        previous = &history;
                        break;
                    }
                }
                for (size_t axis = 0; axis < 3; ++axis)
                {
                    interactor.PrevP0[axis] = previous ? previous->Interactor.P0[axis] : interactor.P0[axis];
                    interactor.PrevP1[axis] = previous ? previous->Interactor.P1[axis] : interactor.P1[axis];
                }
                interactor.PrevRadius = previous ? previous->Interactor.Radius : radius;
                current.push_back({entry.Id, interactor});
                const double x = (static_cast<double>(p0.x) + p1.x) * 0.5 - cameraPosition.x;
                const double y = (static_cast<double>(p0.y) + p1.y) * 0.5 - cameraPosition.y;
                const double z = (static_cast<double>(p0.z) + p1.z) * 0.5 - cameraPosition.z;
                candidates.push_back({interactor, x * x + y * y + z * z, candidates.size()});
            }
            registered.Previous = std::move(current);
        }
        std::sort(candidates.begin(), candidates.end(), [](const Candidate& lhs, const Candidate& rhs)
        {
            if (lhs.DistanceSquared != rhs.DistanceSquared)
            {
                return lhs.DistanceSquared < rhs.DistanceSquared;
            }
            return lhs.Order < rhs.Order;
        });
        const size_t count = std::min(candidates.size(), static_cast<size_t>(Rendering::WorldFieldMaxInteractors));
        out.reserve(count);
        for (size_t i = 0; i < count; ++i)
        {
            out.push_back(candidates[i].Interactor);
        }
    }
} // namespace NorvesLib::Core::Component
