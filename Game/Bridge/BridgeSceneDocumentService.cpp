// 資産ルート内のシーン保存・読み込みと、Entity・全 Component の生成・プロパティ操作を実装する。
#include "BridgeSceneDocumentService.h"

#include "BridgeAssetPath.h"
#include "Component/Component.h"
#include "Object/Entity.h"
#include "Object/IClass.h"
#include "Object/SchemaProjection.h"
#include "Object/World.h"
#include "Scene/SceneSerializer.h"
#include "Text/IdentityPool.h"

namespace Game::Bridge
{
    namespace Core = NorvesLib::Core;

    namespace
    {
        BridgeSceneResult Success()
        {
            BridgeSceneResult result;
            result.Success = true;
            return result;
        }

        BridgeSceneResult Failure(const char* code, const char8_t* message)
        {
            BridgeSceneResult result;
            result.Code = code;
            result.Message = reinterpret_cast<const char*>(message);
            return result;
        }

        BridgeSceneResult InvalidPath()
        {
            return Failure("invalid_path", u8"資産ルート内の .scene.json の相対パスを指定してください。");
        }

        BridgeSceneResult LoadFailed()
        {
            return Failure("load_failed", u8"シーンファイルを読み込めません。");
        }

        BridgeSceneResult SaveFailed()
        {
            return Failure("save_failed", u8"シーンファイルを保存できません。");
        }

        BridgeSceneResult UnknownProperty()
        {
            return Failure("unknown_property", u8"プロパティが見つかりません。");
        }

        BridgeSceneResult InvalidTarget()
        {
            return Failure("invalid_target", u8"操作対象のクラスまたは所属する World が正しくありません。");
        }

        bool ResolveRelativePath(const Container::String& path, Container::WideString& out)
        {
            if (!AssetPath::IsRelativeAssetPath(path) || !AssetPath::HasExtension(path, ".scene.json") ||
                !AssetPath::Utf8ToWide(path, out))
            {
                return false;
            }
            // 最終パスの形式に合わせて、論理パスの区切りをそろえる。
            for (size_t index = 0; index < out.size(); ++index)
            {
                if (out[index] == L'/')
                {
                    out[index] = L'\\';
                }
            }
            return true;
        }

        Container::WideString JoinPath(const Container::WideString& parent, const Container::WideString& child)
        {
            Container::WideString path = parent;
            if (!path.empty() && path[path.size() - 1] != L'\\')
            {
                path += L'\\';
            }
            path += child;
            return path;
        }

        bool IsSamePath(const Container::WideString& left, const Container::WideString& right)
        {
            return CompareStringOrdinal(left.c_str(), -1, right.c_str(), -1, TRUE) == CSTR_EQUAL;
        }

        bool IsReadOnly(Core::PropertyFlags flags)
        {
            return (flags & Core::PropertyFlags::ReadOnly) != Core::PropertyFlags::None;
        }

        bool ApplyProperty(Core::Object& target, const Core::ClassProperty& property, const Core::PropertyValue& value)
        {
            if (Core::Entity* entity = Core::CastTo<Core::Entity>(&target))
            {
                const Core::Identity& name = property.GetName();
                if (name == Core::Identity("Position"))
                {
                    const NorvesLib::Math::Vector3* position = value.Get<NorvesLib::Math::Vector3>();
                    if (!position)
                    {
                        return false;
                    }
                    entity->SetLocalPosition(*position);
                    return true;
                }
                if (name == Core::Identity("Rotation"))
                {
                    const NorvesLib::Math::Quaternion* rotation = value.Get<NorvesLib::Math::Quaternion>();
                    if (!rotation)
                    {
                        return false;
                    }
                    entity->SetLocalRotation(*rotation);
                    return true;
                }
                if (name == Core::Identity("Scale"))
                {
                    const NorvesLib::Math::Vector3* scale = value.Get<NorvesLib::Math::Vector3>();
                    if (!scale)
                    {
                        return false;
                    }
                    entity->SetLocalScale(*scale);
                    return true;
                }
            }
            return property.ApplyValue(static_cast<Core::IUnknown*>(&target), value);
        }
    }

    BridgeSceneDocumentService::BridgeSceneDocumentService(const Container::String& assetRoot)
    {
        AssetPath::ResolveRoot(assetRoot, m_RootPath);
    }

