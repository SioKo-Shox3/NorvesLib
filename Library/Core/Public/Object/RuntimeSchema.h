#pragma once

#include "Container/Containers.h"
#include "Text/IdentityPool.h"
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <limits>
#include <new>
#include <sstream>
#include <type_traits>
#include <typeindex>
#include <utility>

namespace NorvesLib::Core
{
    class IUnknown;
    class ReferenceCollector;

    using ClassId = uint64_t;
    using PropertyId = uint64_t;
    using FunctionId = uint64_t;
    using TypeId = uint64_t;

    using StableClassId = uint64_t;
    using StablePropertyId = uint64_t;
    using StableFunctionId = uint64_t;
    using StableTypeId = uint64_t;

    inline constexpr uint64_t InvalidSchemaId = 0;

    enum class TypeKind : uint8_t
    {
        Void,
        Bool,
        Integer,
        Float,
        String,
        Object,
        Resource,
        Array,
        Struct,
        Enum,
        Custom
    };

    enum class StorageKind : uint8_t
    {
        Member,
        Bag,
        Computed,
        ObjectRef,
        ResourceRef,
        Transient
    };

    enum class PropertyFlags : uint32_t
    {
        None = 0,
        Editable = 1 << 0,
        ReadOnly = 1 << 1,
        Serializable = 1 << 2,
        Transient = 1 << 3,
        RuntimeOnly = 1 << 4,
        EditorOnly = 1 << 5,
        GCReference = 1 << 6,
        ResourceReference = 1 << 7,
        Undoable = 1 << 8,
        Inspectable = 1 << 9
    };

    enum class FunctionFlags : uint32_t
    {
        None = 0,
        RuntimeCallable = 1 << 0,
        EditorCallable = 1 << 1,
        ConsoleCallable = 1 << 2,
        ReadOnly = 1 << 3,
        Mutating = 1 << 4,
        Undoable = 1 << 5,
        Async = 1 << 6,
        GameThreadOnly = 1 << 7,
        RenderThreadOnly = 1 << 8,
        RequiresAuthority = 1 << 9,
        Unsafe = 1 << 10
    };

    enum class ThreadPolicy : uint8_t
    {
        AnyThread,
        GameThreadOnly,
        RenderThreadOnly,
        WorkerThread
    };

    inline constexpr PropertyFlags operator|(PropertyFlags lhs, PropertyFlags rhs)
    {
        return static_cast<PropertyFlags>(
            static_cast<uint32_t>(lhs) | static_cast<uint32_t>(rhs));
    }

    inline constexpr PropertyFlags operator&(PropertyFlags lhs, PropertyFlags rhs)
    {
        return static_cast<PropertyFlags>(
            static_cast<uint32_t>(lhs) & static_cast<uint32_t>(rhs));
    }

    inline constexpr FunctionFlags operator|(FunctionFlags lhs, FunctionFlags rhs)
    {
        return static_cast<FunctionFlags>(
            static_cast<uint32_t>(lhs) | static_cast<uint32_t>(rhs));
    }

    inline constexpr FunctionFlags operator&(FunctionFlags lhs, FunctionFlags rhs)
    {
        return static_cast<FunctionFlags>(
            static_cast<uint32_t>(lhs) & static_cast<uint32_t>(rhs));
    }

    inline uint64_t HashStableBytes(const void *data, size_t size, uint64_t seed = 14695981039346656037ull)
    {
        const auto *bytes = static_cast<const uint8_t *>(data);
        uint64_t hash = seed;
        for (size_t i = 0; i < size; ++i)
        {
            hash ^= bytes[i];
            hash *= 1099511628211ull;
        }
        return hash == InvalidSchemaId ? 1 : hash;
    }

    inline uint64_t HashStableString(Container::StringView value, uint64_t seed = 14695981039346656037ull)
    {
        uint64_t hash = seed;
        for (const TCHAR ch : value)
        {
            using UnsignedChar = std::make_unsigned_t<TCHAR>;
            const auto code = static_cast<UnsignedChar>(ch);
            hash = HashStableBytes(&code, sizeof(code), hash);
        }
        return hash == InvalidSchemaId ? 1 : hash;
    }

