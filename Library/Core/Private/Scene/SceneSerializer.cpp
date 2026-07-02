#include "Scene/SceneSerializer.h"

#include "Component/Component.h"
#include "Object/Entity.h"
#include "Object/IClass.h"
#include "Object/PrefabAsset.h"
#include "Object/ResourceRegistry.h"
#include "Object/World.h"
#include "FileStream/FileStream.h"
#include "Text/JsonDocument.h"
#include "Text/JsonWriter.h"
#include "Logging/LogMacros.h"
#include <cerrno>
#include <cstdio>
#include <cstdlib>

namespace NorvesLib::Core::Scene
{
    namespace
    {
        constexpr const char* kModuleName = "NorvesLib";

        StableClassId MakeSceneStableClassId(Container::StringView className)
        {
            return MakeStableSchemaId(kModuleName, "Class", className);
        }

        StablePropertyId MakeSceneStablePropertyId(Container::StringView className, Container::StringView propertyName)
        {
            return MakeStableSchemaId(kModuleName, "Property", className, propertyName);
        }

        Container::String ToDecimalString(uint64_t value)
        {
            char buffer[24] = {};
            std::snprintf(buffer, sizeof(buffer), "%llu", static_cast<unsigned long long>(value));
            return Container::String(buffer);
        }

        bool TryParseUInt64(const Container::String& text, uint64_t& outValue)
        {
            if (text.empty())
            {
                return false;
            }

            // 先頭は10進数字のみ許可（strtoullが受理する先頭空白/符号を弾く）
            const char firstChar = text.c_str()[0];
            if (firstChar < '0' || firstChar > '9')
            {
                return false;
            }

            errno = 0;
            char* end = nullptr;
            const unsigned long long parsed = std::strtoull(text.c_str(), &end, 10);
            if (end == nullptr || *end != '\0' || end == text.c_str() || errno != 0)
            {
                return false;
            }

            outValue = static_cast<uint64_t>(parsed);
            return true;
        }

        // ========================================
        // 保存側: StableId → 名前 の逆引き
        // ========================================

        struct SchemaNameResolver
        {
            Container::UnorderedMap<StableClassId, const IClass*> ClassesByStableId;
            Container::UnorderedMap<StableTypeId, Container::String> TypeNamesByStableId;
            Container::UnorderedMap<StableClassId, Container::UnorderedMap<StablePropertyId, Container::String>> PropertyNamesByClass;

            SchemaNameResolver()
            {
                Container::VariableArray<const IClass*> classes = ClassRegistry::Get().GetAllClasses();
                for (const IClass* cls : classes)
                {
                    if (!cls)
                    {
                        continue;
                    }

                    const StableClassId classId = MakeSceneStableClassId(cls->GetClassName().GetView());
                    ClassesByStableId[classId] = cls;

                    Container::UnorderedMap<StablePropertyId, Container::String>& propertyNames = PropertyNamesByClass[classId];
                    Container::VariableArray<const ClassProperty*> properties = cls->GetAllProperties();
                    for (const ClassProperty* property : properties)
                    {
                        if (!property)
                        {
                            continue;
                        }
                        const StablePropertyId propertyId = MakeSceneStablePropertyId(
                            cls->GetClassName().GetView(),
                            property->GetName().GetView());
                        propertyNames[propertyId] = property->GetName().ToString();
                    }
                }

                Container::VariableArray<TypeInfo> types = TypeRegistry::Get().GetAllTypes();
                for (const TypeInfo& info : types)
                {
                    TypeNamesByStableId[info.StableId] = info.Name;
                }
            }

            Container::String FindClassName(StableClassId classId) const
            {
                auto it = ClassesByStableId.find(classId);
                if (it == ClassesByStableId.end() || !it->second)
                {
                    return Container::String();
                }
                return it->second->GetClassName().ToString();
            }

            Container::String FindPropertyName(StableClassId classId, StablePropertyId propertyId) const
            {
                auto classIt = PropertyNamesByClass.find(classId);
                if (classIt == PropertyNamesByClass.end())
                {
                    return Container::String();
                }
                auto propertyIt = classIt->second.find(propertyId);
                return propertyIt != classIt->second.end() ? propertyIt->second : Container::String();
            }

            Container::String FindTypeName(StableTypeId typeId) const
            {
                auto it = TypeNamesByStableId.find(typeId);
                return it != TypeNamesByStableId.end() ? it->second : Container::String();
            }
        };

