#include "GameModes/AssetPreview/AssetPreviewSkeletal.h"

// 読み込みの完了を指定順に集め、確認用の材質と姿勢を取り付ける。
// 確認用シーンは開発ビルドだけ（シーンの一覧と GameMode の登録と同じ条件）。
#if NORVES_BUILD_DEVELOPMENT

#include "Core/Private/Resource/SkinnedMaterialBuilder.h"
#include "Core/Public/Animation/SkeletalAssetRuntime.h"
#include "Core/Public/Animation/PoseTypes.h"
#include "Core/Public/Component/SkinnedMeshComponent.h"
#include "Core/Public/GameMode/GameModeContext.h"
#include "Core/Public/GameMode/GameModeScope.h"
#include "Core/Public/Logging/LogMacros.h"
#include "Core/Public/Math/MatrixUtils.h"
#include "Core/Public/Object/Entity.h"
#include "Core/Public/Object/World.h"

#include <charconv>
#include <cmath>
#include <utility>

namespace Game::GameModes
{
    namespace Core = NorvesLib::Core;
    namespace C = Core::Container;
    namespace Math = NorvesLib::Math;
    namespace Materials = Core::ResourceIO;

    namespace
    {
        const char* FailureName(Core::SkeletalRuntimeFailure failure)
        {
            switch (failure)
            {
            case Core::SkeletalRuntimeFailure::None: return "none";
            case Core::SkeletalRuntimeFailure::InvalidRequest: return "invalid_request";
            case Core::SkeletalRuntimeFailure::ResolveRejected: return "resolve_rejected";
            case Core::SkeletalRuntimeFailure::FormatRejected: return "format_rejected";
            case Core::SkeletalRuntimeFailure::ParseRejected: return "parse_rejected";
            case Core::SkeletalRuntimeFailure::MetadataMismatch: return "metadata_mismatch";
            case Core::SkeletalRuntimeFailure::InvalidCpu: return "invalid_cpu";
            case Core::SkeletalRuntimeFailure::WrongOwner: return "wrong_owner";
            case Core::SkeletalRuntimeFailure::RegistryNotReady: return "registry_not_ready";
            case Core::SkeletalRuntimeFailure::ResourceCreateFailed: return "resource_create_failed";
            case Core::SkeletalRuntimeFailure::ResourceLoadFailed: return "resource_load_failed";
            case Core::SkeletalRuntimeFailure::AssemblyInjectedFailure: return "assembly_injected_failure";
            case Core::SkeletalRuntimeFailure::AssemblyException: return "assembly_exception";
            case Core::SkeletalRuntimeFailure::WorkerException: return "worker_exception";
            case Core::SkeletalRuntimeFailure::OwnerException: return "owner_exception";
            case Core::SkeletalRuntimeFailure::PublicationInvalidRequest: return "publication_invalid_request";
            case Core::SkeletalRuntimeFailure::PublicationSessionChanged: return "publication_session_changed";
            case Core::SkeletalRuntimeFailure::PublicationInvalidCandidate: return "publication_invalid_candidate";
            case Core::SkeletalRuntimeFailure::PublicationKeyCollision: return "publication_key_collision";
            case Core::SkeletalRuntimeFailure::PublicationIdCollision: return "publication_id_collision";
            case Core::SkeletalRuntimeFailure::PublicationInvalidCache: return "publication_invalid_cache";
            case Core::SkeletalRuntimeFailure::PublicationBudget: return "publication_budget";
            case Core::SkeletalRuntimeFailure::PublicationInjectedFailure: return "publication_injected_failure";
            case Core::SkeletalRuntimeFailure::PublicationException: return "publication_exception";
            case Core::SkeletalRuntimeFailure::BindingRejected: return "binding_rejected";
            }
            return "unknown";
        }

        const char* SlotStatusName(Materials::SkinnedMaterialSlotStatus status)
        {
            switch (status)
            {
            case Materials::SkinnedMaterialSlotStatus::Created: return "created";
            case Materials::SkinnedMaterialSlotStatus::InvalidMaterial: return "invalid_material";
            case Materials::SkinnedMaterialSlotStatus::Unsupported: return "unsupported";
            case Materials::SkinnedMaterialSlotStatus::TextureFailed: return "texture_failed";
            case Materials::SkinnedMaterialSlotStatus::CreateFailed: return "create_failed";
            }
            return "unknown";
        }

