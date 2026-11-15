#include "Rendering/RenderExtension/RenderExtensionShadowCaster.h"
#include "Logging/LogMacros.h"
#include "Math/MatrixUtils.h"
#include "RHI/IBuffer.h"
#include "Rendering/ProceduralMeshGenerator.h"
#include "Rendering/VirtualShadowMapCasters.h"
#include <cmath>

namespace NorvesLib::Core::Rendering
{
    namespace
    {
        static_assert(sizeof(Mesh3DVertex) == 32, "影の束の頂点は 32 バイトでなければなりません");

        constexpr uint64_t FnvOffset = 14695981039346656037ULL;
        constexpr uint64_t FnvPrime = 1099511628211ULL;
        constexpr uint64_t ExplicitKeyBit = 1ULL << 63;

        enum Rejection : uint32_t
        {
            MissingBuffer = 1u << 0,
            InvalidIndexCount = 1u << 1,
            IndexRange = 1u << 2,
            VertexSize = 1u << 3,
            InvalidBounds = 1u << 4,
            EmptyInstances = 1u << 5,
            InvalidWorld = 1u << 6,
            DrawLimit = 1u << 7,
            InstanceLimit = 1u << 8
        };

        uint64_t HashName(const char* name)
        {
            uint64_t hash = FnvOffset;
            for (const char* c = name; *c; ++c)
            {
                hash ^= static_cast<unsigned char>(*c);
                hash *= FnvPrime;
            }
            return hash;
        }

        uint64_t MakeDrawKey(uint64_t nameHash, uint64_t value, bool bExplicit)
        {
            // 種別のビットを分け、明示キーと提出順のキーを必ず異なる範囲に置く。
            uint64_t hash = nameHash;
            for (uint32_t i = 0; i < 8; ++i)
            {
                hash ^= (value >> (i * 8)) & 0xffULL;
                hash *= FnvPrime;
            }
            return (hash & ~ExplicitKeyBit) | (bExplicit ? ExplicitKeyBit : 0);
        }

        bool IsFiniteBounds(const BoundingBox& bounds)
        {
            return bounds.IsValid() && std::isfinite(bounds.MinX) && std::isfinite(bounds.MinY) &&
                   std::isfinite(bounds.MinZ) && std::isfinite(bounds.MaxX) &&
                   std::isfinite(bounds.MaxY) && std::isfinite(bounds.MaxZ);
        }
    } // namespace

    RenderExtensionShadowCasterSink::RenderExtensionShadowCasterSink(RenderExtensionShadowCasterFrame& frame,
                                                                     const char* name,
                                                                     uint32_t& loggedReasons)
        : m_Frame(frame), m_Name(name ? name : ""), m_LoggedReasons(loggedReasons), m_NameHash(HashName(m_Name))
    {
    }

    bool RenderExtensionShadowCasterSink::Reject(uint32_t reasonBit, const char* reason)
    {
        if ((m_LoggedReasons & reasonBit) == 0)
        {
            NORVES_LOG_WARNING("RenderExtension", "拡張 '%s' の影の束を捨てました: %s", m_Name, reason);
            m_LoggedReasons |= reasonBit;
        }
        return false;
    }