        SceneObjectRecord BuildObjectRecord(const ObjectSnapshot& snapshot, const SchemaNameResolver& resolver)
        {
            SceneObjectRecord record;
            record.ClassId = snapshot.Class;
            record.ClassName = resolver.FindClassName(snapshot.Class);
            record.Path = snapshot.Path;
            record.Properties.reserve(snapshot.Properties.size());
            for (const ProjectedPropertyValue& value : snapshot.Properties)
            {
                ScenePropertyRecord propertyRecord;
                propertyRecord.PropertyId = value.Property;
                propertyRecord.Name = resolver.FindPropertyName(snapshot.Class, value.Property);
                propertyRecord.TypeId = value.Type;
                propertyRecord.TypeName = resolver.FindTypeName(value.Type);
                propertyRecord.Value = value.SerializedValue;
                record.Properties.push_back(std::move(propertyRecord));
            }
            return record;
        }

        SceneEntityRecord BuildEntityRecord(const EntitySubtreeSnapshotNode& node, const SchemaNameResolver& resolver)
        {
            SceneEntityRecord record;
            record.Alias = node.Alias;
            record.ParentAlias = node.ParentAlias;
            record.Object = BuildObjectRecord(node.Object, resolver);

            record.Components.reserve(node.Components.size());
            for (const ComponentSubtreeSnapshot& component : node.Components)
            {
                SceneComponentRecord componentRecord;
                componentRecord.Alias = component.Alias;
                componentRecord.OwnerAlias = component.OwnerAlias;
                componentRecord.Object = BuildObjectRecord(component.Object, resolver);
                record.Components.push_back(std::move(componentRecord));
            }

            record.Children.reserve(node.Children.size());
            for (const EntitySubtreeSnapshotNode& child : node.Children)
            {
                record.Children.push_back(BuildEntityRecord(child, resolver));
            }
            return record;
        }

        // ========================================
        // JSON 書き出し
        // ========================================

        void WriteObjectRecord(JsonWriter& writer, const char* key, const SceneObjectRecord& record)
        {
            writer.BeginObject(key);
            writer.WriteString("class", record.ClassName);
            writer.WriteString("classId", ToDecimalString(record.ClassId));
            writer.WriteString("path", record.Path);
            writer.BeginArray("properties");
            for (const ScenePropertyRecord& property : record.Properties)
            {
                writer.BeginObject();
                writer.WriteString("name", property.Name);
                writer.WriteString("propertyId", ToDecimalString(property.PropertyId));
                writer.WriteString("type", property.TypeName);
                writer.WriteString("typeId", ToDecimalString(property.TypeId));
                writer.WriteString("value", property.Value);
                writer.EndObject();
            }
            writer.EndArray();
            writer.EndObject();
        }

        void WriteEntityRecordMembers(JsonWriter& writer, const SceneEntityRecord& record)
        {
            writer.WriteString("alias", ToDecimalString(record.Alias));
            writer.WriteString("parentAlias", ToDecimalString(record.ParentAlias));
            WriteObjectRecord(writer, "object", record.Object);

            writer.BeginArray("components");
            for (const SceneComponentRecord& component : record.Components)
            {
                writer.BeginObject();
                writer.WriteString("alias", ToDecimalString(component.Alias));
                writer.WriteString("ownerAlias", ToDecimalString(component.OwnerAlias));
                WriteObjectRecord(writer, "object", component.Object);
                writer.EndObject();
            }
            writer.EndArray();

            writer.BeginArray("children");
            for (const SceneEntityRecord& child : record.Children)
            {
                writer.BeginObject();
                WriteEntityRecordMembers(writer, child);
                writer.EndObject();
            }
            writer.EndArray();
        }

        // ========================================
        // JSON 読み取り（構造のみ。スキーマ照合はReconcileWithSchemaが担う）
        // ========================================

        bool SetParseError(Container::String* pOutError, const char* message)
        {
            if (pOutError)
            {
                *pOutError = message;
            }
            return false;
        }

        bool ReadUInt64Member(const JsonValue& value, const char* key, uint64_t& outValue)
        {
            const JsonValue member = value.FindMember(key);
            if (!member.IsString())
            {
                return false;
            }
            return TryParseUInt64(member.AsString(), outValue);
        }