    BridgeSceneResult BridgeSceneDocumentService::LoadScene(Core::World& world, const Container::String& logicalPath,
        Core::Scene::SceneLoadStats* pOutStats, Container::VariableArray<Core::Entity*>* pOutRoots) const
    {
        if (pOutStats)
        {
            *pOutStats = {};
        }
        if (pOutRoots)
        {
            pOutRoots->clear();
        }
        Container::WideString relative;
        if (!ResolveRelativePath(logicalPath, relative))
        {
            return InvalidPath();
        }
        Container::WideString root;
        if (!AssetPath::OpenRoot(m_RootPath, root))
        {
            return LoadFailed();
        }
        const Container::WideString path = JoinPath(m_RootPath, relative);
        AssetPath::NativeHandle file(CreateFileW(path.c_str(), FILE_READ_ATTRIBUTES,
            FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr, OPEN_EXISTING,
            FILE_FLAG_BACKUP_SEMANTICS, nullptr));
        BY_HANDLE_FILE_INFORMATION info{};
        Container::WideString finalPath;
        if (file.Value == INVALID_HANDLE_VALUE || GetFileType(file.Value) != FILE_TYPE_DISK ||
            !GetFileInformationByHandle(file.Value, &info) || (info.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) != 0 ||
            !AssetPath::FinalPath(file.Value, finalPath))
        {
            return LoadFailed();
        }
        if (!AssetPath::IsWithinRoot(root, finalPath))
        {
            return InvalidPath();
        }
        Container::String streamPath;
        if (!AssetPath::WideToFileStreamPath(finalPath, streamPath) ||
            !Core::Scene::SceneSerializer::LoadIntoWorld(world, streamPath, pOutStats, pOutRoots))
        {
            return LoadFailed();
        }
        return Success();
    }

    BridgeSceneResult BridgeSceneDocumentService::SaveScene(const Core::World& world, const Container::String& logicalPath,
        Core::Scene::SceneSaveStats* pOutStats) const
    {
        if (pOutStats)
        {
            *pOutStats = {};
        }
        Container::WideString relative;
        if (!ResolveRelativePath(logicalPath, relative))
        {
            return InvalidPath();
        }
        Container::WideString root;
        if (!AssetPath::OpenRoot(m_RootPath, root))
        {
            return SaveFailed();
        }
        size_t nameStart = 0;
        for (size_t index = 0; index < relative.size(); ++index)
        {
            if (relative[index] == L'\\')
            {
                nameStart = index + 1;
            }
        }
        const Container::WideString parentPath = nameStart == 0 ? m_RootPath : JoinPath(m_RootPath, relative.substr(0, nameStart - 1));
        AssetPath::NativeHandle parent(CreateFileW(parentPath.c_str(), FILE_READ_ATTRIBUTES,
            FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr, OPEN_EXISTING, FILE_FLAG_BACKUP_SEMANTICS, nullptr));
        BY_HANDLE_FILE_INFORMATION parentInfo{};
        Container::WideString finalParent;
        if (parent.Value == INVALID_HANDLE_VALUE || !GetFileInformationByHandle(parent.Value, &parentInfo) ||
            (parentInfo.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) == 0 || !AssetPath::FinalPath(parent.Value, finalParent))
        {
            return SaveFailed();
        }
        if (!IsSamePath(root, finalParent) && !AssetPath::IsWithinRoot(root, finalParent))
        {
            return InvalidPath();
        }
        const Container::WideString destination = JoinPath(finalParent, relative.substr(nameStart));
        {
            AssetPath::NativeHandle existing(CreateFileW(destination.c_str(), FILE_READ_ATTRIBUTES,
                FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr, OPEN_EXISTING,
                FILE_FLAG_BACKUP_SEMANTICS, nullptr));
            if (existing.Value != INVALID_HANDLE_VALUE)
            {
                BY_HANDLE_FILE_INFORMATION info{};
                Container::WideString finalPath;
                if (!GetFileInformationByHandle(existing.Value, &info) || !AssetPath::FinalPath(existing.Value, finalPath))
                {
                    return SaveFailed();
                }
                if ((info.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) != 0 || !AssetPath::IsWithinRoot(root, finalPath))
                {
                    return InvalidPath();
                }
            }
            else if (GetLastError() != ERROR_FILE_NOT_FOUND)
            {
                return SaveFailed();
            }
        }
        Container::String streamPath;
        if (!AssetPath::WideToFileStreamPath(destination, streamPath) ||
            !Core::Scene::SceneSerializer::SaveToFile(world, streamPath, pOutStats))
        {
            return SaveFailed();
        }
        return Success();
    }

    BridgeSceneResult BridgeSceneDocumentService::CreateEntity(Core::World& world, const Container::String& className,
        Core::Entity* parent, Core::Entity*& outEntity)
    {
        outEntity = nullptr;
        const Core::IClass* cls = className.empty() ? nullptr : Core::ClassRegistry::Get().FindClass(Core::Identity(className));
        if (!cls)
        {
            return Failure("unknown_class", u8"クラスが見つかりません。");
        }
        if (!cls->IsChildOf(Core::Entity::StaticClass()))
        {
            return Failure("invalid_class", u8"Entity のクラスを指定してください。");
        }
        if (parent && parent->GetWorld() != &world)
        {
            return InvalidTarget();
        }
        outEntity = world.SpawnEntityOfClass(*cls, parent);
        if (!outEntity)
        {
            return Failure("spawn_failed", u8"Entity を生成できません。");
        }
        return Success();
    }