    bool RenderExtensionShadowCasterSink::Submit(const RenderExtensionShadowCasterBatch& batch)
    {
        const uint64_t submissionIndex = m_SubmissionIndex++;
        if (!batch.VertexBuffer || !batch.IndexBuffer)
        {
            return Reject(MissingBuffer, "頂点またはインデックスのバッファがありません");
        }
        if (batch.IndexCount == 0 || batch.IndexCount % 3 != 0)
        {
            return Reject(InvalidIndexCount, "インデックス数が 0 または 3 の倍数ではありません");
        }
        const uint64_t indexEnd = static_cast<uint64_t>(batch.FirstIndex) + batch.IndexCount;
        if (indexEnd * sizeof(uint32_t) > batch.IndexBuffer->GetSize())
        {
            return Reject(IndexRange, "インデックスの範囲がバッファを超えています");
        }
        if ((static_cast<uint64_t>(batch.VertexOffset) + 1) * sizeof(Mesh3DVertex) > batch.VertexBuffer->GetSize())
        {
            return Reject(VertexSize, "頂点の開始位置が頂点バッファの外です");
        }
        if (!IsFiniteBounds(batch.LocalBounds))
        {
            return Reject(InvalidBounds, "ローカルの境界が無効か有限ではありません");
        }
        if (batch.Instances.empty() || !batch.Instances.data())
        {
            return Reject(EmptyInstances, "インスタンスがありません");
        }
        if (m_Frame.m_Draws.size() >= RENDER_EXTENSION_SHADOW_CASTER_MAX_DRAWS)
        {
            return Reject(DrawLimit, "影の束の上限を超えます");
        }
        if (batch.Instances.size() > RENDER_EXTENSION_SHADOW_CASTER_MAX_INSTANCES - m_Frame.m_Instances.size())
        {
            return Reject(InstanceLimit, "インスタンスの上限を超えます");
        }

        for (const Math::Matrix4x4& matrix : batch.Instances)
        {
            float world[16];
            Math::MatrixUtils::CopyToShaderData(matrix, world);
            for (float value : world)
            {
                if (!std::isfinite(value))
                {
                    return Reject(InvalidWorld, "ワールド行列に有限でない値があります");
                }
            }
        }

        RenderExtensionShadowCasterDraw draw;
        draw.VertexBuffer = batch.VertexBuffer;
        draw.IndexBuffer = batch.IndexBuffer;
        draw.FirstIndex = batch.FirstIndex;
        draw.IndexCount = batch.IndexCount;
        draw.VertexOffset = batch.VertexOffset;
        draw.LocalBounds = batch.LocalBounds;
        draw.Material = batch.Material;
        draw.CoverageMultiplier = batch.CoverageMultiplier;
        draw.bPointLightShadow = batch.bPointLightShadow;
        draw.Key = MakeDrawKey(m_NameHash, batch.Key ? batch.Key : submissionIndex, batch.Key != 0);
        draw.ContentRevision = batch.ContentRevision;
        draw.FirstInstance = static_cast<uint32_t>(m_Frame.m_Instances.size());
        draw.InstanceCount = static_cast<uint32_t>(batch.Instances.size());

        try
        {
            for (const Math::Matrix4x4& matrix : batch.Instances)
            {
                RenderExtensionShadowCasterInstance instance;
                Math::MatrixUtils::CopyToShaderData(matrix, instance.World);
                const VirtualShadowMap::CasterBounds bounds = VirtualShadowMap::TransformBoundsByWorld(instance.World, batch.LocalBounds);
                instance.WorldBounds = {bounds.Min[0], bounds.Min[1], bounds.Min[2], bounds.Max[0], bounds.Max[1], bounds.Max[2]};
                const BoundingBox& box = instance.WorldBounds;
                const double halfX = (static_cast<double>(box.MaxX) - box.MinX) * 0.5;
                const double halfY = (static_cast<double>(box.MaxY) - box.MinY) * 0.5;
                const double halfZ = (static_cast<double>(box.MaxZ) - box.MinZ) * 0.5;
                instance.WorldSphere.CenterX = static_cast<float>((static_cast<double>(box.MinX) + box.MaxX) * 0.5);
                instance.WorldSphere.CenterY = static_cast<float>((static_cast<double>(box.MinY) + box.MaxY) * 0.5);
                instance.WorldSphere.CenterZ = static_cast<float>((static_cast<double>(box.MinZ) + box.MaxZ) * 0.5);
                instance.WorldSphere.Radius = static_cast<float>(std::sqrt(halfX * halfX + halfY * halfY + halfZ * halfZ));
                if (instance.WorldSphere.Radius == 0.0f)
                {
                    instance.WorldSphere.Radius = 1e-4f;
                }
                m_Frame.m_Instances.push_back(instance);
            }
            m_Frame.m_Draws.push_back(draw);
        }
        catch (...)
        {
            // 拡張側が例外を捕まえても、この束の途中までを残さない。
            m_Frame.m_Instances.resize(draw.FirstInstance);
            throw;
        }
        return true;
    }
} // namespace NorvesLib::Core::Rendering
