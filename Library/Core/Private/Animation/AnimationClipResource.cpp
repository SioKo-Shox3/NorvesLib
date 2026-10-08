#include "Animation/AnimationClipResource.h"
#include "Animation/RigBoundClipProof.h"
#include "Animation/SkeletalPoseRuntimeBuild.h"

#include "Text/JsonDocument.h"
#include <filesystem>
#include <fstream>
#include <utility>

namespace NorvesLib::Core
{
    IMPLEMENT_CLASS(AnimationClipResource, Resource)

    AnimationClipResource::AnimationClipResource() = default;

    AnimationClipResource::AnimationClipResource(const FieldInitializer* initializer)
        : Resource(initializer)
    {
    }

    AnimationClipResource::AnimationClipResource(const IUnknown* sourceObject)
        : Resource(sourceObject)
    {
    }

    AnimationClipResource::~AnimationClipResource()
    {
        Finalize();
    }

    void AnimationClipResource::Initialize()
    {
        Resource::Initialize();
    }

    void AnimationClipResource::Finalize()
    {
        Unload();
        Resource::Finalize();
    }

    bool AnimationClipResource::Load()
    {
        SetResourceState(ResourceState::Loaded);
        return true;
    }

    void AnimationClipResource::Unload()
    {
        ++m_PoseRevision;
        ClearRuntimeMetadata();
        m_PoseRuntime = {};
        m_BoundRigProof.reset();
        m_Clip = {};
        SetResourceState(ResourceState::Unloaded);
    }

    size_t AnimationClipResource::GetMemorySize() const
    {
        size_t size =
            Skeletal::RigBoundClipAccess::MemorySize(*this) + sizeof(AnimationClipResource) + m_Clip.Name.size();
        size += m_PoseRuntime.AllocatedBytes() + m_Clip.Metadata.AllocatedBytes();
        if (m_RuntimeMetadata)
            size += sizeof(Animation::ClipMetadata) + m_RuntimeMetadata->AllocatedBytes();
        size += m_Clip.Channels.size() * sizeof(Skeletal::SkeletalAnimationChannel);
        size += m_Clip.RootMotion.size() * sizeof(Skeletal::SkeletalRootMotionSample);
        for (const Skeletal::SkeletalAnimationChannel& channel : m_Clip.Channels)
        {
            size += channel.Samples.size() * sizeof(Skeletal::SkeletalAnimationSample);
        }
        return size;
    }

    void AnimationClipResource::SetClip(Skeletal::SkeletalAnimationClip&& clip)
    {
        ++m_PoseRevision;
        ClearRuntimeMetadata();
        m_PoseRuntime = {};
        m_BoundRigProof.reset();
        m_Clip = std::move(clip);
        Animation::Detail::BuildClipPoseRuntime(m_Clip, m_PoseRuntime);
    }

    const Animation::ClipMetadata& AnimationClipResource::GetClipMetadata() const noexcept
    {
        return m_RuntimeMetadata ? *m_RuntimeMetadata : m_Clip.Metadata;
    }
    bool AnimationClipResource::ApplyMetadataJson(const Container::String& json, Animation::ClipMetadataReport& report)
    {
        Animation::ClipMetadata candidate;
        if (!Animation::ApplyClipMetadataOverride(m_Clip.Metadata, json, m_Clip.DurationSeconds, candidate, report))
            return false;
        m_RuntimeMetadata = Container::MakeShared<Animation::ClipMetadata>(std::move(candidate));
        ++m_MetadataRevision;
        return true;
    }
    bool AnimationClipResource::ApplyMetadataFile(const Container::String& path, Animation::ClipMetadataReport& report)
    {
        report = {};
        auto fail = [&]() {
            report.Error = Animation::ClipMetadataError::FileReadFailed;
            report.Detail = _T("animmeta_file");
            return false;
        };
        try
        {
            const std::filesystem::path filePath(path.c_str());
            std::error_code error;
            if (!std::filesystem::is_regular_file(filePath, error) || error)
                return fail();
            std::ifstream file(filePath, std::ios::binary | std::ios::ate);
            if (!file)
                return fail();
            const auto length = file.tellg();
            if (length <= 0 || length > 1048576)
                return fail();
            Container::VariableArray<uint8_t> bytes(size_t(length), 0);
            file.seekg(0);
            if (!file.read(reinterpret_cast<char*>(bytes.data()), std::streamsize(bytes.size())) ||
                file.peek() != std::char_traits<char>::eof())
                return fail();
            size_t skip = bytes.size() >= 3 && bytes[0] == 0xef && bytes[1] == 0xbb && bytes[2] == 0xbf ? 3 : 0;
            JsonDocument document;
            if (!JsonDocument::TryParseUtf8({bytes.data() + skip, bytes.size() - skip}, document))
            {
                report.Error = Animation::ClipMetadataError::InvalidJson;
                report.Detail = _T("animmeta_utf8_json");
                return false;
            }
            Animation::ClipMetadata candidate;
            if (!Animation::ApplyClipMetadataValueOverride(m_Clip.Metadata, document.GetRoot(), m_Clip.DurationSeconds,
                                                           candidate, report))
                return false;
            m_RuntimeMetadata = Container::MakeShared<Animation::ClipMetadata>(std::move(candidate));
            ++m_MetadataRevision;
            return true;
        }
        catch (...)
        {
            return fail();
        }
    }

    void AnimationClipResource::ClearRuntimeMetadata()
    {
        m_RuntimeMetadata.reset();
        ++m_MetadataRevision;
    }

    const Skeletal::SkeletalAnimationClip& AnimationClipResource::GetClip() const
    {
        return m_Clip;
    }
} // namespace NorvesLib::Core
