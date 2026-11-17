#include "Component/MeshComponent.h"
#include "Engine/Engine.h"
#include "Rendering/ProceduralMeshGenerator.h"
#include "Thread/Atomic.h"
#include "Math/MatrixUtils.h"
#include "Object/Entity.h"
#include "Rendering/RenderResources.h"
#include "Logging/LogMacros.h"
#include <cmath>
#include <limits>

namespace NorvesLib::Core::Component
{
    IMPLEMENT_CLASS(MeshComponent, Component)

    namespace
    {
        constexpr uint64_t kProceduralMeshHandleBase = 0x5052'4F43'0000'0000ull;
        constexpr uint64_t kProceduralMeshHandleEnd = 0x5052'4F43'FFFF'FFFFull;
        Thread::Atomic<uint64_t> g_NextProceduralMeshHandle(kProceduralMeshHandleBase + 1);

        bool IsSpecWhitespace(TCHAR character)
        {
            return character == static_cast<TCHAR>(' ') || character == static_cast<TCHAR>('\t') ||
                   character == static_cast<TCHAR>('\r') || character == static_cast<TCHAR>('\n') ||
                   character == static_cast<TCHAR>('\v') || character == static_cast<TCHAR>('\f');
        }

        template <typename T>
        bool AppendSpecNumber(Container::String& text, const T& value)
        {
            Container::String number;
            if (!Detail::SerializeValue<T>(&value, number))
            {
                return false;
            }
            if (!text.empty())
            {
                text += static_cast<TCHAR>(' ');
            }
            text += number;
            return true;
        }

        bool IsValidProceduralSpec(const ProceduralMeshSpec& spec)
        {
            if (spec.Kind != ProceduralMeshKind::None && spec.Kind != ProceduralMeshKind::UVSphere &&
                spec.Kind != ProceduralMeshKind::Plane)
            {
                return false;
            }
            if (spec.Segments[0] == 0 || spec.Segments[1] == 0)
            {
                return false;
            }
            if (spec.Kind == ProceduralMeshKind::None)
            {
                return true;
            }
            if (!std::isfinite(spec.Size.x) || spec.Size.x <= 0.0f)
            {
                return false;
            }
            return spec.Kind != ProceduralMeshKind::Plane ||
                   (std::isfinite(spec.Size.z) && spec.Size.z > 0.0f);
        }

        bool CanGenerateProceduralSpec(const ProceduralMeshSpec& spec)
        {
            if (!IsValidProceduralSpec(spec) || spec.Kind == ProceduralMeshKind::None ||
                spec.Segments[0] == (std::numeric_limits<uint32_t>::max)() ||
                spec.Segments[1] == (std::numeric_limits<uint32_t>::max)())
            {
                return false;
            }
            const uint64_t first = spec.Segments[0];
            const uint64_t second = spec.Segments[1];
            if (spec.Kind == ProceduralMeshKind::UVSphere && second < 2)
            {
                return false;
            }
            const uint64_t rows = spec.Kind == ProceduralMeshKind::UVSphere ? second - 1 : second + 1;
            const uint64_t vertices = rows * (first + 1) + (spec.Kind == ProceduralMeshKind::UVSphere ? 2 : 0);
            const uint64_t cells = first * (spec.Kind == ProceduralMeshKind::UVSphere ? second - 1 : second);
            return vertices <= (std::numeric_limits<uint32_t>::max)() / sizeof(Rendering::Mesh3DVertex) &&
                   cells <= (std::numeric_limits<uint32_t>::max)() / 6;
        }

        Rendering::BlendMode ResolveMaterialBlendMode(
            Rendering::MaterialHandle handle,
            const Rendering::MaterialResources *materials)
        {
            if (!handle.IsValid() || materials == nullptr)
            {
                return Rendering::BlendMode::Opaque;
            }

            const Rendering::MaterialResourceData *materialData = materials->GetData(handle);
            if (materialData == nullptr)
            {
                return Rendering::BlendMode::Opaque;
            }

            return materialData->Blend;
        }

