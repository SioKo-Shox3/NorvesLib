#pragma once

#include "Asset/AssetManifest.h"
#include "Asset/AssetPath.h"
#include "Object/RuntimeSchema.h"
#include "Text/IdentityPool.h"
#include <type_traits>

namespace NorvesLib::Core
{
    /// 正規化した論理パスで資産を参照する値。
    template <Asset::AssetKind K>
    class TAssetRef
    {
        static_assert(K == Asset::AssetKind::Unknown || K == Asset::AssetKind::Texture ||
                      K == Asset::AssetKind::Model || K == Asset::AssetKind::Raw ||
                      K == Asset::AssetKind::Audio || K == Asset::AssetKind::Skeleton ||
                      K == Asset::AssetKind::Animation || K == Asset::AssetKind::World,
                      "未定義の資産種別は参照できません");

    public:
        static constexpr TypeKind SchemaTypeKind = TypeKind::String;
        static constexpr const char* SchemaTypeName =
            K == Asset::AssetKind::Texture ? "TextureAssetRef" :
            K == Asset::AssetKind::Model ? "ModelAssetRef" :
            K == Asset::AssetKind::Raw ? "RawAssetRef" :
            K == Asset::AssetKind::Audio ? "AudioAssetRef" :
            K == Asset::AssetKind::Skeleton ? "SkeletalAssetRef" :
            K == Asset::AssetKind::Animation ? "AnimationAssetRef" :
            K == Asset::AssetKind::World ? "WorldAssetRef" : "UnknownAssetRef";

        /// 空文字は参照を外し、それ以外は論理パスとして検証する。不正な入力では値を変更しない。
        bool TrySet(Container::StringView path)
        {
            if (path.empty())
            {
                m_Path = Identity{};
                return true;
            }

            size_t segmentStart = 0;
            for (size_t i = 0; i <= path.size(); ++i)
            {
                if (i < path.size() && path[i] == '\0')
                {
                    return false;
                }
                if (i == path.size() || path[i] == '/' || path[i] == '\\')
                {
                    if (i - segmentStart == 2 && path[segmentStart] == '.' && path[segmentStart + 1] == '.')
                    {
                        return false;
                    }
                    segmentStart = i + 1;
                }
            }

            const Asset::AssetPath normalized = Asset::AssetPath::Normalize(
                Container::AnsiStringView(path.data(), path.size()));
            if (!normalized.IsValid() || normalized.IsAbsolute() || !normalized.HasLogicalPath())
            {
                return false;
            }

            m_Path = IdentityPool::Get().CreateIdentity(Container::StringView(normalized.GetLogicalPath()));
            return true;
        }

        [[nodiscard]] Container::StringView GetLogicalPath() const noexcept { return m_Path.GetView(); }
        [[nodiscard]] bool IsEmpty() const noexcept { return !m_Path.IsValid(); }
        [[nodiscard]] constexpr Asset::AssetKind GetKind() const noexcept { return K; }
        [[nodiscard]] bool operator==(const TAssetRef& other) const noexcept { return m_Path == other.m_Path; }

        bool SerializeToString(Container::String& out) const
        {
            out = Container::String(GetLogicalPath());
            return true;
        }

        bool DeserializeFromString(const Container::String& in)
        {
            return TrySet(Container::StringView(in));
        }

    private:
        Identity m_Path;
    };

    using ModelAssetRef = TAssetRef<Asset::AssetKind::Model>;
    using TextureAssetRef = TAssetRef<Asset::AssetKind::Texture>;
    using SkeletalAssetRef = TAssetRef<Asset::AssetKind::Skeleton>;
    using AnimationAssetRef = TAssetRef<Asset::AssetKind::Animation>;
    using AudioAssetRef = TAssetRef<Asset::AssetKind::Audio>;
    using RawAssetRef = TAssetRef<Asset::AssetKind::Raw>;

    static_assert(std::is_trivially_copyable_v<ModelAssetRef> && std::is_trivially_destructible_v<ModelAssetRef>);
    static_assert(std::is_trivially_copyable_v<TextureAssetRef> && std::is_trivially_destructible_v<TextureAssetRef>);
    static_assert(std::is_trivially_copyable_v<SkeletalAssetRef> && std::is_trivially_destructible_v<SkeletalAssetRef>);
    static_assert(std::is_trivially_copyable_v<AnimationAssetRef> && std::is_trivially_destructible_v<AnimationAssetRef>);
    static_assert(std::is_trivially_copyable_v<AudioAssetRef> && std::is_trivially_destructible_v<AudioAssetRef>);
    static_assert(std::is_trivially_copyable_v<RawAssetRef> && std::is_trivially_destructible_v<RawAssetRef>);
    static_assert(std::is_trivially_copyable_v<TAssetRef<Asset::AssetKind::Unknown>> &&
                  std::is_trivially_destructible_v<TAssetRef<Asset::AssetKind::Unknown>>);
    static_assert(std::is_trivially_copyable_v<TAssetRef<Asset::AssetKind::World>> &&
                  std::is_trivially_destructible_v<TAssetRef<Asset::AssetKind::World>>);
} // namespace NorvesLib::Core
