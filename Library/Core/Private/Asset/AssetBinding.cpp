#include "Asset/AssetBinding.h"

#include "Animation/SkeletalAssetRuntime.h"
#include "Engine/Engine.h"
#include "Engine/NorvesEngine.h"
#include "Logging/LogMacros.h"
#include "Rendering/RenderResources.h"

namespace NorvesLib::Core::Asset
{
    namespace
    {
        uint32_t DefaultLoadModel(const Container::String& path, AssetBindingLoaders::ModelCallback callback)
        {
            if (!Engine::GEngine)
            {
                return 0;
            }
            return Engine::GEngine->GetRenderResources().MegaGeometry().LoadModelAsync(path, callback);
        }

        void DefaultReleaseModel(Rendering::ModelHandle model)
        {
            if (Engine::GEngine && model.IsValid())
            {
                Engine::GEngine->GetRenderResources().MegaGeometry().ReleaseModel(model);
            }
        }

        Rendering::MegaGeometry::MegaMeshHandle DefaultGetModelMegaMesh(Rendering::ModelHandle model)
        {
            if (!Engine::GEngine || !model.IsValid())
            {
                return Rendering::MegaGeometry::MegaMeshHandle::Invalid();
            }
            return Engine::GEngine->GetRenderResources().MegaGeometry().GetModelMegaMeshHandle(model);
        }

        uint32_t DefaultLoadTexture(const Container::String& path, AssetBindingLoaders::TextureCallback callback)
        {
            if (!Engine::GEngine)
            {
                return 0;
            }
            return Engine::GEngine->GetRenderResources().Textures().LoadTextureAsync(path, callback);
        }

        uint64_t DefaultLoadSkeletal(const Container::String& path, AssetBindingLoaders::SkeletalCallback callback)
        {
            auto* runtime = NorvesLib::Core::GEngine.GetSkeletalAssetSession().GetRuntime();
            if (!runtime)
            {
                return 0;
            }
            return runtime->LoadAsync(Container::AnsiStringView(path.data(), path.size()), callback).RequestId;
        }

        AssetBindingLoaders g_Loaders{
            DefaultLoadModel, DefaultReleaseModel, DefaultGetModelMegaMesh, DefaultLoadTexture, DefaultLoadSkeletal};

        void FailRequest(AssetBindingState& state)
        {
            // 同期通知が先に完了していれば、その結果を拒否扱いで上書きしない。
            if (static_cast<AssetBindingStatus>(state.Status.Load(std::memory_order_acquire)) !=
                AssetBindingStatus::Pending)
            {
                return;
            }
            state.Status.Store(static_cast<uint8_t>(AssetBindingStatus::Failed), std::memory_order_release);
            NORVES_LOG_WARNING("AssetBinding", "ASSET_BINDING_FAILED kind=%s path=%s",
                               GetAssetKindName(state.Kind).c_str(), state.LogicalPath.c_str());
        }

        void PublishResult(AssetBindingState& state, bool bValid)
        {
            if (!bValid)
            {
                FailRequest(state);
                return;
            }
            state.Status.Store(static_cast<uint8_t>(AssetBindingStatus::Ready), std::memory_order_release);
        }
    }

    AssetBindingState::~AssetBindingState()
    {
        if (static_cast<AssetBindingStatus>(Status.Load(std::memory_order_acquire)) == AssetBindingStatus::Ready &&
            ReleaseModel && Model.IsValid())
        {
            ReleaseModel(Model);
        }
    }

    void SetAssetBindingLoaders(const AssetBindingLoaders* loaders)
    {
        const AssetBindingLoaders replacement = loaders ? *loaders : AssetBindingLoaders{};
        g_Loaders.LoadModel = replacement.LoadModel ? replacement.LoadModel : DefaultLoadModel;
        g_Loaders.ReleaseModel = replacement.ReleaseModel ? replacement.ReleaseModel : DefaultReleaseModel;
        g_Loaders.GetModelMegaMesh = replacement.GetModelMegaMesh ? replacement.GetModelMegaMesh : DefaultGetModelMegaMesh;
        g_Loaders.LoadTexture = replacement.LoadTexture ? replacement.LoadTexture : DefaultLoadTexture;
        g_Loaders.LoadSkeletal = replacement.LoadSkeletal ? replacement.LoadSkeletal : DefaultLoadSkeletal;
    }

    AssetBinding::~AssetBinding()
    {
        Reset();
    }

    bool AssetBinding::BeginRequest(AssetKind kind, Container::StringView path)
    {
        if (path.empty())
        {
            Reset();
            return false;
        }
        const auto status = GetStatus();
        if (Matches(kind, path) && (status == AssetBindingStatus::Pending || status == AssetBindingStatus::Ready))
        {
            return false;
        }
        Reset();
        m_State = Container::MakeShared<AssetBindingState>();
        m_State->Kind = kind;
        m_State->LogicalPath = Container::String(path);
        m_State->Status.Store(static_cast<uint8_t>(AssetBindingStatus::Pending), std::memory_order_release);
        return true;
    }