        // ローカル空間のAABBを包む球をワールドへ移す。中心はワールド行列で点として変換し、半径は上3x3の絶対値で
        // 移した半分の大きさ（変換後の箱を包むワールドのAABB）の長さにする。回転と非一様スケールが重なっても包む。
        Rendering::BoundingSphere MakeWorldBoundingSphere(const Rendering::BoundingBox &localBounds,
                                                          const Math::Matrix4x4 &worldTransform)
        {
            const Math::Vector3 localCenter((localBounds.MinX + localBounds.MaxX) * 0.5f,
                                            (localBounds.MinY + localBounds.MaxY) * 0.5f,
                                            (localBounds.MinZ + localBounds.MaxZ) * 0.5f);
            const Math::Vector3 localHalfExtents((localBounds.MaxX - localBounds.MinX) * 0.5f,
                                                 (localBounds.MaxY - localBounds.MinY) * 0.5f,
                                                 (localBounds.MaxZ - localBounds.MinZ) * 0.5f);

            const Math::Vector3 worldCenter = Math::MatrixUtils::TransformPointRowVector(worldTransform, localCenter);
            const Math::Vector3 worldHalfExtents =
                Math::MatrixUtils::AbsUpper3x3TransformExtentsRowVector(worldTransform, localHalfExtents);

            Rendering::BoundingSphere worldBounds;
            worldBounds.CenterX = worldCenter.x;
            worldBounds.CenterY = worldCenter.y;
            worldBounds.CenterZ = worldCenter.z;
            worldBounds.Radius = std::sqrt(worldHalfExtents.x * worldHalfExtents.x +
                                           worldHalfExtents.y * worldHalfExtents.y +
                                           worldHalfExtents.z * worldHalfExtents.z);
            return worldBounds;
        }
    } // namespace

    bool ProceduralMeshSpec::SerializeToString(Container::String& outValue) const
    {
        Container::String result;
        const uint32_t kind = static_cast<uint32_t>(Kind);
        if (!AppendSpecNumber(result, kind) || !AppendSpecNumber(result, Size.x) ||
            !AppendSpecNumber(result, Size.y) || !AppendSpecNumber(result, Size.z) ||
            !AppendSpecNumber(result, Segments[0]) || !AppendSpecNumber(result, Segments[1]))
        {
            return false;
        }
        outValue = result;
        return true;
    }

    bool ProceduralMeshSpec::DeserializeFromString(const Container::String& inValue)
    {
        Container::String tokens[6];
        size_t position = 0;
        for (Container::String& token : tokens)
        {
            while (position < inValue.size() && IsSpecWhitespace(inValue[position]))
            {
                ++position;
            }
            const size_t start = position;
            while (position < inValue.size() && !IsSpecWhitespace(inValue[position]))
            {
                ++position;
            }
            if (start == position)
            {
                return false;
            }
            token = inValue.substr(start, position - start);
        }
        while (position < inValue.size() && IsSpecWhitespace(inValue[position]))
        {
            ++position;
        }
        if (position != inValue.size() || tokens[0][0] == static_cast<TCHAR>('-') ||
            tokens[4][0] == static_cast<TCHAR>('-') || tokens[5][0] == static_cast<TCHAR>('-'))
        {
            return false;
        }

        ProceduralMeshSpec parsed;
        uint32_t kind = 0;
        if (!Detail::DeserializeValue<uint32_t>(tokens[0], &kind) ||
            kind > static_cast<uint32_t>(ProceduralMeshKind::Plane) ||
            !Detail::DeserializeValue<float>(tokens[1], &parsed.Size.x) ||
            !Detail::DeserializeValue<float>(tokens[2], &parsed.Size.y) ||
            !Detail::DeserializeValue<float>(tokens[3], &parsed.Size.z) ||
            !Detail::DeserializeValue<uint32_t>(tokens[4], &parsed.Segments[0]) ||
            !Detail::DeserializeValue<uint32_t>(tokens[5], &parsed.Segments[1]))
        {
            return false;
        }
        parsed.Kind = static_cast<ProceduralMeshKind>(kind);
        if (!IsValidProceduralSpec(parsed))
        {
            return false;
        }
        *this = parsed;
        return true;
    }