        const char* BuildStatusName(Materials::SkinnedMaterialBuildStatus status)
        {
            switch (status)
            {
            case Materials::SkinnedMaterialBuildStatus::Success: return "success";
            case Materials::SkinnedMaterialBuildStatus::PartialFailure: return "partial_failure";
            case Materials::SkinnedMaterialBuildStatus::WrongThread: return "wrong_thread";
            case Materials::SkinnedMaterialBuildStatus::InvalidAsset: return "invalid_asset";
            case Materials::SkinnedMaterialBuildStatus::NotSplitMesh: return "not_split_mesh";
            case Materials::SkinnedMaterialBuildStatus::NotLoaded: return "not_loaded";
            }
            return "unknown";
        }

        Math::Matrix4x4 MeshTransform(const Core::SkinnedMeshResource& mesh)
        {
            const auto& v = mesh.GetMeshNodeGlobalTransform();
            return Math::Matrix4x4(v[0], v[1], v[2], v[3], v[4], v[5], v[6], v[7],
                                   v[8], v[9], v[10], v[11], v[12], v[13], v[14], v[15]);
        }

        bool IsFinite(const Math::Vector3& point)
        {
            return std::isfinite(point.x) && std::isfinite(point.y) && std::isfinite(point.z);
        }

        bool IsFinite(const Math::Matrix4x4& matrix)
        {
            for (int row = 0; row < 4; ++row)
            {
                const auto value = matrix.GetRow(row);
                if (!std::isfinite(value.x) || !std::isfinite(value.y) ||
                    !std::isfinite(value.z) || !std::isfinite(value.w))
                {
                    return false;
                }
            }
            return true;
        }

        Math::AABB ComputeBindBounds(const Core::SkeletalAssetResource& asset)
        {
            auto bounds = Math::AABB::CreateInvalid();
            const auto& mesh = asset.GetMesh();
            if (!mesh || mesh->GetVertices().empty())
            {
                return bounds;
            }
            const auto transform = MeshTransform(*mesh);
            if (!IsFinite(transform))
            {
                return bounds;
            }
            for (const auto& vertex : mesh->GetVertices())
            {
                const Math::Vector3 position(vertex.Position.X, vertex.Position.Y, vertex.Position.Z);
                const auto point = Math::MatrixUtils::TransformPointRowVector(transform, position);
                if (!IsFinite(position) || !IsFinite(point))
                {
                    return Math::AABB::CreateInvalid();
                }
                bounds.Expand(point);
            }
            return bounds;
        }

        bool BuildBindLocalPose(const Core::SkeletalAssetResource& asset, Core::Animation::LocalPose& out)
        {
            const auto& skeleton = asset.GetSkeleton();
            const auto& mesh = asset.GetMesh();
            if (!skeleton || !mesh)
            {
                return false;
            }
            const auto& runtime = skeleton->GetPoseRuntime();
            const auto count = runtime.Parents.size();
            if (!runtime.bValid || count == 0)
            {
                return false;
            }
            if (runtime.bSplit)
            {
                // split のメッシュは対応する rest の骨格だけと組み合わせられる。
                out = runtime.AuthorRestLocal;
                return out.size() == count;
            }
            if (runtime.LegacyBindModel.size() != count)
            {
                return false;
            }
            const auto transform = MeshTransform(*mesh);
            C::VariableArray<Math::Matrix4x4> globals(count);
            for (size_t i = 0; i < count; ++i)
            {
                globals[i] = runtime.LegacyBindModel[i] * transform;
                if (!IsFinite(globals[i]))
                {
                    return false;
                }
            }
            out.resize(count);
            for (size_t i = 0; i < count; ++i)
            {
                auto local = globals[i];
                const auto parent = runtime.Parents[i];
                if (parent >= 0)
                {
                    if (static_cast<size_t>(parent) >= count)
                    {
                        return false;
                    }
                    const float determinant = Math::MatrixUtils::Determinant(globals[parent]);
                    if (!std::isfinite(determinant) || std::abs(determinant) < Math::Constants::EPSILON)
                    {
                        return false;
                    }
                    local = local * Math::MatrixUtils::Inverse(globals[parent]);
                }
                if (!IsFinite(local))
                {
                    return false;
                }
                out[i] = Core::Animation::FromRowMatrix(local);
            }
            return true;
        }

        void SetBindPose(Core::Component::SkinnedMeshComponent& component, const AssetPreviewSkeletalResult& result)
        {
            component.SetPlaying(false);
            component.SetAnimationTimeSeconds(0.0f);
            Core::Animation::LocalPose pose;
            if (BuildBindLocalPose(*result.Asset, pose))
            {
                component.SetExternalAnimationDriven(true);
                if (component.SubmitLocalPose(pose))
                {
                    return;
                }
            }
            // 外部姿勢の準備にも有効なクリップが必要。失敗時は先頭別名の時刻 0 に戻す。
            component.SetExternalAnimationDriven(false);
            LOG_WARNING_F("ASSET_PREVIEW_BIND_POSE_UNAVAILABLE path=%s bind の姿勢を設定できないため先頭のクリップの時刻 0 で止めます（有効なクリップが無い場合は描画できません）",
                          result.Path.c_str());
        }