        bool ParseObjectRecord(const JsonValue& value, SceneObjectRecord& outRecord, Container::String* pOutError)
        {
            if (!value.IsObject())
            {
                return SetParseError(pOutError, "scene: object record must be a JSON object");
            }

            outRecord.ClassName = value.FindMember("class").AsString();
            uint64_t classId = 0;
            if (!ReadUInt64Member(value, "classId", classId))
            {
                return SetParseError(pOutError, "scene: classId must be a decimal string");
            }
            outRecord.ClassId = classId;
            outRecord.Path = value.FindMember("path").AsString();

            const JsonValue properties = value.FindMember("properties");
            if (!properties.IsArray())
            {
                return SetParseError(pOutError, "scene: properties must be an array");
            }
            outRecord.Properties.reserve(properties.GetArraySize());
            for (size_t index = 0; index < properties.GetArraySize(); ++index)
            {
                const JsonValue propertyValue = properties.GetArrayElement(index);
                if (!propertyValue.IsObject())
                {
                    return SetParseError(pOutError, "scene: property entry must be an object");
                }

                ScenePropertyRecord propertyRecord;
                propertyRecord.Name = propertyValue.FindMember("name").AsString();
                uint64_t propertyId = 0;
                if (!ReadUInt64Member(propertyValue, "propertyId", propertyId))
                {
                    return SetParseError(pOutError, "scene: propertyId must be a decimal string");
                }
                propertyRecord.PropertyId = propertyId;
                propertyRecord.TypeName = propertyValue.FindMember("type").AsString();
                uint64_t typeId = 0;
                if (!ReadUInt64Member(propertyValue, "typeId", typeId))
                {
                    return SetParseError(pOutError, "scene: typeId must be a decimal string");
                }
                propertyRecord.TypeId = typeId;

                const JsonValue serialized = propertyValue.FindMember("value");
                if (!serialized.IsString())
                {
                    return SetParseError(pOutError, "scene: property value must be a string");
                }
                propertyRecord.Value = serialized.AsString();
                outRecord.Properties.push_back(std::move(propertyRecord));
            }
            return true;
        }

        bool ParseEntityRecord(const JsonValue& value, SceneEntityRecord& outRecord, Container::String* pOutError)
        {
            if (!value.IsObject())
            {
                return SetParseError(pOutError, "scene: entity record must be a JSON object");
            }

            if (!ReadUInt64Member(value, "alias", outRecord.Alias))
            {
                return SetParseError(pOutError, "scene: entity alias must be a decimal string");
            }
            if (!ReadUInt64Member(value, "parentAlias", outRecord.ParentAlias))
            {
                return SetParseError(pOutError, "scene: entity parentAlias must be a decimal string");
            }
            if (!ParseObjectRecord(value.FindMember("object"), outRecord.Object, pOutError))
            {
                return false;
            }

            const JsonValue components = value.FindMember("components");
            if (!components.IsArray())
            {
                return SetParseError(pOutError, "scene: components must be an array");
            }
            outRecord.Components.reserve(components.GetArraySize());
            for (size_t index = 0; index < components.GetArraySize(); ++index)
            {
                const JsonValue componentValue = components.GetArrayElement(index);
                if (!componentValue.IsObject())
                {
                    return SetParseError(pOutError, "scene: component entry must be an object");
                }

                SceneComponentRecord componentRecord;
                if (!ReadUInt64Member(componentValue, "alias", componentRecord.Alias))
                {
                    return SetParseError(pOutError, "scene: component alias must be a decimal string");
                }
                if (!ReadUInt64Member(componentValue, "ownerAlias", componentRecord.OwnerAlias))
                {
                    return SetParseError(pOutError, "scene: component ownerAlias must be a decimal string");
                }
                if (!ParseObjectRecord(componentValue.FindMember("object"), componentRecord.Object, pOutError))
                {
                    return false;
                }
                outRecord.Components.push_back(std::move(componentRecord));
            }

            const JsonValue children = value.FindMember("children");
            if (!children.IsArray())
            {
                return SetParseError(pOutError, "scene: children must be an array");
            }
            outRecord.Children.reserve(children.GetArraySize());
            for (size_t index = 0; index < children.GetArraySize(); ++index)
            {
                SceneEntityRecord childRecord;
                if (!ParseEntityRecord(children.GetArrayElement(index), childRecord, pOutError))
                {
                    return false;
                }
                outRecord.Children.push_back(std::move(childRecord));
            }
            return true;
        }

        // ========================================
        // 寛容フィルタ（現行スキーマとの突き合わせ）
        // ========================================

        size_t CountRecordEntities(const SceneEntityRecord& record)
        {
            size_t count = 1;
            for (const SceneEntityRecord& child : record.Children)
            {
                count += CountRecordEntities(child);
            }
            return count;
        }

        size_t CountRecordComponents(const SceneEntityRecord& record)
        {
            size_t count = record.Components.size();
            for (const SceneEntityRecord& child : record.Children)
            {
                count += CountRecordComponents(child);
            }
            return count;
        }