    inline uint64_t HashStableCString(const char *value, uint64_t seed = 14695981039346656037ull)
    {
        if (!value)
        {
            return seed;
        }
        uint64_t hash = seed;
        while (*value)
        {
            hash = HashStableBytes(value, 1, hash);
            ++value;
        }
        return hash == InvalidSchemaId ? 1 : hash;
    }

    inline uint64_t MakeStableSchemaId(const char *moduleName, const char *schemaKind,
                                       Container::StringView ownerName, Container::StringView memberName = {})
    {
        uint64_t hash = HashStableCString(moduleName);
        hash = HashStableCString(":", hash);
        hash = HashStableCString(schemaKind, hash);
        hash = HashStableCString(":", hash);
        hash = HashStableString(ownerName, hash);
        if (!memberName.empty())
        {
            hash = HashStableCString(".", hash);
            hash = HashStableString(memberName, hash);
        }
        return hash == InvalidSchemaId ? 1 : hash;
    }

    struct TypeOps
    {
        using CopyFn = void (*)(void *dst, const void *src);
        using MoveFn = void (*)(void *dst, void *src);
        using DestroyFn = void (*)(void *value);
        using SerializeFn = bool (*)(const void *value, Container::String &outValue);
        using DeserializeFn = bool (*)(const Container::String &inValue, void *outValue);
        using AddReferencesFn = void (*)(const void *value, ReferenceCollector &collector);

        CopyFn Copy = nullptr;
        MoveFn Move = nullptr;
        DestroyFn Destroy = nullptr;
        SerializeFn Serialize = nullptr;
        DeserializeFn Deserialize = nullptr;
        AddReferencesFn AddReferences = nullptr;
    };

    struct TypeInfo
    {
        TypeId Id = InvalidSchemaId;
        StableTypeId StableId = InvalidSchemaId;
        Container::String Name;
        TypeKind Kind = TypeKind::Custom;
        size_t Size = 0;
        size_t Alignment = alignof(std::max_align_t);
        TypeOps Ops;
    };

    namespace Detail
    {
        template <typename T>
        struct TypeKindOf
        {
            static constexpr TypeKind Value =
                std::is_same_v<T, void> ? TypeKind::Void :
                std::is_same_v<T, bool> ? TypeKind::Bool :
                std::is_integral_v<T> ? TypeKind::Integer :
                std::is_floating_point_v<T> ? TypeKind::Float :
                std::is_same_v<T, Container::String> ? TypeKind::String :
                std::is_enum_v<T> ? TypeKind::Enum :
                std::is_class_v<T> ? TypeKind::Struct :
                TypeKind::Custom;
        };

        template <typename T>
        inline Container::String DefaultTypeName()
        {
            if constexpr (std::is_same_v<T, void>)
            {
                return "void";
            }
            else if constexpr (std::is_same_v<T, bool>)
            {
                return "bool";
            }
            else if constexpr (std::is_same_v<T, int8_t>)
            {
                return "int8";
            }
            else if constexpr (std::is_same_v<T, int16_t>)
            {
                return "int16";
            }
            else if constexpr (std::is_same_v<T, int32_t> || std::is_same_v<T, int>)
            {
                return "int32";
            }
            else if constexpr (std::is_same_v<T, int64_t>)
            {
                return "int64";
            }
            else if constexpr (std::is_same_v<T, uint8_t>)
            {
                return "uint8";
            }
            else if constexpr (std::is_same_v<T, uint16_t>)
            {
                return "uint16";
            }
            else if constexpr (std::is_same_v<T, uint32_t>)
            {
                return "uint32";
            }
            else if constexpr (std::is_same_v<T, uint64_t>)
            {
                return "uint64";
            }
            else if constexpr (std::is_same_v<T, float>)
            {
                return "float";
            }
            else if constexpr (std::is_same_v<T, double>)
            {
                return "double";
            }
            else if constexpr (std::is_same_v<T, Container::String>)
            {
                return "String";
            }
            else
            {
                return Container::String(typeid(T).name());
            }
        }