    void AssetBinding::RequestModel(const ModelAssetRef& reference)
    {
        if (!BeginRequest(AssetKind::Model, reference.GetLogicalPath()))
        {
            return;
        }
        m_State->ReleaseModel = g_Loaders.ReleaseModel;
        const Container::TWeakPtr<AssetBindingState> weakState = m_State;
        const auto requestId = g_Loaders.LoadModel(m_State->LogicalPath, [weakState](Rendering::ModelHandle model)
        {
            const auto state = weakState.lock();
            if (!Container::IsValid(state))
            {
                if (model.IsValid())
                {
                    g_Loaders.ReleaseModel(model);
                }
                return;
            }
            state->Model = model;
            PublishResult(*state, model.IsValid());
        });
        if (requestId == 0)
        {
            FailRequest(*m_State);
        }
    }

    void AssetBinding::RequestTexture(const TextureAssetRef& reference)
    {
        if (!BeginRequest(AssetKind::Texture, reference.GetLogicalPath()))
        {
            return;
        }
        const Container::TWeakPtr<AssetBindingState> weakState = m_State;
        const auto requestId = g_Loaders.LoadTexture(m_State->LogicalPath, [weakState](Rendering::TextureHandle texture)
        {
            const auto state = weakState.lock();
            if (!Container::IsValid(state))
            {
                return;
            }
            state->Texture = texture;
            PublishResult(*state, texture.IsValid());
        });
        if (requestId == 0)
        {
            FailRequest(*m_State);
        }
    }

    void AssetBinding::RequestSkeletal(const SkeletalAssetRef& reference)
    {
        if (!BeginRequest(AssetKind::Skeleton, reference.GetLogicalPath()))
        {
            return;
        }
        const Container::TWeakPtr<AssetBindingState> weakState = m_State;
        const auto requestId = g_Loaders.LoadSkeletal(m_State->LogicalPath, [weakState](const SkeletalAssetCompletion& completion)
        {
            const auto state = weakState.lock();
            if (!Container::IsValid(state))
            {
                return;
            }
            state->Skeletal = completion.Asset;
            PublishResult(*state, Container::IsValid(completion.Asset));
        });
        if (requestId == 0)
        {
            FailRequest(*m_State);
        }
    }

    void AssetBinding::MarkExternal(AssetKind kind, Container::StringView logicalPath)
    {
        // 引数のビューが現在の状態を指していても、Reset後までパスを保つ。
        Container::String path(logicalPath);
        Reset();
        m_State = Container::MakeShared<AssetBindingState>();
        m_State->Kind = kind;
        m_State->LogicalPath = path;
        m_bExternal = true;
        m_State->Status.Store(static_cast<uint8_t>(AssetBindingStatus::Ready), std::memory_order_release);
    }

    bool AssetBinding::CanTake(AssetKind kind) const
    {
        return GetStatus() == AssetBindingStatus::Ready && m_State->Kind == kind && !m_bExternal && !m_bTaken;
    }

    bool AssetBinding::TakeReadyModel(Rendering::ModelHandle& outModel)
    {
        if (!CanTake(AssetKind::Model))
        {
            return false;
        }
        outModel = m_State->Model;
        m_bTaken = true;
        return true;
    }

    bool AssetBinding::TakeReadyTexture(Rendering::TextureHandle& outTexture)
    {
        if (!CanTake(AssetKind::Texture))
        {
            return false;
        }
        outTexture = m_State->Texture;
        m_bTaken = true;
        return true;
    }

    bool AssetBinding::TakeReadySkeletal(Container::TSharedPtr<SkeletalAssetResource>& outSkeletal)
    {
        if (!CanTake(AssetKind::Skeleton))
        {
            return false;
        }
        outSkeletal = m_State->Skeletal;
        m_bTaken = true;
        return true;
    }

    void AssetBinding::Reset()
    {
        m_State.reset();
        m_bTaken = false;
        m_bExternal = false;
    }

    AssetBindingStatus AssetBinding::GetStatus() const
    {
        return Container::IsValid(m_State)
            ? static_cast<AssetBindingStatus>(m_State->Status.Load(std::memory_order_acquire))
            : AssetBindingStatus::Unresolved;
    }

    bool AssetBinding::IsPending() const
    {
        return GetStatus() == AssetBindingStatus::Pending;
    }

    bool AssetBinding::IsWaiting() const
    {
        const auto status = GetStatus();
        return !m_bTaken && (status == AssetBindingStatus::Pending || status == AssetBindingStatus::Failed);
    }

    bool AssetBinding::Matches(AssetKind kind, Container::StringView path) const
    {
        return Container::IsValid(m_State) && m_State->Kind == kind && Container::StringView(m_State->LogicalPath) == path;
    }

    Container::StringView AssetBinding::GetLogicalPath() const
    {
        return Container::IsValid(m_State) ? Container::StringView(m_State->LogicalPath) : Container::StringView{};
    }

    Rendering::MegaGeometry::MegaMeshHandle AssetBinding::ResolveModelMegaMesh(Rendering::ModelHandle model) const
    {
        return g_Loaders.GetModelMegaMesh(model);
    }
} // namespace NorvesLib::Core::Asset