        const IClass* ResolveRecordClass(const SceneObjectRecord& record)
        {
            if (!record.ClassName.empty())
            {
                const IClass* cls = ClassRegistry::Get().FindClass(Identity(record.ClassName));
                if (cls)
                {
                    return cls;
                }
            }
            if (record.ClassId != InvalidSchemaId)
            {
                Container::VariableArray<const IClass*> classes = ClassRegistry::Get().GetAllClasses();
                for (const IClass* cls : classes)
                {
                    if (cls && MakeSceneStableClassId(cls->GetClassName().GetView()) == record.ClassId)
                    {
                        return cls;
                    }
                }
            }
            return nullptr;
        }

        enum class PropertyReconcileResult : uint8_t
        {
            Accepted,
            SkippedRuntimeOnly,
            Dropped
        };

        PropertyReconcileResult ReconcileProperty(
            const IClass& cls,
            const ScenePropertyRecord& record,
            ProjectedPropertyValue& outValue)
        {
            const ClassProperty* property = nullptr;
            if (!record.Name.empty())
            {
                property = cls.GetProperty(Identity(record.Name));
            }
            if (!property && record.PropertyId != InvalidSchemaId)
            {
                Container::VariableArray<const ClassProperty*> properties = cls.GetAllProperties();
                for (const ClassProperty* candidate : properties)
                {
                    if (candidate &&
                        MakeSceneStablePropertyId(cls.GetClassName().GetView(), candidate->GetName().GetView()) == record.PropertyId)
                    {
                        property = candidate;
                        break;
                    }
                }
            }
            if (!property)
            {
                NORVES_LOG_WARNING("Scene", "Unknown property '%s' (id=%llu) on class '%s'; dropped",
                                   record.Name.c_str(),
                                   static_cast<unsigned long long>(record.PropertyId),
                                   cls.GetClassName().ToString().c_str());
                return PropertyReconcileResult::Dropped;
            }

            // ObjectId/bPendingDestroy等のランタイム専用はSpawnPrefab側でも無視されるため黙って除去する
            if (ShouldSkipPrefabRestoreProperty(cls, *property))
            {
                return PropertyReconcileResult::SkippedRuntimeOnly;
            }

            const TypeId runtimeType = property->GetRuntimeTypeId();
            const TypeInfo* typeInfo = TypeRegistry::Get().Find(runtimeType);
            if (!typeInfo)
            {
                NORVES_LOG_WARNING("Scene", "Property '%s' on class '%s' has no runtime type info; dropped",
                                   property->GetName().ToString().c_str(),
                                   cls.GetClassName().ToString().c_str());
                return PropertyReconcileResult::Dropped;
            }

            const bool bTypeMatches =
                (!record.TypeName.empty() && typeInfo->Name == record.TypeName) ||
                record.TypeId == typeInfo->StableId;
            if (!bTypeMatches)
            {
                NORVES_LOG_WARNING("Scene", "Property '%s' on class '%s': stored type '%s' (id=%llu) does not match current type '%s'; dropped",
                                   property->GetName().ToString().c_str(),
                                   cls.GetClassName().ToString().c_str(),
                                   record.TypeName.c_str(),
                                   static_cast<unsigned long long>(record.TypeId),
                                   typeInfo->Name.c_str());
                return PropertyReconcileResult::Dropped;
            }

            // 値がdeserialize可能かプローブしてからSpawnPrefabへ渡す（全か無かロールバック回避の要）
            PropertyValue probe;
            if (!probe.DeserializeStable(typeInfo->StableId, record.Value))
            {
                NORVES_LOG_WARNING("Scene", "Property '%s' on class '%s': value '%s' failed to deserialize; dropped",
                                   property->GetName().ToString().c_str(),
                                   cls.GetClassName().ToString().c_str(),
                                   record.Value.c_str());
                return PropertyReconcileResult::Dropped;
            }

            outValue.Property = MakeSceneStablePropertyId(cls.GetClassName().GetView(), property->GetName().GetView());
            outValue.Type = typeInfo->StableId;
            outValue.SerializedValue = record.Value;
            return PropertyReconcileResult::Accepted;
        }

        void ReconcileObjectInto(const IClass& cls, const SceneObjectRecord& record, ObjectSnapshot& outSnapshot, SceneLoadStats& stats)
        {
            outSnapshot.Class = MakeSceneStableClassId(cls.GetClassName().GetView());
            outSnapshot.Path = record.Path;
            outSnapshot.Ref.Path = record.Path;
            outSnapshot.Properties.reserve(record.Properties.size());
            for (const ScenePropertyRecord& propertyRecord : record.Properties)
            {
                ProjectedPropertyValue value;
                const PropertyReconcileResult result = ReconcileProperty(cls, propertyRecord, value);
                if (result == PropertyReconcileResult::Accepted)
                {
                    outSnapshot.Properties.push_back(std::move(value));
                }
                else if (result == PropertyReconcileResult::Dropped)
                {
                    ++stats.DroppedProperties;
                }
            }
        }