    bool ProceduralMeshSpec::operator==(const ProceduralMeshSpec& other) const
    {
        return Kind == other.Kind && Size == other.Size &&
               Segments[0] == other.Segments[0] && Segments[1] == other.Segments[1];
    }

    MeshComponent::MeshComponent()
        : Component()
    {
        // PROPERTYマクロは値初期化するため、非ゼロデフォルト値を設定
        getbVisible().Set(true);
        getbCastShadow().Set(true);
        bSkyMapProxy = false;
        getbReceiveShadow().Set(true);
        getRenderLayerProp().Set(Rendering::RenderLayer::Default);
        ForcedLODLevel = -1;
    }

    MeshComponent::MeshComponent(const FieldInitializer *initializer)
        : Component(initializer)
    {
        getbVisible().Set(true);
        getbCastShadow().Set(true);
        bSkyMapProxy = false;
        getbReceiveShadow().Set(true);
        getRenderLayerProp().Set(Rendering::RenderLayer::Default);
        ForcedLODLevel = -1;
    }

    MeshComponent::MeshComponent(const IUnknown *sourceObject)
        : Component(sourceObject)
    {
        getbVisible().Set(true);
        getbCastShadow().Set(true);
        bSkyMapProxy = false;
        getbReceiveShadow().Set(true);
        getRenderLayerProp().Set(Rendering::RenderLayer::Default);
        ForcedLODLevel = -1;
    }

    MeshComponent::~MeshComponent()
    {
    }

    void MeshComponent::Initialize()
    {
        Component::Initialize();

        // トランスフォーム初期化
        m_WorldTransform = Math::Matrix4x4::Identity;
        m_PreviousWorldTransform = Math::Matrix4x4::Identity;

        m_bTransformDirty = true;
        m_bBoundsDirty = true;
    }

    void MeshComponent::Finalize()
    {
        ReleaseProceduralMesh();
        m_Materials.clear();
        Component::Finalize();
    }

    void MeshComponent::BeginPlay()
    {
        Component::BeginPlay();
        const ProceduralMeshSpec& spec = ProceduralMesh.Get();
        if ((m_bOwnsProceduralMesh && !(spec == m_AppliedProceduralMeshSpec)) ||
            (spec.Kind != ProceduralMeshKind::None && !LoadMeshHandle().IsValid()))
        {
            RefreshProceduralMesh();
        }
    }

    void MeshComponent::EndPlay()
    {
        Component::EndPlay();
    }

    // ========================================
    // メッシュ設定
    // ========================================

    void MeshComponent::SetMeshHandle(Rendering::MeshDataHandle handle)
    {
        const Rendering::MeshDataHandle oldHandle = LoadMeshHandle();
        const bool bReleaseOld = m_bOwnsProceduralMesh && oldHandle != handle;
        StoreMeshHandle(handle);
        MarkRenderStateDirty();
        if (bReleaseOld)
        {
            m_bOwnsProceduralMesh = false;
            if (Engine::GEngine)
            {
                Engine::GEngine->GetRenderResources().Meshes().Unregister(oldHandle);
            }
        }
        ProceduralMesh = ProceduralMeshSpec{};
        if (m_bOwnsProceduralMesh)
        {
            // 同じハンドルを渡された場合は使用中の形を壊さず、所有権だけ維持する。
            m_AppliedProceduralMeshSpec = ProceduralMesh.Get();
        }
    }