        template <typename T>
        inline bool SerializeValue(const void *value, Container::String &outValue)
        {
            if (!value)
            {
                return false;
            }

            if constexpr (std::is_same_v<T, Container::String>)
            {
                outValue = *static_cast<const T *>(value);
                return true;
            }
            else if constexpr (std::is_arithmetic_v<T>)
            {
                std::basic_ostringstream<TCHAR> stream;
                stream << *static_cast<const T *>(value);
                outValue = Container::String(stream.str());
                return !stream.fail();
            }
            else
            {
                return false;
            }
        }

        template <typename T>
        inline bool DeserializeValue(const Container::String &inValue, void *outValue)
        {
            if (!outValue)
            {
                return false;
            }

            if constexpr (std::is_same_v<T, Container::String>)
            {
                *static_cast<T *>(outValue) = inValue;
                return true;
            }
            else if constexpr (std::is_arithmetic_v<T>)
            {
                std::basic_string<TCHAR> text(inValue.data(), inValue.size());
                std::basic_istringstream<TCHAR> stream(text);
                stream >> *static_cast<T *>(outValue);
                return !stream.fail();
            }
            else
            {
                return false;
            }
        }
    }

    class TypeRegistry
    {
    public:
        static TypeRegistry &Get()
        {
            static TypeRegistry s_Registry;
            return s_Registry;
        }

        template <typename T>
        TypeId Register(Container::String name = Detail::DefaultTypeName<T>(), TypeKind kind = Detail::TypeKindOf<T>::Value)
        {
            using ValueType = std::remove_cv_t<T>;
            const std::type_index typeKey(typeid(ValueType));
            auto typeIt = m_TypeIdsByCppType.find(typeKey);
            if (typeIt != m_TypeIdsByCppType.end())
            {
                return typeIt->second;
            }

            TypeInfo info;
            info.Id = AllocateTypeId();
            info.StableId = MakeStableSchemaId("NorvesLib", "Type", Container::StringView(name));
            info.Name = std::move(name);
            info.Kind = kind;
            info.Size = sizeof(ValueType);
            info.Alignment = alignof(ValueType);
            info.Ops.Copy = [](void *dst, const void *src)
            {
                new (dst) ValueType(*static_cast<const ValueType *>(src));
            };
            info.Ops.Move = [](void *dst, void *src)
            {
                new (dst) ValueType(std::move(*static_cast<ValueType *>(src)));
            };
            info.Ops.Destroy = [](void *value)
            {
                static_cast<ValueType *>(value)->~ValueType();
            };
            info.Ops.Serialize = &Detail::SerializeValue<ValueType>;
            info.Ops.Deserialize = &Detail::DeserializeValue<ValueType>;

            const TypeId id = info.Id;
            m_TypeIdsByCppType.emplace(typeKey, id);
            m_TypeIdsByStableId.emplace(info.StableId, id);
            m_Types.emplace(id, std::move(info));
            return id;
        }

        template <typename T>
        TypeId GetTypeId()
        {
            return Register<std::remove_cv_t<T>>();
        }

        const TypeInfo *Find(TypeId id) const
        {
            auto it = m_Types.find(id);
            return it != m_Types.end() ? &it->second : nullptr;
        }

        const TypeInfo *FindStable(StableTypeId stableId) const
        {
            auto idIt = m_TypeIdsByStableId.find(stableId);
            return idIt != m_TypeIdsByStableId.end() ? Find(idIt->second) : nullptr;
        }

        Container::VariableArray<TypeInfo> GetAllTypes() const
        {
            Container::VariableArray<TypeInfo> result;
            result.reserve(m_Types.size());
            for (const auto &pair : m_Types)
            {
                result.push_back(pair.second);
            }
            return result;
        }

    private:
        TypeRegistry() = default;

        TypeId AllocateTypeId()
        {
            return m_NextTypeId++;
        }

        TypeId m_NextTypeId = 1;
        Container::UnorderedMap<TypeId, TypeInfo> m_Types;
        Container::UnorderedMap<StableTypeId, TypeId> m_TypeIdsByStableId;
        Container::UnorderedMap<std::type_index, TypeId> m_TypeIdsByCppType;
    };