        bool ReconcileEntityRecord(
            const SceneEntityRecord& record,
            SubtreeSnapshotAliasId parentAlias,
            SubtreeSnapshotAliasId& nextAlias,
            EntitySubtreeSnapshotNode& outNode,
            SceneLoadStats& stats)
        {
            const IClass* cls = ResolveRecordClass(record.Object);
            if (!cls || !cls->IsChildOf(Entity::StaticClass()))
            {
                NORVES_LOG_WARNING("Scene", "Unknown entity class '%s' (id=%llu); subtree dropped",
                                   record.Object.ClassName.c_str(),
                                   static_cast<unsigned long long>(record.Object.ClassId));
                stats.DroppedEntities += CountRecordEntities(record);
                stats.DroppedComponents += CountRecordComponents(record);
                return false;
            }

            outNode.Alias = nextAlias++;
            outNode.ParentAlias = parentAlias;
            ReconcileObjectInto(*cls, record.Object, outNode.Object, stats);

            outNode.Components.reserve(record.Components.size());
            for (const SceneComponentRecord& componentRecord : record.Components)
            {
                const IClass* componentClass = ResolveRecordClass(componentRecord.Object);
                if (!componentClass || !componentClass->IsChildOf(Component::Component::StaticClass()))
                {
                    NORVES_LOG_WARNING("Scene", "Unknown component class '%s' (id=%llu); component dropped",
                                       componentRecord.Object.ClassName.c_str(),
                                       static_cast<unsigned long long>(componentRecord.Object.ClassId));
                    ++stats.DroppedComponents;
                    continue;
                }

                ComponentSubtreeSnapshot componentSnapshot;
                componentSnapshot.Alias = nextAlias++;
                componentSnapshot.OwnerAlias = outNode.Alias;
                ReconcileObjectInto(*componentClass, componentRecord.Object, componentSnapshot.Object, stats);
                outNode.Components.push_back(std::move(componentSnapshot));
            }

            outNode.Children.reserve(record.Children.size());
            for (const SceneEntityRecord& childRecord : record.Children)
            {
                EntitySubtreeSnapshotNode childNode;
                if (ReconcileEntityRecord(childRecord, outNode.Alias, nextAlias, childNode, stats))
                {
                    outNode.Children.push_back(std::move(childNode));
                }
            }
            return true;
        }
        // ========================================
        // 保存側: pending-destroyフィルタ＋シリアライズ不能プロパティ計上
        // BuildEntitySubtreeSnapshotNode（SchemaProjection.cpp）と同じ走査条件
        // （null/GetClass() nullをskip）でライブ列とスナップショット列を
        // ロックステップ対応させる。SchemaProjection自体は変更しない
        // （PrefabRoundTripTestが無条件walkを前提にしているため）。
        // ========================================

        struct SceneCaptureStats
        {
            size_t PrunedEntities = 0;
            size_t PrunedComponents = 0;
            size_t UnserializedProperties = 0;
        };

        size_t CountSnapshotEntities(const EntitySubtreeSnapshotNode& node)
        {
            size_t count = 1;
            for (const EntitySubtreeSnapshotNode& child : node.Children)
            {
                count += CountSnapshotEntities(child);
            }
            return count;
        }

        void CountUnserializedProperties(const Object& liveObject, const ObjectSnapshot& snapshot, SceneCaptureStats& stats)
        {
            const IClass* cls = liveObject.GetClass();
            if (!cls)
            {
                return;
            }

            size_t total = 0;
            Container::VariableArray<const ClassProperty*> properties = cls->GetAllProperties();
            for (const ClassProperty* property : properties)
            {
                if (property)
                {
                    ++total;
                }
            }
            if (total > snapshot.Properties.size())
            {
                stats.UnserializedProperties += total - snapshot.Properties.size();
            }
        }

