#include "CookMeshOptimizer.h"

#include <meshoptimizer.h>

#include <algorithm>

namespace NorvesLib::Tools::AssetCook
{
    namespace
    {
        bool ValidateMesh(const float* positions,
                          size_t vertexCount,
                          size_t positionStrideBytes,
                          const uint32_t* indices,
                          size_t indexCount,
                          Core::Container::AnsiString& error)
        {
            if (positions == nullptr || indices == nullptr)
            {
                error = "CookMeshOptimizer: 頂点位置か索引が null です";
                return false;
            }
            if (vertexCount == 0 || indexCount == 0 || indexCount % 3 != 0)
            {
                error = "CookMeshOptimizer: 頂点数が 0 か、索引数が 3 の倍数ではありません";
                return false;
            }
            if (positionStrideBytes < sizeof(float) * 3 || positionStrideBytes % sizeof(float) != 0)
            {
                error = "CookMeshOptimizer: 頂点位置の stride が 12 バイト未満か 4 の倍数ではありません";
                return false;
            }
            for (size_t i = 0; i < indexCount; ++i)
            {
                if (indices[i] >= vertexCount)
                {
                    error = "CookMeshOptimizer: 索引が頂点数の範囲外です";
                    return false;
                }
            }
            return true;
        }
    }

    bool SimplifyMeshTriangles(const float* positions,
                               size_t vertexCount,
                               size_t positionStrideBytes,
                               const uint32_t* indices,
                               size_t indexCount,
                               const CookSimplifyParams& params,
                               CookSimplifyResult& outResult,
                               Core::Container::AnsiString& error)
    {
        outResult = CookSimplifyResult{};
        if (!ValidateMesh(positions, vertexCount, positionStrideBytes, indices, indexCount, error))
        {
            return false;
        }
        if (params.TargetIndexCount % 3 != 0 || params.TargetIndexCount > indexCount)
        {
            error = "CookMeshOptimizer: 目標の索引数が 3 の倍数ではないか、元の索引数を超えています";
            return false;
        }
        if (!params.VertexLock.empty() && params.VertexLock.size() != vertexCount)
        {
            error = "CookMeshOptimizer: 固定フラグの大きさが頂点数と合いません";
            return false;
        }

        outResult.Indices.assign(indexCount, 0u);
        float relativeError = 0.0f;
        // 上流は flag & 1 で固定を判定するので、0 でない値は meshopt_SimplifyVertex_Lock へ正規化して渡す。
        Core::Container::VariableArray<unsigned char> lockFlags;
        const unsigned char* lock = nullptr;
        if (!params.VertexLock.empty())
        {
            lockFlags.resize(vertexCount);
            for (size_t v = 0; v < vertexCount; ++v)
            {
                lockFlags[v] = params.VertexLock[v] != 0 ? static_cast<unsigned char>(meshopt_SimplifyVertex_Lock) : 0;
            }
            lock = lockFlags.data();
        }
        const size_t resultCount = meshopt_simplifyWithAttributes(outResult.Indices.data(),
                                                                  indices,
                                                                  indexCount,
                                                                  positions,
                                                                  vertexCount,
                                                                  positionStrideBytes,
                                                                  nullptr,
                                                                  0,
                                                                  nullptr,
                                                                  0,
                                                                  lock,
                                                                  params.TargetIndexCount,
                                                                  params.TargetErrorRelative,
                                                                  0,
                                                                  &relativeError);
        outResult.Indices.resize(resultCount);
        outResult.ErrorRelative = relativeError;
        outResult.ErrorAbsolute = relativeError * meshopt_simplifyScale(positions, vertexCount, positionStrideBytes);
        return true;
    }

    Core::Container::VariableArray<uint8_t> FindBoundaryVertices(const float* positions,
                                                                 size_t vertexCount,
                                                                 size_t positionStrideBytes,
                                                                 const uint32_t* indices,
                                                                 size_t indexCount)
    {
        Core::Container::VariableArray<uint8_t> boundary(vertexCount, 0);
        Core::Container::AnsiString unused;
        if (!ValidateMesh(positions, vertexCount, positionStrideBytes, indices, indexCount, unused))
        {
            return boundary;
        }

        // 位置が同じ頂点(UV の継ぎ目などで複製されたもの)を同じ頂点として辺を数える。
        Core::Container::VariableArray<uint32_t> remap(vertexCount, 0u);
        meshopt_generatePositionRemap(remap.data(), positions, vertexCount, positionStrideBytes);

        Core::Container::VariableArray<uint64_t> edges;
        edges.reserve(indexCount);
        for (size_t i = 0; i < indexCount; i += 3)
        {
            for (size_t e = 0; e < 3; ++e)
            {
                const uint32_t a = remap[indices[i + e]];
                const uint32_t b = remap[indices[i + (e + 1) % 3]];
                const uint32_t lo = std::min(a, b);
                const uint32_t hi = std::max(a, b);
                edges.push_back((static_cast<uint64_t>(lo) << 32) | hi);
            }
        }
        std::sort(edges.begin(), edges.end());

        Core::Container::VariableArray<uint8_t> boundaryByRemapped(vertexCount, 0);
        for (size_t i = 0; i < edges.size();)
        {
            size_t j = i + 1;
            while (j < edges.size() && edges[j] == edges[i])
            {
                ++j;
            }
            if (j - i == 1)
            {
                boundaryByRemapped[static_cast<uint32_t>(edges[i] >> 32)] = 1;
                boundaryByRemapped[static_cast<uint32_t>(edges[i] & 0xFFFFFFFFu)] = 1;
            }
            i = j;
        }

        for (size_t v = 0; v < vertexCount; ++v)
        {
            boundary[v] = boundaryByRemapped[remap[v]];
        }
        return boundary;
    }
}
