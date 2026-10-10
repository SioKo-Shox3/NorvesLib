#pragma once

/**
 * @file SceneProxyHash.h
 * @brief 描画の proxy の内容を 64 bit の値にまとめる（検査 ECS_RENDER_INVALIDATION が、公開した内容と作り直した内容を比べる）
 *
 * 見るのは Component が決める内容だけ。SceneView が公開の後に書き換える値（ソートキー・ソート用の深さ）は含めない。
 * 構造体ごとバイト列を読まず、フィールドを 1 つずつ足す（パディングが不定のため）。浮動小数は bit のまま足す。
 */

#include "Math/Matrix4x4.h"
#include "Math/Vector2.h"
#include "Math/Vector3.h"
#include "Math/Vector4.h"
#include "Rendering/SceneProxy.h"
#include "Rendering/SkinnedMeshTypes.h"

#include <cstdint>
#include <cstring>
#include <type_traits>

namespace NorvesLib::Core::Rendering
{
    /// FNV-1a で値を足していく
    class ProxyHasher
    {
    public:
        void AddBytes(const void* data, size_t size)
        {
            const unsigned char* bytes = static_cast<const unsigned char*>(data);
            for (size_t i = 0; i < size; ++i)
            {
                m_Hash ^= bytes[i];
                m_Hash *= 1099511628211ull;
            }
        }

        void Add(uint64_t value) { AddBytes(&value, sizeof(value)); }
        void Add(uint32_t value) { AddBytes(&value, sizeof(value)); }
        void Add(bool value) { Add(static_cast<uint64_t>(value ? 1u : 0u)); }

        void Add(float value)
        {
            uint32_t bits = 0;
            std::memcpy(&bits, &value, sizeof(bits));
            Add(bits);
        }

        template <typename E, std::enable_if_t<std::is_enum_v<E>, int> = 0>
        void Add(E value)
        {
            Add(static_cast<uint64_t>(static_cast<std::underlying_type_t<E>>(value)));
        }

        template <typename Tag>
        void Add(const ResourceHandle<Tag>& handle) { Add(handle.Id); }

        void Add(const Math::Vector2& v)
        {
            Add(v.x);
            Add(v.y);
        }

        void Add(const Math::Vector3& v)
        {
            Add(v.x);
            Add(v.y);
            Add(v.z);
        }

        void Add(const Math::Vector4& v)
        {
            Add(v.x);
            Add(v.y);
            Add(v.z);
            Add(v.w);
        }

        void Add(const Math::Matrix4x4& matrix)
        {
            for (int row = 0; row < 4; ++row)
            {
                for (int column = 0; column < 4; ++column)
                {
                    Add(matrix.m[row][column]);
                }
            }
        }

        void Add(const BoundingSphere& sphere)
        {
            Add(sphere.CenterX);
            Add(sphere.CenterY);
            Add(sphere.CenterZ);
            Add(sphere.Radius);
        }

        [[nodiscard]] uint64_t Value() const { return m_Hash; }

    private:
        uint64_t m_Hash = 1469598103934665603ull;
    };

    inline uint64_t HashProxy(const MeshProxy& p)
    {
        ProxyHasher h;
        h.Add(p.ObjectId);
        h.Add(p.ComponentId);
        h.Add(p.MeshHandle);
        h.Add(static_cast<uint32_t>(p.LODLevel));
        h.Add(p.SubMeshCount);
        for (uint32_t i = 0; i < p.SubMeshCount && i < MAX_MATERIAL_SLOTS; ++i)
        {
            h.Add(p.SubMeshes[i].IndexStart);
            h.Add(p.SubMeshes[i].IndexCount);
            h.Add(p.SubMeshes[i].VertexStart);
            h.Add(p.SubMeshes[i].MaterialIndex);
        }
        h.Add(p.WorldTransform);
        h.Add(p.PreviousWorldTransform);
        h.Add(p.WorldBounds);
        h.Add(p.MaterialCount);
        for (uint32_t i = 0; i < p.MaterialCount && i < MAX_MATERIAL_SLOTS; ++i)
        {
            h.Add(p.Materials[i]);
            h.Add(p.MaterialBlendModes[i]);
        }
        h.Add(p.bHasMaterialOverrides);
        h.Add(p.bVisible);
        h.Add(p.bCastShadow);
        h.Add(p.bReceiveShadow);
        h.Add(p.bAffectDynamicIndirectLighting);
        h.Add(p.bAffectDistanceFieldLighting);
        h.Add(p.LayerMask);
        for (int i = 0; i < 4; ++i)
        {
            h.Add(p.CustomData[i]);
        }
        return h.Value();
    }