        void PruneSnapshotNode(const Entity& liveEntity, EntitySubtreeSnapshotNode& node, SceneCaptureStats& stats)
        {
            CountUnserializedProperties(liveEntity, node.Object, stats);

            Container::VariableArray<Component::Component*> components = liveEntity.GetComponents();
            Container::VariableArray<ComponentSubtreeSnapshot> keptComponents;
            keptComponents.reserve(node.Components.size());
            size_t componentIndex = 0;
            for (Component::Component* component : components)
            {
                if (!component || !component->GetClass())
                {
                    continue; // projector側もskipするため添字を進めない
                }
                if (componentIndex >= node.Components.size())
                {
                    break; // 走査中にWorldが変わった場合の保険（GameThread前提で通常到達しない）
                }

                ComponentSubtreeSnapshot& componentSnapshot = node.Components[componentIndex];
                ++componentIndex;

                if (component->IsPendingDestroy())
                {
                    ++stats.PrunedComponents;
                    continue;
                }

                CountUnserializedProperties(*component, componentSnapshot.Object, stats);
                keptComponents.push_back(std::move(componentSnapshot));
            }
            node.Components = std::move(keptComponents);

            Container::VariableArray<Entity*> children = liveEntity.GetChildEntities();
            Container::VariableArray<EntitySubtreeSnapshotNode> keptChildren;
            keptChildren.reserve(node.Children.size());
            size_t childIndex = 0;
            for (Entity* child : children)
            {
                if (!child || !child->GetClass())
                {
                    continue;
                }
                if (childIndex >= node.Children.size())
                {
                    break;
                }

                EntitySubtreeSnapshotNode& childNode = node.Children[childIndex];
                ++childIndex;

                if (child->IsPendingDestroy())
                {
                    stats.PrunedEntities += CountSnapshotEntities(childNode);
                    continue;
                }

                PruneSnapshotNode(*child, childNode, stats);
                keptChildren.push_back(std::move(childNode));
            }
            node.Children = std::move(keptChildren);
        }

    } // namespace

    SceneDocument SceneSerializer::BuildDocument(const Container::VariableArray<EntitySubtreeSnapshot>& roots)
    {
        SchemaNameResolver resolver;

        SceneDocument document;
        document.FormatVersion = SceneFileFormatVersion;
        document.Roots.reserve(roots.size());
        for (const EntitySubtreeSnapshot& snapshot : roots)
        {
            SceneRootRecord record;
            record.FormatVersion = snapshot.FormatVersion;
            record.RootAlias = snapshot.RootAlias;
            record.RootPath = snapshot.RootPath;
            record.Root = BuildEntityRecord(snapshot.Root, resolver);
            document.Roots.push_back(std::move(record));
        }
        return document;
    }

    Container::String SceneSerializer::ToJson(const SceneDocument& document)
    {
        JsonWriter writer(true);
        writer.BeginObject();
        writer.WriteUInt64("formatVersion", document.FormatVersion);
        writer.WriteString("module", kModuleName);
        writer.BeginArray("roots");
        for (const SceneRootRecord& record : document.Roots)
        {
            writer.BeginObject();
            writer.WriteUInt64("formatVersion", record.FormatVersion);
            writer.WriteString("rootAlias", ToDecimalString(record.RootAlias));
            writer.WriteString("rootPath", record.RootPath);
            writer.BeginObject("root");
            WriteEntityRecordMembers(writer, record.Root);
            writer.EndObject();
            writer.EndObject();
        }
        writer.EndArray();
        writer.EndObject();

        // 防御チェック: 書き出しロジックの退行でエラー/未完成のまま不正JSONを返さない
        if (!writer.IsComplete())
        {
            NORVES_LOG_ERROR("Scene", "ToJson: JSON writer finished in an incomplete or error state");
            return Container::String();
        }
        return writer.ToString();
    }

    bool SceneSerializer::TryParseJson(
        const Container::String& jsonText,
        SceneDocument& outDocument,
        Container::String* pOutError)
    {
        outDocument = SceneDocument();

        JsonDocument document;
        Container::String parseError;
        if (!JsonDocument::TryParse(jsonText, document, &parseError))
        {
            if (pOutError)
            {
                *pOutError = parseError;
            }
            return false;
        }

        const JsonValue root = document.GetRoot();
        if (!root.IsObject())
        {
            return SetParseError(pOutError, "scene: document root must be an object");
        }

        const JsonValue formatVersion = root.FindMember("formatVersion");
        if (!formatVersion.IsNumber())
        {
            return SetParseError(pOutError, "scene: formatVersion is required");
        }
        const uint32_t version = formatVersion.AsUInt32();
        if (version != SceneFileFormatVersion)
        {
            return SetParseError(pOutError, "scene: unsupported formatVersion");
        }
        outDocument.FormatVersion = version;

        // "module"フィールドは診断専用。ロード時は読み取り・検証せずSceneDocumentにも保持しない。

        const JsonValue roots = root.FindMember("roots");
        if (!roots.IsArray())
        {
            return SetParseError(pOutError, "scene: roots must be an array");
        }

        outDocument.Roots.reserve(roots.GetArraySize());
        for (size_t index = 0; index < roots.GetArraySize(); ++index)
        {
            const JsonValue rootValue = roots.GetArrayElement(index);
            if (!rootValue.IsObject())
            {
                return SetParseError(pOutError, "scene: root entry must be an object");
            }

            SceneRootRecord record;
            const JsonValue rootFormatVersion = rootValue.FindMember("formatVersion");
            if (!rootFormatVersion.IsNumber())
            {
                return SetParseError(pOutError, "scene: root formatVersion is required");
            }
            record.FormatVersion = rootFormatVersion.AsUInt32();
            if (!ReadUInt64Member(rootValue, "rootAlias", record.RootAlias))
            {
                return SetParseError(pOutError, "scene: rootAlias must be a decimal string");
            }
            record.RootPath = rootValue.FindMember("rootPath").AsString();
            if (!ParseEntityRecord(rootValue.FindMember("root"), record.Root, pOutError))
            {
                return false;
            }
            outDocument.Roots.push_back(std::move(record));
        }
        return true;
    }