    void MeshComponent::SetProceduralMeshSpec(const ProceduralMeshSpec& spec)
    {
        ProceduralMesh = spec;
        if (bBegunPlay)
        {
            RefreshProceduralMesh();
        }
    }

    void MeshComponent::OnPropertyApplied(const ClassProperty& property)
    {
        Component::OnPropertyApplied(property);
        if (&property == s_PropertyPtr_ProceduralMesh && bBegunPlay)
        {
            RefreshProceduralMesh();
        }
    }

    Rendering::MeshDataHandle MeshComponent::LoadMeshHandle() const
    {
        Rendering::MeshDataHandle handle = m_MeshHandleFallback;
        (void)Ecs::EcsReadColumnPart(GetEcsRow(), Ecs::EcsColumnInfoOf<Ecs::EcsMeshRender>().Type,
                                     static_cast<uint32_t>(offsetof(Ecs::EcsMeshRender, MeshHandle)),
                                     static_cast<uint32_t>(sizeof(handle)), &handle);
        return handle;
    }

    void MeshComponent::StoreMeshHandle(Rendering::MeshDataHandle handle)
    {
        const Ecs::EcsRowRef row = GetEcsRow();
        const TypeId type = Ecs::EcsColumnInfoOf<Ecs::EcsMeshRender>().Type;
        if (Ecs::EcsRowHasColumn(row, type))
        {
            Ecs::EcsWriteColumnPart(row, type, static_cast<uint32_t>(offsetof(Ecs::EcsMeshRender, MeshHandle)),
                                    static_cast<uint32_t>(sizeof(handle)), &handle);
            return;
        }
        m_MeshHandleFallback = handle;
    }

    void MeshComponent::OnEcsRowAttached()
    {
        Component::OnEcsRowAttached();
        StoreMeshHandle(m_MeshHandleFallback);
    }

    void MeshComponent::OnEcsRowDetaching()
    {
        Component::OnEcsRowDetaching();
        m_MeshHandleFallback = LoadMeshHandle();
    }

    void MeshComponent::ReleaseProceduralMesh()
    {
        if (!m_bOwnsProceduralMesh)
        {
            return;
        }
        const Rendering::MeshDataHandle oldHandle = LoadMeshHandle();
        StoreMeshHandle(Rendering::MeshDataHandle::Invalid());
        MarkRenderStateDirty();
        m_bOwnsProceduralMesh = false;
        if (Engine::GEngine)
        {
            Engine::GEngine->GetRenderResources().Meshes().Unregister(oldHandle);
        }
    }

    void MeshComponent::RefreshProceduralMesh()
    {
        const ProceduralMeshSpec& spec = ProceduralMesh.Get();
        if (spec.Kind == ProceduralMeshKind::None)
        {
            ReleaseProceduralMesh();
            return;
        }
        if (m_bOwnsProceduralMesh && spec == m_AppliedProceduralMeshSpec)
        {
            return;
        }
        if (!CanGenerateProceduralSpec(spec))
        {
            NORVES_LOG_WARNING("MeshComponent", "手続きメッシュの指定が不正なため生成できません");
            return;
        }
        if (!Engine::GEngine)
        {
            NORVES_LOG_WARNING("MeshComponent", "エンジンがないため手続きメッシュを登録できません");
            return;
        }

        Container::VariableArray<Rendering::Mesh3DVertex> vertices;
        Container::VariableArray<uint32_t> indices;
        if (spec.Kind == ProceduralMeshKind::UVSphere)
        {
            Rendering::ProceduralMeshGenerator::GenerateUVSphere(spec.Size.x, spec.Segments[0], spec.Segments[1],
                                                                  vertices, indices);
        }
        else
        {
            Rendering::ProceduralMeshGenerator::GeneratePlane(spec.Size.x, spec.Size.z, spec.Segments[0],
                                                               spec.Segments[1], vertices, indices);
        }

        Rendering::MeshResources& meshes = Engine::GEngine->GetRenderResources().Meshes();
        Rendering::MeshDataHandle newHandle;
        do
        {
            newHandle.Id = g_NextProceduralMeshHandle.FetchAdd(1);
            if (newHandle.Id <= kProceduralMeshHandleBase || newHandle.Id > kProceduralMeshHandleEnd)
            {
                NORVES_LOG_WARNING("MeshComponent", "手続きメッシュ用の番号が不足しています");
                return;
            }
        } while (meshes.GetGPUData(newHandle) != nullptr);

        if (!meshes.Register(newHandle, vertices.data(), vertices.size() * sizeof(Rendering::Mesh3DVertex),
                             indices.data(), static_cast<uint32_t>(indices.size())))
        {
            NORVES_LOG_WARNING("MeshComponent", "手続きメッシュを登録できませんでした");
            return;
        }

        const Rendering::MeshDataHandle oldHandle = LoadMeshHandle();
        const bool bReleaseOld = m_bOwnsProceduralMesh;
        StoreMeshHandle(newHandle);
        m_bOwnsProceduralMesh = true;
        m_AppliedProceduralMeshSpec = spec;
        MarkRenderStateDirty();
        if (bReleaseOld)
        {
            meshes.Unregister(oldHandle);
        }
    }