        void SetMaterials(Core::Component::SkinnedMeshComponent& component,
                          Materials::SkinnedMaterialBuilder& builder, const AssetPreviewSkeletalResult& result,
                          Core::Rendering::MaterialHandle fallbackMaterial)
        {
            component.SetMaterial(fallbackMaterial);
            const auto built = builder.Build(result.Asset);
            if (!built)
            {
                LOG_WARNING_F("ASSET_PREVIEW_MATERIAL path=%s status=invalid_asset 材質の結果が無いため既定の材質で描きます",
                              result.Path.c_str());
                return;
            }
            if (built->Status == Materials::SkinnedMaterialBuildStatus::NotSplitMesh)
            {
                LOG_INFO_F("ASSET_PREVIEW_MATERIAL path=%s status=not_split_mesh 旧経路の資産なので既定の材質で描きます",
                           result.Path.c_str());
                return;
            }
            if (built->Status != Materials::SkinnedMaterialBuildStatus::Success &&
                built->Status != Materials::SkinnedMaterialBuildStatus::PartialFailure)
            {
                LOG_WARNING_F("ASSET_PREVIEW_MATERIAL path=%s status=%s 既定の材質で描きます",
                              result.Path.c_str(), BuildStatusName(built->Status));
                return;
            }
            for (const auto& slot : built->Slots)
            {
                if (slot.Status != Materials::SkinnedMaterialSlotStatus::Created)
                {
                    LOG_WARNING_F("ASSET_PREVIEW_MATERIAL path=%s slot=%u name=%s status=%s reason=%s",
                                  result.Path.c_str(), slot.Slot, slot.Name.c_str(),
                                  SlotStatusName(slot.Status), slot.Reason.c_str());
                }
            }
            const auto bound = builder.Bind(component, *built);
            uint32_t rejected = 0;
            uint32_t notCreated = 0;
            for (const auto status : bound.Slots)
            {
                if (status == Materials::SkinnedMaterialBindStatus::Rejected)
                {
                    ++rejected;
                }
                else if (status == Materials::SkinnedMaterialBindStatus::NotCreated)
                {
                    ++notCreated;
                }
            }
            if (!bound.bAccepted || rejected != 0 || notCreated != 0)
            {
                LOG_WARNING_F("ASSET_PREVIEW_MATERIAL path=%s rejected=%u not_created=%u 材質を付けられない slot は既定の材質で描きます",
                              result.Path.c_str(), rejected, notCreated);
            }
        }
    } // namespace

    struct AssetPreviewSkeletalLoads::State
    {
        struct Request
        {
            uint64_t Id = 0;
            bool bPending = false;
        };
        Core::SkeletalAssetRuntime* Runtime = nullptr;
        C::VariableArray<AssetPreviewSkeletalResult> Results;
        C::VariableArray<Request> Requests;
        C::String ClipName;
        size_t Remaining = 0;
        bool bAlive = true;
    };

    AssetPreviewSkeletalLoads::~AssetPreviewSkeletalLoads()
    {
        Cancel();
    }