    class PropertyValue
    {
    public:
        PropertyValue() = default;

        PropertyValue(const PropertyValue &other)
        {
            CopyFrom(other);
        }

        PropertyValue(PropertyValue &&other) noexcept
            : m_Type(other.m_Type), m_Data(other.m_Data), m_Size(other.m_Size), m_Alignment(other.m_Alignment)
        {
            other.m_Type = InvalidSchemaId;
            other.m_Data = nullptr;
            other.m_Size = 0;
            other.m_Alignment = alignof(std::max_align_t);
        }

        ~PropertyValue()
        {
            Reset();
        }

        PropertyValue &operator=(const PropertyValue &other)
        {
            if (this != &other)
            {
                Reset();
                CopyFrom(other);
            }
            return *this;
        }

        PropertyValue &operator=(PropertyValue &&other) noexcept
        {
            if (this != &other)
            {
                Reset();
                m_Type = other.m_Type;
                m_Data = other.m_Data;
                m_Size = other.m_Size;
                m_Alignment = other.m_Alignment;

                other.m_Type = InvalidSchemaId;
                other.m_Data = nullptr;
                other.m_Size = 0;
                other.m_Alignment = alignof(std::max_align_t);
            }
            return *this;
        }

        template <typename T>
        static PropertyValue Create(const T &value)
        {
            PropertyValue result;
            result.Set(value);
            return result;
        }

        template <typename T>
        void Set(const T &value)
        {
            Reset();

            using ValueType = std::remove_cv_t<T>;
            m_Type = TypeRegistry::Get().GetTypeId<ValueType>();
            const TypeInfo *typeInfo = TypeRegistry::Get().Find(m_Type);
            if (!typeInfo)
            {
                m_Type = InvalidSchemaId;
                return;
            }

            m_Size = typeInfo->Size;
            m_Alignment = typeInfo->Alignment;
            m_Data = ::operator new(m_Size, std::align_val_t(m_Alignment));
            typeInfo->Ops.Copy(m_Data, &value);
        }

        template <typename T>
        T *Get()
        {
            using ValueType = std::remove_cv_t<T>;
            return m_Type == TypeRegistry::Get().GetTypeId<ValueType>() ? static_cast<T *>(m_Data) : nullptr;
        }

        template <typename T>
        const T *Get() const
        {
            using ValueType = std::remove_cv_t<T>;
            return m_Type == TypeRegistry::Get().GetTypeId<ValueType>() ? static_cast<const T *>(m_Data) : nullptr;
        }

        TypeId GetType() const { return m_Type; }
        const void *GetData() const { return m_Data; }
        void *GetData() { return m_Data; }
        size_t GetSize() const { return m_Size; }
        bool IsValid() const { return m_Type != InvalidSchemaId && m_Data != nullptr; }

        bool Serialize(Container::String &outValue) const
        {
            const TypeInfo *typeInfo = TypeRegistry::Get().Find(m_Type);
            return typeInfo && typeInfo->Ops.Serialize && typeInfo->Ops.Serialize(m_Data, outValue);
        }

        bool Equals(const PropertyValue &other) const
        {
            if (m_Type != other.m_Type || m_Size != other.m_Size)
            {
                return false;
            }
            if (!m_Data || !other.m_Data)
            {
                return m_Data == other.m_Data;
            }

            Container::String lhs;
            Container::String rhs;
            if (Serialize(lhs) && other.Serialize(rhs))
            {
                return lhs == rhs;
            }

            return std::memcmp(m_Data, other.m_Data, m_Size) == 0;
        }

        void Reset()
        {
            if (!m_Data)
            {
                m_Type = InvalidSchemaId;
                m_Size = 0;
                m_Alignment = alignof(std::max_align_t);
                return;
            }

            const TypeInfo *typeInfo = TypeRegistry::Get().Find(m_Type);
            if (typeInfo && typeInfo->Ops.Destroy)
            {
                typeInfo->Ops.Destroy(m_Data);
            }

            ::operator delete(m_Data, std::align_val_t(m_Alignment));
            m_Type = InvalidSchemaId;
            m_Data = nullptr;
            m_Size = 0;
            m_Alignment = alignof(std::max_align_t);
        }