    // ========================================
    // マテリアル設定
    // ========================================

    void MeshComponent::SetMaterial(uint32_t index, Rendering::MaterialHandle material)
    {
        if (index >= m_Materials.size())
        {
            m_Materials.resize(index + 1);
        }
        m_Materials[index] = material;
        MarkRenderStateDirty();
    }

    Rendering::MaterialHandle MeshComponent::GetMaterial(uint32_t index) const
    {
        if (index < m_Materials.size())
        {
            return m_Materials[index];
        }
        return Rendering::MaterialHandle{0};
    }

    void MeshComponent::ClearMaterials()
    {
        m_Materials.clear();
        MarkRenderStateDirty();
    }

    // ========================================
    // LOD設定
    // ========================================

    bool MeshComponent::SetLODChain(const Rendering::LODGroup& group)
    {
        if (group.Levels.empty() || group.Levels.size() > Rendering::MAX_MESH_LOD_LEVELS)
        {
            NORVES_LOG_WARNING("MeshComponent", "LODの段数は1～4段で指定してください");
            return false;
        }

        const uint32_t levelCount = static_cast<uint32_t>(group.Levels.size());
        for (uint32_t i = 0; i < levelCount; ++i)
        {
            const Rendering::LODLevel& level = group.Levels[i];
            if (i > 0 && !level.MeshHandle.IsValid())
            {
                NORVES_LOG_WARNING("MeshComponent", "LODの段1以降には有効なメッシュハンドルが必要です");
                return false;
            }
            if (!std::isfinite(level.ScreenSizeThreshold) || level.ScreenSizeThreshold < 0.0f)
            {
                NORVES_LOG_WARNING("MeshComponent", "LODのしきい値は有限かつ0以上で指定してください");
                return false;
            }
            if (i > 0 && group.Levels[i - 1].ScreenSizeThreshold <= level.ScreenSizeThreshold)
            {
                NORVES_LOG_WARNING("MeshComponent", "LODのしきい値は段の順に狭義に小さく指定してください");
                return false;
            }
        }

        if (group.Levels[0].MeshHandle.IsValid())
        {
            if (m_bOwnsProceduralMesh && group.Levels[0].MeshHandle != LoadMeshHandle())
            {
                SetMeshHandle(group.Levels[0].MeshHandle);
            }
            else
            {
                StoreMeshHandle(group.Levels[0].MeshHandle);
            }
        }
        if (levelCount == 1)
        {
            ClearLODChain();
            return true;
        }

        m_LODChainMeshes.fill(Rendering::MeshDataHandle::Invalid());
        m_LODScreenSizeThresholds.fill(0.0f);
        m_LODLevelCount = levelCount;
        for (uint32_t i = 0; i < levelCount; ++i)
        {
            m_LODScreenSizeThresholds[i] = group.Levels[i].ScreenSizeThreshold;
            if (i > 0)
            {
                m_LODChainMeshes[i - 1] = group.Levels[i].MeshHandle;
            }
        }
        MarkRenderStateDirty();
        return true;
    }