    inline uint64_t HashProxy(const SkinnedMeshProxy& p)
    {
        ProxyHasher h;
        h.Add(p.ObjectId);
        h.Add(p.ComponentId);
        h.Add(p.MeshHandle.Id);
        h.Add(p.MeshHandle.Generation);
        h.Add(p.Material);
        h.Add(p.MaterialCount);
        for (uint32_t i = 0; i < p.MaterialCount && i < MAX_MATERIAL_SLOTS; ++i)
        {
            h.Add(p.Materials[i]);
        }
        h.Add(static_cast<uint64_t>(reinterpret_cast<uintptr_t>(p.AssetLease.lock().get())));
        h.Add(p.WorldTransform);
        h.Add(static_cast<uint64_t>(p.BonePalette.size()));
        for (const Math::Matrix4x4& bone : p.BonePalette)
        {
            h.Add(bone);
        }
        h.Add(p.AnimatedBounds.Min);
        h.Add(p.AnimatedBounds.Max);
        h.Add(p.bCastShadow);
        h.Add(p.bHasAnimatedBounds);
        h.Add(p.bVisible);
        return h.Value();
    }

    inline uint64_t HashProxy(const MegaGeometryProxy& p)
    {
        ProxyHasher h;
        h.Add(p.ObjectId);
        h.Add(p.ComponentId);
        h.Add(p.MegaMeshHandle);
        h.Add(p.WorldTransform);
        h.Add(p.PreviousWorldTransform);
        h.Add(p.WorldBounds);
        h.Add(p.bVisible);
        h.Add(p.bCastShadow);
        h.Add(p.LayerMask);
        return h.Value();
    }

    inline uint64_t HashProxy(const BoardProxy& p)
    {
        ProxyHasher h;
        h.Add(p.ObjectId);
        h.Add(p.ComponentId);
        h.Add(p.SortKey);
        h.Add(p.LayerPriority);
        h.Add(p.OrderInLayer);
        h.Add(p.Texture);
        h.Add(p.WorldTransform);
        h.Add(p.PreviousWorldTransform);
        h.Add(p.WorldBounds);
        h.Add(p.LayerMask);
        h.Add(p.Space);
        h.Add(p.RenderSubtype);
        h.Add(p.BlendModeProp);
        h.Add(p.SourceMeshComponentId);
        h.Add(p.LODSwitchDistance);
        h.Add(p.ImpostorCellResolution);
        h.Add(p.ImpostorAxisCellCountX);
        h.Add(p.ImpostorAxisCellCountY);
        h.Add(p.ImpostorAtlasWidth);
        h.Add(p.ImpostorAtlasHeight);
        h.Add(p.Tint);
        h.Add(p.bFlipX);
        h.Add(p.bFlipY);
        h.Add(p.Pivot);
        h.Add(p.SizePx);
        h.Add(p.SizeWorld);
        h.Add(p.UVRect);
        h.Add(p.bVisible);
        return h.Value();
    }

    inline uint64_t HashProxy(const LightProxy& p)
    {
        ProxyHasher h;
        h.Add(p.LightId);
        h.Add(p.Type);
        h.Add(p.PositionX);
        h.Add(p.PositionY);
        h.Add(p.PositionZ);
        h.Add(p.DirectionX);
        h.Add(p.DirectionY);
        h.Add(p.DirectionZ);
        h.Add(p.ColorR);
        h.Add(p.ColorG);
        h.Add(p.ColorB);
        h.Add(p.CanonicalIntensity);
        h.Add(p.Range);
        h.Add(p.AttenuationConstant);
        h.Add(p.AttenuationLinear);
        h.Add(p.AttenuationQuadratic);
        h.Add(p.InnerConeAngle);
        h.Add(p.OuterConeAngle);
        h.Add(p.bCastShadows);
        h.Add(p.ShadowBias);
        h.Add(p.ShadowMapResolution);
        h.Add(p.bVisible);
        h.Add(p.AffectedLayers);
        return h.Value();
    }
}