    void AssetPreviewSkeletalLoads::Start(Core::SkeletalAssetRuntime& runtime,
                                          const C::VariableArray<C::String>& paths, const C::String& clipName)
    {
        Cancel();
        auto state = C::MakeShared<State>();
        state->Results.resize(paths.size());
        state->Requests.resize(paths.size());
        state->ClipName = clipName;
        state->Remaining = paths.size();
        state->Runtime = &runtime;
        m_State = state;
        const C::TWeakPtr<State> weak = state;
        for (size_t i = 0; i < paths.size(); ++i)
        {
            state->Results[i].Path = paths[i];
            state->Requests[i].bPending = true;
            Core::SkeletalAdmissionResult admission;
            try
            {
                admission = runtime.LoadAsync(C::AnsiStringView(paths[i].c_str(), paths[i].size()),
                    [weak, i](const Core::SkeletalAssetCompletion& completion)
                    {
                        auto current = weak.lock();
                        if (!current || !current->bAlive || !current->Requests[i].bPending ||
                            current->Requests[i].Id != completion.RequestId)
                        {
                            return;
                        }
                        auto& result = current->Results[i];
                        if (completion.Failure != Core::SkeletalRuntimeFailure::None)
                        {
                            result.MissingReason = C::String("skeletal_") + FailureName(completion.Failure);
                        }
                        else if (!completion.Asset)
                        {
                            result.MissingReason = "skeletal_resource_load_failed";
                        }
                        else
                        {
                            result.Asset = completion.Asset;
                            result.BindBounds = ComputeBindBounds(*result.Asset);
                            if (!current->ClipName.empty())
                            {
                                const auto clip = result.Asset->GetClip(C::StringView(current->ClipName));
                                if (clip && clip->IsLoaded() && clip->IsValid())
                                {
                                    result.Clip = clip;
                                }
                                else
                                {
                                    result.MissingReason = C::String("clip_not_found clip=") + current->ClipName;
                                }
                            }
                        }
                        current->Requests[i].bPending = false;
                        --current->Remaining;
                    });
            }
            catch (...)
            {
                admission = {Core::SkeletalRuntimeStatus::PreparationException, 0};
            }
            state->Requests[i].Id = admission.RequestId;
            if (admission.Status != Core::SkeletalRuntimeStatus::Accepted)
            {
                char number[4] = {};
                const auto converted = std::to_chars(number, number + 3, static_cast<unsigned>(admission.Status));
                *converted.ptr = '\0';
                state->Results[i].MissingReason = C::String("admission_rejected_") + number;
                state->Requests[i].bPending = false;
                --state->Remaining;
            }
        }
    }

    void AssetPreviewSkeletalLoads::FailAll(const C::VariableArray<C::String>& paths, const char* reason)
    {
        Cancel();
        auto state = C::MakeShared<State>();
        state->Results.resize(paths.size());
        for (size_t i = 0; i < paths.size(); ++i)
        {
            state->Results[i].Path = paths[i];
            state->Results[i].MissingReason = reason ? reason : "skeletal_runtime_unavailable";
        }
        m_State = state;
    }

    void AssetPreviewSkeletalLoads::Cancel()
    {
        auto state = std::move(m_State);
        if (!state)
        {
            return;
        }
        state->bAlive = false;
        auto* runtime = state->Runtime;
        state->Runtime = nullptr;
        for (auto& request : state->Requests)
        {
            if (request.bPending && runtime && request.Id != 0)
            {
                request.bPending = false;
                (void)runtime->Cancel(request.Id);
            }
        }
    }

    bool AssetPreviewSkeletalLoads::IsComplete() const
    {
        return !m_State || m_State->Remaining == 0;
    }

    const C::VariableArray<AssetPreviewSkeletalResult>& AssetPreviewSkeletalLoads::GetResults() const
    {
        return m_State ? m_State->Results : m_EmptyResults;
    }

    Core::Component::SkinnedMeshComponent* AttachAssetPreviewSkeletal(
        Core::GameMode::GameModeContext& ctx, Materials::SkinnedMaterialBuilder& builder,
        const AssetPreviewSkeletalResult& result, Core::Rendering::MaterialHandle fallbackMaterial,
        const Math::Vector3& position, float timeSeconds)
    {
        if (!result.Asset)
        {
            return nullptr;
        }
        auto* entity = ctx.ScopeRef.SpawnObject<Core::Entity>();
        if (!entity)
        {
            LOG_ERROR_F("ASSET_PREVIEW_SKELETAL path=%s Entity を生成できませんでした", result.Path.c_str());
            return nullptr;
        }
        entity->SetPosition(position);
        auto* component = ctx.WorldRef.CreateComponent<Core::Component::SkinnedMeshComponent>(entity);
        if (!component)
        {
            LOG_ERROR_F("ASSET_PREVIEW_SKELETAL path=%s Component を生成できませんでした", result.Path.c_str());
            return nullptr;
        }
        component->SetSkeletalAsset(result.Asset);
        component->SetCastShadow(true);
        SetMaterials(*component, builder, result, fallbackMaterial);
        if (result.Clip && component->SetAnimationClip(result.Clip))
        {
            component->SetLooping(true);
            component->SetPlaying(timeSeconds < 0.0f);
            component->SetAnimationTimeSeconds(timeSeconds >= 0.0f ? timeSeconds : 0.0f);
        }
        else
        {
            if (result.Clip)
            {
                LOG_WARNING_F("ASSET_PREVIEW_SKELETAL path=%s クリップを設定できなかったため bind の姿勢にします",
                              result.Path.c_str());
            }
            SetBindPose(*component, result);
        }
        return component;
    }
} // namespace Game::GameModes

#endif