    void MeshComponent::ClearLODChain()
    {
        m_LODChainMeshes.fill(Rendering::MeshDataHandle::Invalid());
        m_LODScreenSizeThresholds.fill(0.0f);
        m_LODLevelCount = 1;
        MarkRenderStateDirty();
    }

    // ========================================
    // カスタムデータ
    // ========================================

    void MeshComponent::SetCustomData(uint32_t index, float value)
    {
        if (index < 4)
        {
            m_CustomData[index] = value;
            MarkRenderStateDirty();
        }
    }

    float MeshComponent::GetCustomData(uint32_t index) const
    {
        if (index < 4)
        {
            return m_CustomData[index];
        }
        return 0.0f;
    }

    void MeshComponent::SetShadingParams(uint32_t index, float value)
    {
        if (index >= Rendering::ShadingParamCount)
        {
            return;
        }
        const float sanitized = Rendering::SanitizeShadingParam(value);
        if (m_ShadingParams[index] != sanitized)
        {
            m_ShadingParams[index] = sanitized;
            MarkRenderStateDirty();
        }
    }

    float MeshComponent::GetShadingParams(uint32_t index) const
    {
        return index < Rendering::ShadingParamCount ? m_ShadingParams[index] : -1.0f;
    }

    void MeshComponent::SetDissolveThreshold(float threshold)
    {
        SetShadingParams(Rendering::ShadingParamP0, threshold);
    }

    float MeshComponent::GetDissolveThreshold() const
    {
        return GetShadingParams(Rendering::ShadingParamP0);
    }

    // ========================================
    // バウンディング
    // ========================================

    const Rendering::BoundingBox &MeshComponent::GetLocalBounds() const
    {
        static Rendering::BoundingBox defaultBounds = {-1.0f, -1.0f, -1.0f, 1.0f, 1.0f, 1.0f};
        return defaultBounds;
    }

    void MeshComponent::RefreshRenderTransformCache()
    {
        // 初期化から一度も求めていなければ先に 1 回求め、前フレームの変換を今の値に揃える
        // （初回の Tick で求めてから同期していた頃と同じ値になる）
        if (m_bTransformDirty)
        {
            UpdateWorldTransform();
        }
        UpdateWorldTransform();
    }

    void MeshComponent::CommitRenderTransformHistory()
    {
        m_PreviousWorldTransform = m_WorldTransform;
    }

    // ========================================
    // SceneProxy生成
    // ========================================

    void MeshComponent::CollectRenderResourceRefs(Container::VariableArray<Rendering::RenderResourceRef>& out) const
    {
        const Rendering::MeshDataHandle meshHandle = LoadMeshHandle();
        if (meshHandle.IsValid())
        {
            out.push_back(Rendering::RenderResourceRef{Rendering::RenderResourceKind::Mesh, meshHandle.Id});
        }
        for (uint32_t i = 1; i < m_LODLevelCount; ++i)
        {
            const Rendering::MeshDataHandle lodMeshHandle = m_LODChainMeshes[i - 1];
            if (lodMeshHandle.IsValid())
            {
                out.push_back(Rendering::RenderResourceRef{Rendering::RenderResourceKind::Mesh, lodMeshHandle.Id});
            }
        }
        for (const Rendering::MaterialHandle& material : m_Materials)
        {
            if (material.IsValid())
            {
                out.push_back(Rendering::RenderResourceRef{Rendering::RenderResourceKind::Material, material.Id});
            }
        }
    }

