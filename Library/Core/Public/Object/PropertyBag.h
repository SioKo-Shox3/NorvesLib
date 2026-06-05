#pragma once

#include "Object/ReferenceCollector.h"
#include "Object/RuntimeSchema.h"
#include "Container/Containers.h"

namespace NorvesLib::Core
{
    class PropertyBag
    {
    public:
        PropertyBag() = default;
        PropertyBag(const PropertyBag &) = default;
        PropertyBag(PropertyBag &&) noexcept = default;
        PropertyBag &operator=(const PropertyBag &) = default;
        PropertyBag &operator=(PropertyBag &&) noexcept = default;

        template <typename T>
        void Set(StablePropertyId propertyId, const T &value)
        {
            SetValue(propertyId, PropertyValue::Create<T>(value));
        }

        void SetValue(StablePropertyId propertyId, PropertyValue value)
        {
            if (propertyId != InvalidSchemaId)
            {
                m_Values[propertyId] = std::move(value);
            }
        }

        bool Remove(StablePropertyId propertyId)
        {
            return m_Values.erase(propertyId) > 0;
        }

        bool Has(StablePropertyId propertyId) const
        {
            return m_Values.find(propertyId) != m_Values.end();
        }

        PropertyValue *Find(StablePropertyId propertyId)
        {
            auto it = m_Values.find(propertyId);
            return it != m_Values.end() ? &it->second : nullptr;
        }

        const PropertyValue *Find(StablePropertyId propertyId) const
        {
            auto it = m_Values.find(propertyId);
            return it != m_Values.end() ? &it->second : nullptr;
        }

        template <typename T>
        T *Get(StablePropertyId propertyId)
        {
            PropertyValue *value = Find(propertyId);
            return value ? value->Get<T>() : nullptr;
        }

        template <typename T>
        const T *Get(StablePropertyId propertyId) const
        {
            const PropertyValue *value = Find(propertyId);
            return value ? value->Get<T>() : nullptr;
        }

        bool SerializeValue(StablePropertyId propertyId, Container::String &outValue) const
        {
            const PropertyValue *value = Find(propertyId);
            return value && value->Serialize(outValue);
        }

        void AddReferencedObjects(ReferenceCollector &collector) const
        {
            for (const auto &pair : m_Values)
            {
                pair.second.AddReferences(collector);
            }
        }

        size_t GetValueCount() const
        {
            return m_Values.size();
        }

        void Clear()
        {
            m_Values.clear();
        }

    private:
        Container::UnorderedMap<StablePropertyId, PropertyValue> m_Values;
    };

} // namespace NorvesLib::Core