    bool SceneSerializer::ReconcileWithSchema(
        const SceneDocument& document,
        Container::VariableArray<EntitySubtreeSnapshot>& outRoots,
        SceneLoadStats& outStats)
    {
        outRoots.clear();
        outStats = SceneLoadStats(); // outRootsのclear()と対称にゼロ化する（累積させない）
        outRoots.reserve(document.Roots.size());
        for (const SceneRootRecord& rootRecord : document.Roots)
        {
            // root単位のformatVersionはトップレベルと同方針で検証するが、
            // ファイル全体は殺さず該当ルートのみ警告付きdrop（寛容ロード）。
            if (rootRecord.FormatVersion != SceneFileFormatVersion)
            {
                NORVES_LOG_WARNING("Scene", "Unsupported root formatVersion %u; root subtree dropped",
                                   rootRecord.FormatVersion);
                outStats.DroppedEntities += CountRecordEntities(rootRecord.Root);
                outStats.DroppedComponents += CountRecordComponents(rootRecord.Root);
                continue;
            }

            EntitySubtreeSnapshot snapshot;
            snapshot.FormatVersion = rootRecord.FormatVersion;
            snapshot.RootAlias = 1;
            snapshot.RootPath = rootRecord.RootPath;

            SubtreeSnapshotAliasId nextAlias = 1;
            if (!ReconcileEntityRecord(rootRecord.Root, InvalidSubtreeSnapshotAliasId, nextAlias, snapshot.Root, outStats))
            {
                continue; // ルートごとdrop（警告出力済み）
            }
            outRoots.push_back(std::move(snapshot));
            ++outStats.LoadedRoots;
        }
        return true;
    }

    size_t SceneSerializer::CaptureWorld(const World& world, Container::VariableArray<EntitySubtreeSnapshot>& outRoots)
    {
        outRoots.clear();

        SceneCaptureStats stats;
        Container::VariableArray<Entity*> roots = world.GetRootEntities();
        outRoots.reserve(roots.size());
        for (Entity* root : roots)
        {
            if (!root || !root->GetClass())
            {
                continue;
            }
            if (root->IsPendingDestroy())
            {
                ++stats.PrunedEntities;
                continue;
            }

            EntitySubtreeSnapshot snapshot = RuntimeSchemaProjector::BuildEntitySubtreeSnapshot(*root);
            PruneSnapshotNode(*root, snapshot.Root, stats);
            outRoots.push_back(std::move(snapshot));
        }

        if (stats.PrunedEntities > 0 || stats.PrunedComponents > 0)
        {
            NORVES_LOG_INFO("Scene", "CaptureWorld: pruned %llu pending-destroy entities and %llu components",
                            static_cast<unsigned long long>(stats.PrunedEntities),
                            static_cast<unsigned long long>(stats.PrunedComponents));
        }
        if (stats.UnserializedProperties > 0)
        {
            NORVES_LOG_WARNING("Scene", "CaptureWorld: %llu properties could not be serialized and were omitted",
                               static_cast<unsigned long long>(stats.UnserializedProperties));
        }

        return outRoots.size();
    }