    bool MeshComponent::BuildMeshProxy(Rendering::MeshProxy &outProxy,
                                       const Rendering::MaterialResources* materials,
                                       const Rendering::MeshResources* meshes) const
    {
        // メッシュが無効なら生成しない
        const Rendering::MeshDataHandle meshHandle = LoadMeshHandle();
        if (!meshHandle.IsValid())
        {
            return false;
        }

        // 可視でなければ生成しない
        if (!IsVisible())
        {
            return false;
        }

        outProxy = Rendering::MeshProxy{};

        // Proxyを構築
        outProxy.ObjectId = GetOwnerId();
        outProxy.ComponentId = ComponentId;
        outProxy.MeshHandle = meshHandle;
        outProxy.LODLevel = 0;
        outProxy.LODLevelCount = static_cast<uint8_t>(m_LODLevelCount);
        outProxy.LODScreenSizeThresholds = m_LODScreenSizeThresholds;

        // トランスフォーム
        outProxy.WorldTransform = m_WorldTransform;
        outProxy.PreviousWorldTransform = m_PreviousWorldTransform;

        // バウンディング。登録したメッシュの頂点から求めたAABBがあれば、それを包む球にする（GetLocalBounds の
        // 既定の単位の箱のままだと、大きなメッシュは中心が視錐台の外に出たときに見えている部分ごとカリングされる）。
        outProxy.WorldBounds = m_WorldBounds;
        Rendering::BoundingBox meshLocalBounds;
        if (meshes != nullptr && meshes->TryGetLocalBounds(outProxy.MeshHandle, meshLocalBounds) &&
            meshLocalBounds.IsValid())
        {
            outProxy.WorldBounds = MakeWorldBoundingSphere(meshLocalBounds, m_WorldTransform);
        }

        // マテリアル
        const uint32_t materialCount = static_cast<uint32_t>(m_Materials.size());
        outProxy.MaterialCount = materialCount < Rendering::MAX_MATERIAL_SLOTS
                                     ? materialCount
                                     : Rendering::MAX_MATERIAL_SLOTS;
        for (uint32_t i = 0; i < outProxy.MaterialCount; ++i)
        {
            outProxy.Materials[i] = m_Materials[i];
            outProxy.MaterialBlendModes[i] = ResolveMaterialBlendMode(m_Materials[i], materials);
        }
        // マテリアル数が0の場合はデフォルトマテリアルを1つ入れる
        if (outProxy.MaterialCount == 0)
        {
            outProxy.MaterialCount = 1;
            outProxy.Materials[0] = Rendering::MaterialHandle::Invalid();
            outProxy.MaterialBlendModes[0] = Rendering::BlendMode::Opaque;
        }

        if (meshes != nullptr)
        {
            meshes->TryGetSubMeshRanges(outProxy.MeshHandle, outProxy.SubMeshes, outProxy.SubMeshCount);
        }

        for (uint32_t i = 1; i < m_LODLevelCount; ++i)
        {
            const Rendering::MeshDataHandle lodMeshHandle = m_LODChainMeshes[i - 1];
            if (meshes != nullptr && meshes->GetGPUData(lodMeshHandle) == nullptr)
            {
                // 未登録の段以降は、後の段が登録済みでも使わない。
                outProxy.LODLevelCount = static_cast<uint8_t>(i);
                break;
            }

            Rendering::MeshLODChainLevel& level = outProxy.LODChain[i - 1];
            level.MeshHandle = lodMeshHandle;
            if (meshes != nullptr)
            {
                meshes->TryGetSubMeshRanges(lodMeshHandle, level.SubMeshes, level.SubMeshCount);
            }
        }
        const int32_t maxLODLevel = static_cast<int32_t>(outProxy.LODLevelCount) - 1;
        const int32_t forcedLODLevel = ForcedLODLevel;
        outProxy.ForcedLODLevel = static_cast<int8_t>(
            forcedLODLevel < -1 ? -1 : (forcedLODLevel > maxLODLevel ? maxLODLevel : forcedLODLevel));

        // 描画フラグ
        outProxy.bVisible = getbVisible().Get();
        outProxy.bCastShadow = getbCastShadow().Get();
        outProxy.bSkyMapProxy = bSkyMapProxy.Get();
        outProxy.bReceiveShadow = getbReceiveShadow().Get();
        outProxy.LayerMask = getRenderLayerProp().Get();

        // カスタムデータ
        for (uint32_t i = 0; i < 4; ++i)
        {
            outProxy.CustomData[i] = m_CustomData[i];
            outProxy.ShadingParams[i] = m_ShadingParams[i];
        }

        return true;
    }