    private:
        void CopyFrom(const PropertyValue &other)
        {
            if (!other.m_Data || other.m_Type == InvalidSchemaId)
            {
                return;
            }

            const TypeInfo *typeInfo = TypeRegistry::Get().Find(other.m_Type);
            if (!typeInfo || !typeInfo->Ops.Copy)
            {
                return;
            }

            m_Type = other.m_Type;
            m_Size = other.m_Size;
            m_Alignment = other.m_Alignment;
            m_Data = ::operator new(m_Size, std::align_val_t(m_Alignment));
            typeInfo->Ops.Copy(m_Data, other.m_Data);
        }

        TypeId m_Type = InvalidSchemaId;
        void *m_Data = nullptr;
        size_t m_Size = 0;
        size_t m_Alignment = alignof(std::max_align_t);
    };

    struct ParamDesc
    {
        Container::String Name;
        TypeId Type = InvalidSchemaId;
    };

    struct PropertyDesc
    {
        using GetterFn = bool (*)(const IUnknown *instance, PropertyValue &outValue);
        using SetterFn = bool (*)(IUnknown *instance, const PropertyValue &value);

        PropertyId Id = InvalidSchemaId;
        StablePropertyId StableId = InvalidSchemaId;
        Container::String Name;
        TypeId Type = InvalidSchemaId;
        PropertyFlags Flags = PropertyFlags::None;
        StorageKind Storage = StorageKind::Member;
        GetterFn Get = nullptr;
        SetterFn Set = nullptr;
    };

    struct FunctionDesc
    {
        using InvokeFn = bool (*)(IUnknown *instance, const Container::VariableArray<PropertyValue> &arguments, PropertyValue *outReturnValue);

        FunctionId Id = InvalidSchemaId;
        StableFunctionId StableId = InvalidSchemaId;
        Container::String Name;
        Container::VariableArray<ParamDesc> Params;
        TypeId ReturnType = InvalidSchemaId;
        FunctionFlags Flags = FunctionFlags::None;
        ThreadPolicy Thread = ThreadPolicy::GameThreadOnly;
        InvokeFn Invoke = nullptr;
    };

    class ClassDefaults
    {
    public:
        void SetDefault(StablePropertyId propertyId, PropertyValue value)
        {
            m_Values[propertyId] = std::move(value);
        }

        bool HasDefault(StablePropertyId propertyId) const
        {
            return m_Values.find(propertyId) != m_Values.end();
        }

        const PropertyValue *FindDefault(StablePropertyId propertyId) const
        {
            auto it = m_Values.find(propertyId);
            return it != m_Values.end() ? &it->second : nullptr;
        }

        Container::VariableArray<StablePropertyId> Diff(const ClassDefaults &other) const
        {
            Container::VariableArray<StablePropertyId> result;

            for (const auto &pair : m_Values)
            {
                const PropertyValue *otherValue = other.FindDefault(pair.first);
                if (!otherValue || !pair.second.Equals(*otherValue))
                {
                    result.push_back(pair.first);
                }
            }

            for (const auto &pair : other.m_Values)
            {
                if (!HasDefault(pair.first))
                {
                    result.push_back(pair.first);
                }
            }

            return result;
        }

        void Clear()
        {
            m_Values.clear();
        }

    private:
        Container::UnorderedMap<StablePropertyId, PropertyValue> m_Values;
    };

    struct ClassInfo
    {
        ClassId Id = InvalidSchemaId;
        StableClassId StableId = InvalidSchemaId;
        Container::String Name;
        ClassId ParentId = InvalidSchemaId;
        Container::VariableArray<PropertyDesc> Properties;
        Container::VariableArray<FunctionDesc> Functions;
        IUnknown *(*Factory)(IUnknown *outer) = nullptr;
        ClassDefaults Defaults;
        uint32_t SchemaVersion = 1;
    };

} // namespace NorvesLib::Core