    bool SceneSerializer::SaveToFile(const World& world, const Container::String& filePath)
    {
        Container::VariableArray<EntitySubtreeSnapshot> roots;
        CaptureWorld(world, roots);

        const SceneDocument document = BuildDocument(roots);
        const Container::String json = ToJson(document);
        // ToJsonはwriterのIsComplete()/HasError()検査で失敗時に空文字を返す（防御チェック）
        if (json.empty())
        {
            NORVES_LOG_ERROR("Scene", "SaveToFile: JSON serialization failed (writer incomplete or error)");
            return false;
        }

        NorvesLib::FileStream::FileStreamUniquePtr stream = NorvesLib::FileStream::FileStream::CreateUnique(
            filePath,
            NorvesLib::FileStream::FileMode::Write,
            NorvesLib::FileStream::FileAccess::Write);
        // TUniquePtrはstd::unique_ptrの素のエイリアスで、IsNull/IsValidはフリー関数
        // （Container/PointerTypes.h L24-25, L113-129）。メンバ呼び出しは不可なので!streamで判定する。
        if (!stream || !stream->IsOpen())
        {
            NORVES_LOG_ERROR("Scene", "SaveToFile: failed to open '%s' for writing", filePath.c_str());
            return false;
        }

        const size_t written = stream->WriteString(json);
        stream->Flush();
        if (written != json.size())
        {
            NORVES_LOG_ERROR("Scene", "SaveToFile: short write to '%s' (%llu of %llu bytes)",
                             filePath.c_str(),
                             static_cast<unsigned long long>(written),
                             static_cast<unsigned long long>(json.size()));
            return false;
        }

        NORVES_LOG_INFO("Scene", "SaveToFile: wrote %llu roots to '%s'",
                        static_cast<unsigned long long>(document.Roots.size()),
                        filePath.c_str());
        return true;
    }

    bool SceneSerializer::LoadIntoWorld(World& world, const Container::String& filePath, SceneLoadStats* pOutStats)
    {
        Container::String jsonText;
        {
            NorvesLib::FileStream::FileStreamUniquePtr stream = NorvesLib::FileStream::FileStream::CreateUnique(
                filePath,
                NorvesLib::FileStream::FileMode::Read,
                NorvesLib::FileStream::FileAccess::Read);
            if (!stream || !stream->IsOpen())
            {
                NORVES_LOG_ERROR("Scene", "LoadIntoWorld: failed to open '%s' for reading", filePath.c_str());
                if (pOutStats)
                {
                    *pOutStats = SceneLoadStats();
                }
                return false;
            }
            jsonText = stream->ReadString();
        }

        SceneDocument document;
        Container::String parseError;
        if (!TryParseJson(jsonText, document, &parseError))
        {
            NORVES_LOG_ERROR("Scene", "LoadIntoWorld: parse failed for '%s': %s",
                             filePath.c_str(), parseError.c_str());
            if (pOutStats)
            {
                *pOutStats = SceneLoadStats();
            }
            return false;
        }

        SceneLoadStats stats;
        Container::VariableArray<EntitySubtreeSnapshot> roots;
        if (!ReconcileWithSchema(document, roots, stats))
        {
            if (pOutStats)
            {
                *pOutStats = stats;
            }
            return false;
        }

        // ReconcileWithSchemaはreconcile成功ルート数をLoadedRootsに計上するが、
        // LoadIntoWorldは実際にSpawnPrefabできたルート数を報告するため一旦ゼロ化する。
        stats.LoadedRoots = 0;

        // Bridge sceneDuplicateObjectと同経路: 関数ローカルRegistry＋一時PrefabAsset。
        // SpawnPrefabは同期消費するため関数ローカル寿命で十分。
        ResourceRegistry registry;
        if (!registry.Initialize())
        {
            NORVES_LOG_ERROR("Scene", "LoadIntoWorld: transient ResourceRegistry initialization failed");
            if (pOutStats)
            {
                *pOutStats = stats;
            }
            return false;
        }

        bool bAllSpawned = true;
        {
            Container::VariableArray<Container::TSharedPtr<PrefabAsset>> prefabs;
            prefabs.reserve(roots.size());
            for (EntitySubtreeSnapshot& root : roots)
            {
                Container::TSharedPtr<PrefabAsset> prefab = registry.CreateTransient<PrefabAsset>("SceneLoad");
                if (prefab == nullptr)
                {
                    bAllSpawned = false;
                    continue;
                }
                prefabs.push_back(prefab);
                prefab->SetTree(std::move(root));

                Entity* spawned = world.SpawnPrefab(*prefab);
                if (spawned == nullptr)
                {
                    NORVES_LOG_ERROR("Scene", "LoadIntoWorld: SpawnPrefab failed for a scene root in '%s'", filePath.c_str());
                    bAllSpawned = false;
                    continue;
                }
                ++stats.LoadedRoots;
            }
            prefabs.clear();
        }
        registry.Shutdown();

        if (pOutStats)
        {
            *pOutStats = stats;
        }
        NORVES_LOG_INFO("Scene", "LoadIntoWorld: spawned %llu roots from '%s' (dropped: %llu entities, %llu components, %llu properties)",
                        static_cast<unsigned long long>(stats.LoadedRoots),
                        filePath.c_str(),
                        static_cast<unsigned long long>(stats.DroppedEntities),
                        static_cast<unsigned long long>(stats.DroppedComponents),
                        static_cast<unsigned long long>(stats.DroppedProperties));
        return bAllSpawned;
    }

} // namespace NorvesLib::Core::Scene