    BridgeSceneResult BridgeSceneDocumentService::AddComponent(Core::World& world, Core::Entity& owner,
        const Container::String& className, Core::Component::Component*& outComponent)
    {
        outComponent = nullptr;
        const Core::IClass* cls = className.empty() ? nullptr : Core::ClassRegistry::Get().FindClass(Core::Identity(className));
        if (!cls)
        {
            return Failure("unknown_class", u8"クラスが見つかりません。");
        }
        if (!cls->IsChildOf(Core::Component::Component::StaticClass()))
        {
            return Failure("invalid_class", u8"Component のクラスを指定してください。");
        }
        if (owner.GetWorld() != &world)
        {
            return InvalidTarget();
        }
        outComponent = world.CreateComponentOfClass(&owner, *cls);
        if (!outComponent)
        {
            return Failure("spawn_failed", u8"Component を追加できません。");
        }
        return Success();
    }

    BridgeSceneResult BridgeSceneDocumentService::ListProperties(const Core::Object& target,
        Container::VariableArray<BridgeSceneProperty>& outProperties)
    {
        outProperties.clear();
        const Core::IClass* cls = target.GetClass();
        if (!cls)
        {
            return InvalidTarget();
        }
        const Core::ClassSchemaProjection projection = Core::RuntimeSchemaProjector::ProjectClass(*cls, "NorvesLib");
        Core::StableObjectRef ref;
        const Core::ObjectSnapshot snapshot = Core::RuntimeSchemaProjector::BuildObjectSnapshot(target, ref, "NorvesLib");
        for (const Core::ProjectedPropertyValue& value : snapshot.Properties)
        {
            for (const Core::PropertySchemaProjection& property : projection.Properties)
            {
                if (property.StableId == Core::InvalidSchemaId || property.Name.empty() || property.StableId != value.Property)
                {
                    continue;
                }
                BridgeSceneProperty entry;
                entry.Name = property.Name;
                entry.Type = value.Type;
                entry.SerializedValue = value.SerializedValue;
                entry.bReadOnly = IsReadOnly(property.Flags);
                outProperties.push_back(entry);
                break;
            }
        }
        return Success();
    }

    BridgeSceneResult BridgeSceneDocumentService::FindProperty(const Core::Object& target, const Container::String& name,
        BridgeSceneProperty& outProperty)
    {
        outProperty = {};
        Container::VariableArray<BridgeSceneProperty> properties;
        const BridgeSceneResult result = ListProperties(target, properties);
        if (!result.Success)
        {
            return result;
        }
        for (const BridgeSceneProperty& property : properties)
        {
            if (property.Name == name)
            {
                outProperty = property;
                return Success();
            }
        }
        return UnknownProperty();
    }

    BridgeSceneResult BridgeSceneDocumentService::SetProperty(Core::Object& target, const Container::String& name,
        const Container::String& serializedValue, BridgeSceneProperty* pOutApplied)
    {
        if (pOutApplied)
        {
            *pOutApplied = {};
        }
        const Core::IClass* cls = target.GetClass();
        if (!cls)
        {
            return InvalidTarget();
        }
        const Core::ClassProperty* property = cls->GetProperty(Core::IdentityPool::Get().CreateIdentity(name));
        if (!property)
        {
            return UnknownProperty();
        }
        const Core::ClassSchemaProjection projection = Core::RuntimeSchemaProjector::ProjectClass(*cls, "NorvesLib");
        const Core::PropertySchemaProjection* projectedProperty = nullptr;
        for (const Core::PropertySchemaProjection& projected : projection.Properties)
        {
            if (projected.Name == name && projected.StableId != Core::InvalidSchemaId)
            {
                projectedProperty = &projected;
                break;
            }
        }
        if (!projectedProperty)
        {
            return UnknownProperty();
        }
        if (IsReadOnly(projectedProperty->Flags))
        {
            return Failure("read_only", u8"このプロパティは読み取り専用です。");
        }
        const Core::TypeInfo* typeInfo = Core::TypeRegistry::Get().Find(property->GetRuntimeTypeId());
        Core::PropertyValue value;
        if (!typeInfo || !value.DeserializeStable(typeInfo->StableId, serializedValue))
        {
            return Failure("invalid_value", u8"プロパティの型に合う内部表記の値を指定してください。");
        }
        if (!ApplyProperty(target, *property, value))
        {
            return Failure("apply_failed", u8"プロパティを適用できません。");
        }
        if (Core::Component::Component* component = Core::CastTo<Core::Component::Component>(&target))
        {
            component->OnPropertyApplied(*property);
        }
        if (pOutApplied)
        {
            return FindProperty(target, name, *pOutApplied);
        }
        return Success();
    }
} // namespace Game::Bridge
