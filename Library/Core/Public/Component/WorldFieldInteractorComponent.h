// 草などのワールド空間フィールドを押す形と、その毎フレームの収集を定義する。
#pragma once

#include "Component/Component.h"
#include "Rendering/WorldFieldTypes.h"

namespace NorvesLib::Core::Component
{
    enum class WorldFieldShapeKind : uint8_t
    {
        Sphere,
        Capsule
    };

    struct WorldFieldInteractorShape
    {
        WorldFieldShapeKind Kind = WorldFieldShapeKind::Sphere;
        Math::Vector3 P0 = Math::Vector3::Zero;
        Math::Vector3 P1 = Math::Vector3::Zero;
        Identity Socket;
        float Radius = 0.5f;
        float Strength = 1.0f;
        uint32_t ChannelMask = Rendering::WorldFieldChannelAll;
    };

    // 反射の標準直列化は構造体配列を扱わないため、配列全体を文字列として保存する。
    class WorldFieldInteractorShapes
    {
    public:
        static constexpr const char* SchemaTypeName = "WorldFieldInteractorShapes";

        WorldFieldInteractorShapes() = default;
        WorldFieldInteractorShapes(const WorldFieldInteractorShapes& other);
        WorldFieldInteractorShapes(WorldFieldInteractorShapes&& other) noexcept = default;
        WorldFieldInteractorShapes& operator=(const WorldFieldInteractorShapes& other);
        WorldFieldInteractorShapes& operator=(WorldFieldInteractorShapes&& other);

        size_t Size() const { return m_Entries.size(); }
        const WorldFieldInteractorShape* Get(size_t index) const;
        bool Add(const WorldFieldInteractorShape& shape);
        bool Set(size_t index, const WorldFieldInteractorShape& shape);
        bool Remove(size_t index);
        void Clear();
        bool SerializeToString(Container::String& out) const;
        bool DeserializeFromString(const Container::String& source);

    private:
        friend class WorldFieldInteractorGatherer;
        struct Entry
        {
            uint64_t Id = 0;
            WorldFieldInteractorShape Shape;
        };
        Container::VariableArray<Entry> m_Entries;
        uint64_t m_NextId = 1;
    };

    class WorldFieldInteractorGatherer;

    class WorldFieldInteractorComponent : public Component
    {
        REFLECTION_CLASS(WorldFieldInteractorComponent, Component)

    public:
        WorldFieldInteractorComponent();
        explicit WorldFieldInteractorComponent(const FieldInitializer* initializer);
        explicit WorldFieldInteractorComponent(const IUnknown* sourceObject);
        ~WorldFieldInteractorComponent() override;

        void Finalize() override;
        const WorldFieldInteractorShapes& GetShapes() const { return Shapes.Get(); }
        bool AddShape(const WorldFieldInteractorShape& shape) { return Shapes.Get().Add(shape); }
        bool SetShape(size_t index, const WorldFieldInteractorShape& shape) { return Shapes.Get().Set(index, shape); }
        bool RemoveShape(size_t index) { return Shapes.Get().Remove(index); }
        void ClearShapes() { Shapes.Get().Clear(); }

        PROPERTY(WorldFieldInteractorShapes, Shapes)

    protected:
        void OnEcsRowAttached() override;
        void OnEcsRowDetaching() override;

    private:
        friend class WorldFieldInteractorGatherer;
        void Unregister();
        WorldFieldInteractorGatherer* m_Gatherer = nullptr;
    };

    // World から登録された部品だけを借り、形ごとの直前の位置を保持する。
    class WorldFieldInteractorGatherer
    {
    public:
        using SocketResolver = bool (*)(const Entity&, Identity, Math::Vector3&, void*);

        WorldFieldInteractorGatherer() = default;
        ~WorldFieldInteractorGatherer();
        WorldFieldInteractorGatherer(const WorldFieldInteractorGatherer&) = delete;
        WorldFieldInteractorGatherer& operator=(const WorldFieldInteractorGatherer&) = delete;

        void Register(WorldFieldInteractorComponent& component);
        void Unregister(WorldFieldInteractorComponent& component);
        void SetSocketResolver(SocketResolver resolver, void* user = nullptr);
        void Gather(const Math::Vector3& cameraPosition, Container::VariableArray<Rendering::WorldFieldInteractor>& out);

    private:
        struct History
        {
            uint64_t ShapeId = 0;
            Rendering::WorldFieldInteractor Interactor;
        };
        struct RegisteredComponent
        {
            WorldFieldInteractorComponent* Component = nullptr;
            Container::VariableArray<History> Previous;
        };
        Container::VariableArray<RegisteredComponent> m_Components;
        SocketResolver m_SocketResolver = nullptr;
        void* m_SocketResolverUser = nullptr;
    };
} // namespace NorvesLib::Core::Component