    // ========================================
    // 内部メソッド
    // ========================================

    void MeshComponent::UpdateWorldTransform()
    {
        // 前フレーム保存
        m_PreviousWorldTransform = m_WorldTransform;

        // オーナーからワールド行列を計算
        CalculateWorldMatrix(m_WorldTransform);

        m_bTransformDirty = false;
        m_bBoundsDirty = true;

        // バウンディング更新
        UpdateWorldBounds();
    }

    void MeshComponent::UpdateWorldBounds()
    {
        if (!m_bBoundsDirty)
        {
            return;
        }

        // ローカルバウンドからワールドバウンドを計算
        const auto &localBounds = GetLocalBounds();

        // センターとサイズを計算
        float centerX = (localBounds.MinX + localBounds.MaxX) * 0.5f;
        float centerY = (localBounds.MinY + localBounds.MaxY) * 0.5f;
        float centerZ = (localBounds.MinZ + localBounds.MaxZ) * 0.5f;

        float extentX = (localBounds.MaxX - localBounds.MinX) * 0.5f;
        float extentY = (localBounds.MaxY - localBounds.MinY) * 0.5f;
        float extentZ = (localBounds.MaxZ - localBounds.MinZ) * 0.5f;

        float radius = std::sqrt(extentX * extentX + extentY * extentY + extentZ * extentZ);

        // ワールド座標に変換
        Rendering::BoundingSphere localSphere;
        localSphere.CenterX = centerX;
        localSphere.CenterY = centerY;
        localSphere.CenterZ = centerZ;
        localSphere.Radius = radius;

        // MeshProxy::UpdateWorldBoundsと同様のロジック
        const Math::Vector3 translation = m_WorldTransform.GetTranslationRow();
        m_WorldBounds.CenterX = translation.x + localSphere.CenterX;
        m_WorldBounds.CenterY = translation.y + localSphere.CenterY;
        m_WorldBounds.CenterZ = translation.z + localSphere.CenterZ;

        // スケールの概算
        const Math::Vector3 scale = Math::MatrixUtils::ExtractScale(m_WorldTransform);
        float maxScale = scale.x > scale.y ? (scale.x > scale.z ? scale.x : scale.z)
                                           : (scale.y > scale.z ? scale.y : scale.z);

        m_WorldBounds.Radius = localSphere.Radius * maxScale;

        m_bBoundsDirty = false;
    }

    void MeshComponent::SyncMeshProxy()
    {
        // 現時点ではWorld::SyncToSceneViewで毎フレームBuildMeshProxyを呼ぶ形式
        // 将来的にはダーティフラグベースの差分更新に移行
    }

    void MeshComponent::CalculateWorldMatrix(Math::Matrix4x4 &outMatrix) const
    {
        const auto *owner = GetOwner();
        if (!owner)
        {
            outMatrix = Math::Matrix4x4::Identity;
            return;
        }

        const Math::Transform& worldTransform = owner->GetRenderWorldTransform();
        outMatrix = Math::MatrixUtils::CreateWorldRowVector(worldTransform.position,
                                                            worldTransform.rotation,
                                                            worldTransform.scale);
    }

} // namespace NorvesLib::Core::Component
