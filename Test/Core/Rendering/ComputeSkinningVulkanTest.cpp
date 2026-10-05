// 計算シェーダーでのスキニング（skinning_compute.comp / SkinningCompute）の GPU テスト。
// 実際の SkinnedMeshGpuStore が作る今・前のフレームのパレットを使い、2 つのインスタンス（頂点数・骨の数・変換が違う）を
// 1 本の出力バッファの別の範囲へ書き、読み戻した今・前のフレームの頂点（位置・法線・UV）が CPU で計算した値と
// 許容 1e-4 で一致することを確かめる。
//   - インスタンス A: 150 頂点・骨 3 本（ワークグループ 64 を跨ぐ）。骨 1 本・2 本・4 本の影響、重みの合計が 1 でない頂点、
//     重みが 0 の頂点（束縛の姿勢のまま）、範囲外の骨の番号、乱数の頂点を含む。法線行列が単位行列でない非一様スケールの骨を使う。
//   - インスタンス B: 9 頂点・骨 2 本（骨の番号 2 は範囲外）。出力の範囲は A の直後から始まる。
//   - 出力の範囲の外（前後の番号）は初期値のまま（範囲外へ書かない）。
//   - 範囲が出力バッファに収まらない dispatch は記録されず false を返す。
//   - Vulkan の validation error が 0 件であること。
// CPU の参照は、エンジンの行列関数に頼らずに（行ベクトル規約の行列を float の配列で組み）独立に求める。
// Vulkan デバイスが無い環境では 125（スキップ）を返す。
#include "Rendering/ShaderManager.h"
#include "Rendering/SkinnedMeshGpuStore.h"
#include "Rendering/SkinnedMeshTypes.h"
#include "Rendering/SkinningComputePass.h"

#include "Math/Matrix4x4.h"
#include "RHI/IBuffer.h"
#include "RHI/ICommandList.h"
#include "RHI/IDevice.h"
#include "RHI/RHIDeviceDesc.h"
#include "RHI/RHIDeviceFactory.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <iostream>
#include <stdexcept>
#include <utility>

namespace NorvesLib::RHI::Vulkan
{
void BeginVulkanValidationErrorCaptureForTesting() noexcept;
void EndVulkanValidationErrorCaptureForTesting() noexcept;
uint32_t GetVulkanValidationErrorCaptureHitCountForTesting() noexcept;
}

namespace
{
    using namespace NorvesLib;
    using namespace NorvesLib::Core;
    using namespace NorvesLib::Core::Rendering;
    using namespace NorvesLib::RHI;

    constexpr const char* TestName = "ComputeSkinningVulkanTest";
    constexpr int GpuTestSkipReturnCode = 125;
    constexpr float Tolerance = 1.0e-4f;
    // 出力の範囲の外に置く見張りの頂点の数と、その初期値
    constexpr uint32_t GuardVertices = 4u;
    constexpr float GuardValue = 12345.0f;

    int g_failures = 0;

    void Expect(bool condition, const char* message)
    {
        if (!condition)
        {
            std::cerr << TestName << " 失敗: " << message << std::endl;
            ++g_failures;
        }
    }

    bool IsGpuTestSkipForced()
    {
        char* forceSkip = nullptr;
        size_t forceSkipLength = 0;
        if (_dupenv_s(&forceSkip, &forceSkipLength, "NORVESLIB_FORCE_GPU_TEST_SKIP") != 0 || forceSkip == nullptr)
        {
            return false;
        }
        const bool bForceSkip = std::strcmp(forceSkip, "1") == 0;
        free(forceSkip);
        return bForceSkip;
    }

    int SkipGpuTest(const char* reason)
    {
        std::cout << TestName << " スキップ: " << reason << std::endl;
        return GpuTestSkipReturnCode;
    }

    class VulkanValidationErrorCapture
    {
    public:
        VulkanValidationErrorCapture() { RHI::Vulkan::BeginVulkanValidationErrorCaptureForTesting(); }
        ~VulkanValidationErrorCapture() { RHI::Vulkan::EndVulkanValidationErrorCaptureForTesting(); }
        uint32_t GetHitCount() const { return RHI::Vulkan::GetVulkanValidationErrorCaptureHitCountForTesting(); }
    };

    // ========================================
    // CPU の参照（行ベクトル規約: 点は 1x4 の行ベクトルに行列を右から掛け、並進は行 3）
    // ========================================

    struct Mat4
    {
        float v[16] = {1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1};
    };

    struct Vec3
    {
        float x = 0.0f;
        float y = 0.0f;
        float z = 0.0f;
    };

    // a を先に、b を後に適用する合成（行ベクトル × a × b）
    Mat4 Mul(const Mat4& a, const Mat4& b)
    {
        Mat4 result;
        for (int row = 0; row < 4; ++row)
        {
            for (int column = 0; column < 4; ++column)
            {
                float sum = 0.0f;
                for (int k = 0; k < 4; ++k)
                {
                    sum += a.v[row * 4 + k] * b.v[k * 4 + column];
                }
                result.v[row * 4 + column] = sum;
            }
        }
        return result;
    }

    Mat4 Scale(float x, float y, float z)
    {
        Mat4 m;
        m.v[0] = x;
        m.v[5] = y;
        m.v[10] = z;
        return m;
    }

    Mat4 Translate(float x, float y, float z)
    {
        Mat4 m;
        m.v[12] = x;
        m.v[13] = y;
        m.v[14] = z;
        return m;
    }

    Mat4 RotateX(float angle)
    {
        Mat4 m;
        m.v[5] = std::cos(angle);
        m.v[6] = std::sin(angle);
        m.v[9] = -std::sin(angle);
        m.v[10] = std::cos(angle);
        return m;
    }

    Mat4 RotateY(float angle)
    {
        Mat4 m;
        m.v[0] = std::cos(angle);
        m.v[2] = -std::sin(angle);
        m.v[8] = std::sin(angle);
        m.v[10] = std::cos(angle);
        return m;
    }

    Mat4 RotateZ(float angle)
    {
        Mat4 m;
        m.v[0] = std::cos(angle);
        m.v[1] = std::sin(angle);
        m.v[4] = -std::sin(angle);
        m.v[5] = std::cos(angle);
        return m;
    }

    Math::Matrix4x4 ToEngine(const Mat4& m)
    {
        Math::Matrix4x4 result;
        std::memcpy(result.values, m.v, sizeof(m.v));
        return result;
    }

    Vec3 TransformPoint(const Mat4& m, const Vec3& p)
    {
        return {p.x * m.v[0] + p.y * m.v[4] + p.z * m.v[8] + m.v[12],
                p.x * m.v[1] + p.y * m.v[5] + p.z * m.v[9] + m.v[13],
                p.x * m.v[2] + p.y * m.v[6] + p.z * m.v[10] + m.v[14]};
    }

    // 法線の変換（行ベクトル × 上 3x3 の逆転置）。特異に近い行列は単位行列として扱う
    Vec3 TransformNormal(const Mat4& m, const Vec3& n)
    {
        const float a00 = m.v[0], a01 = m.v[1], a02 = m.v[2];
        const float a10 = m.v[4], a11 = m.v[5], a12 = m.v[6];
        const float a20 = m.v[8], a21 = m.v[9], a22 = m.v[10];
        const float determinant = a00 * (a11 * a22 - a12 * a21) - a01 * (a10 * a22 - a12 * a20) +
                                  a02 * (a10 * a21 - a11 * a20);
        if (std::abs(determinant) < 1.0e-6f)
        {
            return n;
        }
        const float inv = 1.0f / determinant;
        // 逆行列 B = A^-1（B[row][column]）
        const float b[3][3] = {
            {(a11 * a22 - a12 * a21) * inv, (a02 * a21 - a01 * a22) * inv, (a01 * a12 - a02 * a11) * inv},
            {(a12 * a20 - a10 * a22) * inv, (a00 * a22 - a02 * a20) * inv, (a02 * a10 - a00 * a12) * inv},
            {(a10 * a21 - a11 * a20) * inv, (a01 * a20 - a00 * a21) * inv, (a00 * a11 - a01 * a10) * inv}};
        // n' = n × (A^-1)^T → n'_j = Σ_i n_i * B[j][i]
        return {n.x * b[0][0] + n.y * b[0][1] + n.z * b[0][2],
                n.x * b[1][0] + n.y * b[1][1] + n.z * b[1][2],
                n.x * b[2][0] + n.y * b[2][1] + n.z * b[2][2]};
    }

    float Dot(const Vec3& a, const Vec3& b)
    {
        return a.x * b.x + a.y * b.y + a.z * b.z;
    }

    Vec3 Normalize(const Vec3& v)
    {
        const float length = std::sqrt(Dot(v, v));
        return {v.x / length, v.y / length, v.z / length};
    }

    // skinned_gbuffer.vert の SkinVertex と同じ手順で骨のパレットから変形し、変換 world でワールド空間へ送る
    SkinnedOutputVertex SkinReference(const SkinnedMeshVertex& vertex,
                                      const Container::VariableArray<Mat4>& palette,
                                      const Mat4& world)
    {
        const Vec3 inPosition{vertex.Position[0], vertex.Position[1], vertex.Position[2]};
        const Vec3 inNormal{vertex.Normal[0], vertex.Normal[1], vertex.Normal[2]};
        Vec3 position;
        Vec3 normal;
        float totalWeight = 0.0f;
        for (int influence = 0; influence < 4; ++influence)
        {
            const uint32_t boneIndex = vertex.BoneIndices[influence];
            const float weight = vertex.BoneWeights[influence];
            if (weight <= 0.0f || boneIndex >= palette.size())
            {
                continue;
            }
            const Vec3 p = TransformPoint(palette[boneIndex], inPosition);
            const Vec3 n = TransformNormal(palette[boneIndex], inNormal);
            position = {position.x + p.x * weight, position.y + p.y * weight, position.z + p.z * weight};
            normal = {normal.x + n.x * weight, normal.y + n.y * weight, normal.z + n.z * weight};
            totalWeight += weight;
        }
        if (totalWeight <= 0.000001f)
        {
            position = inPosition;
            normal = inNormal;
        }
        else
        {
            position = {position.x / totalWeight, position.y / totalWeight, position.z / totalWeight};
            normal = {normal.x / totalWeight, normal.y / totalWeight, normal.z / totalWeight};
            if (Dot(normal, normal) > 0.000001f)
            {
                normal = Normalize(normal);
            }
        }

        const Vec3 worldPosition = TransformPoint(world, position);
        Vec3 worldNormal = TransformNormal(world, normal);
        worldNormal = Dot(worldNormal, worldNormal) > 0.000001f ? Normalize(worldNormal) : Vec3{};

        SkinnedOutputVertex out;
        out.Position[0] = worldPosition.x;
        out.Position[1] = worldPosition.y;
        out.Position[2] = worldPosition.z;
        out.Normal[0] = worldNormal.x;
        out.Normal[1] = worldNormal.y;
        out.Normal[2] = worldNormal.z;
        out.TexCoord[0] = vertex.TexCoord[0];
        out.TexCoord[1] = vertex.TexCoord[1];
        return out;
    }

    // ========================================
    // 入力
    // ========================================

    uint32_t g_RandomState = 0x12345678u;

    float NextRandom01()
    {
        g_RandomState = g_RandomState * 1664525u + 1013904223u;
        return static_cast<float>((g_RandomState >> 8) & 0xFFFFu) / 65535.0f;
    }

    SkinnedMeshVertex MakeVertex(Vec3 position, Vec3 normal, float u, float v,
                                 const uint32_t (&bones)[4], const float (&weights)[4])
    {
        SkinnedMeshVertex vertex;
        vertex.Position[0] = position.x;
        vertex.Position[1] = position.y;
        vertex.Position[2] = position.z;
        const Vec3 unit = Normalize(normal);
        vertex.Normal[0] = unit.x;
        vertex.Normal[1] = unit.y;
        vertex.Normal[2] = unit.z;
        vertex.TexCoord[0] = u;
        vertex.TexCoord[1] = v;
        for (int index = 0; index < 4; ++index)
        {
            vertex.BoneIndices[index] = bones[index];
            vertex.BoneWeights[index] = weights[index];
        }
        return vertex;
    }

    SkinnedMeshVertex MakeRandomVertex(uint32_t boneCount)
    {
        const Vec3 position{NextRandom01() * 4.0f - 2.0f, NextRandom01() * 4.0f - 2.0f, NextRandom01() * 4.0f - 2.0f};
        const Vec3 normal{NextRandom01() - 0.5f + 0.1f, NextRandom01() - 0.5f, NextRandom01() - 0.5f};
        uint32_t bones[4];
        float weights[4];
        float sum = 0.0f;
        for (int index = 0; index < 4; ++index)
        {
            bones[index] = static_cast<uint32_t>(NextRandom01() * static_cast<float>(boneCount)) % boneCount;
            // 4 つのうち 1〜2 個は重み 0（影響しない）にする
            weights[index] = NextRandom01() < 0.3f ? 0.0f : NextRandom01() + 0.05f;
            sum += weights[index];
        }
        if (sum <= 0.0f)
        {
            weights[0] = 1.0f;
            sum = 1.0f;
        }
        for (float& weight : weights)
        {
            weight /= sum;
        }
        return MakeVertex(position, normal, NextRandom01(), NextRandom01(), bones, weights);
    }

    Container::VariableArray<SkinnedMeshVertex> MakeVerticesA()
    {
        Container::VariableArray<SkinnedMeshVertex> vertices;
        // 骨 1 本
        vertices.push_back(MakeVertex({0.5f, 1.0f, -0.25f}, {0.0f, 1.0f, 0.2f}, 0.1f, 0.2f, {0, 0, 0, 0}, {1.0f, 0.0f, 0.0f, 0.0f}));
        // 骨 2 本
        vertices.push_back(MakeVertex({-1.0f, 0.5f, 0.75f}, {1.0f, 0.3f, 0.0f}, 0.3f, 0.4f, {0, 1, 0, 0}, {0.25f, 0.75f, 0.0f, 0.0f}));
        // 骨 4 本。重みの合計が 0.8（1 でない）
        vertices.push_back(MakeVertex({0.2f, -0.8f, 1.2f}, {0.2f, 0.2f, 1.0f}, 0.5f, 0.6f, {0, 1, 2, 1}, {0.4f, 0.2f, 0.1f, 0.1f}));
        // 重みが全て 0: 束縛の姿勢のまま（変換だけ掛かる）
        vertices.push_back(MakeVertex({1.5f, 0.25f, -0.5f}, {0.0f, 0.0f, 1.0f}, 0.7f, 0.8f, {2, 1, 0, 0}, {0.0f, 0.0f, 0.0f, 0.0f}));
        // 範囲外の骨の番号（9）は飛ばされ、骨 1 だけの重み 0.5 で割り戻す
        vertices.push_back(MakeVertex({-0.5f, -0.5f, 0.5f}, {0.5f, 0.5f, 0.5f}, 0.9f, 1.0f, {9, 1, 0, 0}, {0.5f, 0.5f, 0.0f, 0.0f}));
        // 同じ骨を 4 つ
        vertices.push_back(MakeVertex({0.0f, 1.0f, 1.0f}, {1.0f, 0.0f, 0.5f}, 0.0f, 1.0f, {2, 2, 2, 2}, {0.25f, 0.25f, 0.25f, 0.25f}));
        while (vertices.size() < 150)
        {
            vertices.push_back(MakeRandomVertex(3u));
        }
        return vertices;
    }

    Container::VariableArray<SkinnedMeshVertex> MakeVerticesB()
    {
        Container::VariableArray<SkinnedMeshVertex> vertices;
        while (vertices.size() < 9)
        {
            // 骨は 3 本ぶんの番号を使うが、B のパレットは 2 本（番号 2 は範囲外）
            vertices.push_back(MakeRandomVertex(3u));
        }
        return vertices;
    }

    struct InstanceCase
    {
        Container::VariableArray<SkinnedMeshVertex> Vertices;
        Container::VariableArray<Mat4> Palette;
        Container::VariableArray<Mat4> PreviousPalette;
        Mat4 World;
        Mat4 PreviousWorld;
        SkinnedMeshHandle Handle;
        Container::TSharedPtr<const SkinnedMeshFrameLease> FrameLease;
        SkinnedMeshPreparedDraw Prepared;
        uint32_t VertexBase = 0;
    };

    void BuildInstanceA(InstanceCase& instance)
    {
        instance.Vertices = MakeVerticesA();
        instance.Palette.push_back(Mul(RotateY(0.6f), Translate(0.5f, -1.0f, 2.0f)));
        instance.Palette.push_back(Mul(Mul(Scale(1.5f, 0.75f, 2.0f), RotateX(0.4f)), Translate(-1.0f, 0.5f, 0.0f)));
        instance.Palette.push_back(Mul(Mul(Scale(0.5f, 1.25f, 1.0f), RotateZ(-0.9f)), Translate(0.0f, 2.0f, -1.0f)));
        instance.PreviousPalette.push_back(Mul(RotateY(0.2f), Translate(0.1f, -1.5f, 2.5f)));
        instance.PreviousPalette.push_back(Mul(Mul(Scale(1.4f, 0.8f, 1.8f), RotateX(0.1f)), Translate(-1.2f, 0.4f, 0.1f)));
        instance.PreviousPalette.push_back(Mul(Mul(Scale(0.6f, 1.1f, 1.0f), RotateZ(-0.5f)), Translate(0.3f, 1.6f, -0.7f)));
        instance.World = Mul(Mul(Scale(1.0f, 2.0f, 0.5f), RotateZ(0.3f)), Translate(5.0f, -2.0f, 1.0f));
        instance.PreviousWorld = Mul(Mul(Scale(1.0f, 2.0f, 0.5f), RotateZ(0.1f)), Translate(4.5f, -2.2f, 1.0f));
        instance.Handle = SkinnedMeshHandle{1, 1};
    }

    void BuildInstanceB(InstanceCase& instance)
    {
        instance.Vertices = MakeVerticesB();
        instance.Palette.push_back(Mul(Mul(Scale(2.0f, 1.0f, 1.0f), RotateY(-0.7f)), Translate(1.0f, 1.0f, 1.0f)));
        instance.Palette.push_back(Mul(RotateX(1.1f), Translate(-2.0f, 0.0f, 0.5f)));
        instance.PreviousPalette.push_back(Mul(Mul(Scale(1.8f, 1.0f, 1.2f), RotateY(-0.4f)), Translate(0.8f, 1.2f, 0.9f)));
        instance.PreviousPalette.push_back(Mul(RotateX(0.8f), Translate(-1.7f, 0.2f, 0.4f)));
        instance.World = Mul(RotateY(1.0f), Translate(-3.0f, 0.5f, -4.0f));
        instance.PreviousWorld = Mul(RotateY(0.7f), Translate(-3.4f, 0.5f, -3.5f));
        instance.Handle = SkinnedMeshHandle{2, 1};
    }

    // 使う骨の行列を、エンジンの行列の配列へ（SkinnedMeshGpuStore::PrepareDraw の引数）
    Container::VariableArray<Math::Matrix4x4> ToEngineArray(const Container::VariableArray<Mat4>& source)
    {
        Container::VariableArray<Math::Matrix4x4> result;
        for (const Mat4& matrix : source)
        {
            result.push_back(ToEngine(matrix));
        }
        return result;
    }

    bool PrepareInstance(SkinnedMeshGpuStore& store, InstanceCase& instance)
    {
        Container::VariableArray<uint32_t> indices;
        indices.push_back(0u);
        indices.push_back(1u);
        indices.push_back(2u);
        Container::VariableArray<SkinnedMeshVertex> vertices = instance.Vertices;
        auto assetLease = Container::MakeShared<SkinnedMeshAssetLease>(instance.Handle, std::move(vertices), std::move(indices));
        instance.FrameLease = Container::MakeShared<SkinnedMeshFrameLease>(assetLease);
        const Container::VariableArray<Math::Matrix4x4> palette = ToEngineArray(instance.Palette);
        const Container::VariableArray<Math::Matrix4x4> previousPalette = ToEngineArray(instance.PreviousPalette);
        const Math::Matrix4x4 world = ToEngine(instance.World);
        const Math::Matrix4x4 previousWorld = ToEngine(instance.PreviousWorld);
        return store.PrepareDraw(instance.FrameLease, palette, world, instance.Prepared, &previousPalette, &previousWorld) &&
               instance.Prepared.IsValid() && instance.Prepared.PreviousPaletteBuffer;
    }

    // ========================================
    // 比較
    // ========================================

    struct CompareStats
    {
        uint32_t Compared = 0;
        uint32_t Mismatched = 0;
        float MaxError = 0.0f;
    };

    void CompareVertex(const float* actual, const SkinnedOutputVertex& expected, uint32_t vertexNumber, const char* label,
                       CompareStats& stats)
    {
        static_assert(sizeof(SkinnedOutputVertex) == sizeof(float) * 8, "SkinnedOutputVertex は float 8 個");
        float expectedValues[8];
        std::memcpy(expectedValues, &expected, sizeof(expectedValues));
        bool bMismatch = false;
        for (int component = 0; component < 8; ++component)
        {
            const float error = std::abs(actual[component] - expectedValues[component]);
            stats.MaxError = std::max(stats.MaxError, std::isfinite(error) ? error : 1.0e9f);
            if (!(error <= Tolerance))
            {
                bMismatch = true;
            }
        }
        ++stats.Compared;
        if (bMismatch)
        {
            if (stats.Mismatched < 4)
            {
                std::cerr << TestName << " " << label << " 頂点 " << vertexNumber << " が一致しない: GPU=[";
                for (int component = 0; component < 8; ++component)
                {
                    std::cerr << (component ? "," : "") << actual[component];
                }
                std::cerr << "] CPU=[";
                for (int component = 0; component < 8; ++component)
                {
                    std::cerr << (component ? "," : "") << expectedValues[component];
                }
                std::cerr << "]" << std::endl;
            }
            ++stats.Mismatched;
        }
    }

    void FillWithGuard(const BufferPtr& buffer)
    {
        const uint64_t size = buffer->GetSize();
        float* mapped = static_cast<float*>(buffer->Map(0u, size));
        if (mapped == nullptr)
        {
            ++g_failures;
            return;
        }
        for (uint64_t index = 0; index < size / sizeof(float); ++index)
        {
            mapped[index] = GuardValue;
        }
        buffer->Unmap();
    }

    int RunTest()
    {
        if (IsGpuTestSkipForced())
        {
            return SkipGpuTest("NORVESLIB_FORCE_GPU_TEST_SKIP=1 が指定された");
        }

        VulkanValidationErrorCapture validationCapture;
        RHIDeviceDesc deviceDesc;
        deviceDesc.Api = GraphicsAPI::Vulkan;
        deviceDesc.bEnableValidation = true;
        DevicePtr device = RHI::CreateRHIDevice(deviceDesc);
        if (!device || device->GetAPI() != API::Vulkan)
        {
            return SkipGpuTest("Vulkanデバイスを利用できません");
        }

        ShaderManager shaderManager;
        String shaderRoot(NORVES_SOURCE_ROOT);
        shaderRoot += "/Assets/Shaders";
        if (!shaderManager.Initialize(device.get(), shaderRoot))
        {
            std::cerr << TestName << " ShaderManagerを初期化できませんでした" << std::endl;
            return 1;
        }

        {
            SkinningCompute compute;
            if (!compute.Initialize(device.get(), &shaderManager))
            {
                std::cerr << TestName << " 計算スキニングを初期化できませんでした" << std::endl;
                return 1;
            }

            SkinnedMeshGpuStore store(device);
            store.BeginFrame(0);
            InstanceCase instances[2];
            BuildInstanceA(instances[0]);
            BuildInstanceB(instances[1]);
            uint32_t totalVertices = 0;
            for (InstanceCase& instance : instances)
            {
                if (!PrepareInstance(store, instance))
                {
                    std::cerr << TestName << " SkinnedMeshGpuStore::PrepareDraw に失敗" << std::endl;
                    return 1;
                }
                instance.VertexBase = totalVertices;
                totalVertices += static_cast<uint32_t>(instance.Vertices.size());
            }

            // 出力の範囲の前後に見張りの頂点を置く。先頭の見張りの後ろから A、B の順に詰める。
            const uint32_t bufferVertices = GuardVertices + totalVertices + GuardVertices;
            const uint64_t bufferBytes = static_cast<uint64_t>(bufferVertices) * sizeof(SkinnedOutputVertex);
            const ResourceUsage outputUsage =
                ResourceUsage::StorageBuffer | ResourceUsage::ShaderRead | ResourceUsage::BufferDeviceAddress;
            BufferPtr currentOut = device->CreateBuffer(BufferDesc(bufferBytes, outputUsage, true, "ComputeSkinningCurrent"));
            BufferPtr previousOut = device->CreateBuffer(BufferDesc(bufferBytes, outputUsage, true, "ComputeSkinningPrevious"));
            if (!currentOut || !previousOut)
            {
                std::cerr << TestName << " 出力のバッファを作れませんでした" << std::endl;
                return 1;
            }
            FillWithGuard(currentOut);
            FillWithGuard(previousOut);

            if (device->GetCapabilities().bBufferDeviceAddress)
            {
                Expect(currentOut->GetDeviceAddress() != 0 && previousOut->GetDeviceAddress() != 0,
                       "BDA が使えるデバイスでは出力のバッファのアドレスを取れなければならない");
                Expect(instances[0].Prepared.VertexBuffer->GetDeviceAddress() != 0 &&
                           instances[0].Prepared.IndexBuffer->GetDeviceAddress() != 0,
                       "スキニングの頂点・インデックスのバッファもアドレスで読めなければならない");
            }

            CommandListPtr commandList = device->CreateCommandList();
            if (!commandList)
            {
                std::cerr << TestName << " コマンドリストを作れませんでした" << std::endl;
                return 1;
            }

            compute.BeginFrame(0, 1);
            commandList->Begin();
            commandList->BufferBarrier(currentOut, ResourceState::Undefined, ResourceState::UnorderedAccess, 0u, bufferBytes);
            commandList->BufferBarrier(previousOut, ResourceState::Undefined, ResourceState::UnorderedAccess, 0u, bufferBytes);
            for (InstanceCase& instance : instances)
            {
                SkinningComputeDispatch dispatch;
                dispatch.SkinVertices = instance.Prepared.VertexBuffer;
                dispatch.Palette = instance.Prepared.PaletteBuffer;
                dispatch.PreviousPalette = instance.Prepared.PreviousPaletteBuffer;
                dispatch.CurrentVertices = currentOut;
                dispatch.PreviousVertices = previousOut;
                dispatch.VertexCount = static_cast<uint32_t>(instance.Vertices.size());
                dispatch.OutputVertexBase = GuardVertices + instance.VertexBase;
                Expect(compute.Record(commandList.get(), dispatch), "インスタンスの変形を記録できなければならない");
            }

            // 範囲が出力バッファに収まらない dispatch は記録されず、出力へも書かれない
            {
                SkinningComputeDispatch overflow;
                overflow.SkinVertices = instances[0].Prepared.VertexBuffer;
                overflow.Palette = instances[0].Prepared.PaletteBuffer;
                overflow.PreviousPalette = instances[0].Prepared.PreviousPaletteBuffer;
                overflow.CurrentVertices = currentOut;
                overflow.PreviousVertices = previousOut;
                overflow.VertexCount = static_cast<uint32_t>(instances[0].Vertices.size());
                overflow.OutputVertexBase = bufferVertices - 10u;
                Expect(!compute.Record(commandList.get(), overflow), "出力に収まらない範囲は記録してはならない");
                SkinningComputeDispatch empty = overflow;
                empty.VertexCount = 0;
                empty.OutputVertexBase = 0;
                Expect(!compute.Record(commandList.get(), empty), "頂点が 0 個の dispatch は記録してはならない");
            }

            commandList->BufferBarrier(currentOut, ResourceState::UnorderedAccess, ResourceState::HostRead, 0u, bufferBytes);
            commandList->BufferBarrier(previousOut, ResourceState::UnorderedAccess, ResourceState::HostRead, 0u, bufferBytes);
            commandList->End();
            commandList->Submit(true);
            device->WaitIdle();

            const float* currentValues = static_cast<const float*>(currentOut->Map(0u, bufferBytes));
            const float* previousValues = static_cast<const float*>(previousOut->Map(0u, bufferBytes));
            if (currentValues == nullptr || previousValues == nullptr)
            {
                std::cerr << TestName << " 出力を読み戻せませんでした" << std::endl;
                return 1;
            }

            CompareStats currentStats;
            CompareStats previousStats;
            float maxMotion = 0.0f;
            for (const InstanceCase& instance : instances)
            {
                for (uint32_t vertexIndex = 0; vertexIndex < instance.Vertices.size(); ++vertexIndex)
                {
                    const uint32_t outputIndex = GuardVertices + instance.VertexBase + vertexIndex;
                    const SkinnedOutputVertex expectedCurrent =
                        SkinReference(instance.Vertices[vertexIndex], instance.Palette, instance.World);
                    const SkinnedOutputVertex expectedPrevious =
                        SkinReference(instance.Vertices[vertexIndex], instance.PreviousPalette, instance.PreviousWorld);
                    CompareVertex(currentValues + outputIndex * 8u, expectedCurrent, outputIndex, "今のフレーム", currentStats);
                    CompareVertex(previousValues + outputIndex * 8u, expectedPrevious, outputIndex, "前のフレーム", previousStats);
                    for (int component = 0; component < 3; ++component)
                    {
                        maxMotion = std::max(maxMotion,
                                             std::abs(expectedCurrent.Position[component] - expectedPrevious.Position[component]));
                    }
                }
            }

            // 見張りの頂点（範囲の外）は初期値のまま
            uint32_t guardViolations = 0;
            for (uint32_t vertexIndex = 0; vertexIndex < bufferVertices; ++vertexIndex)
            {
                const bool bInside = vertexIndex >= GuardVertices && vertexIndex < GuardVertices + totalVertices;
                if (bInside)
                {
                    continue;
                }
                for (uint32_t component = 0; component < 8; ++component)
                {
                    if (currentValues[vertexIndex * 8u + component] != GuardValue ||
                        previousValues[vertexIndex * 8u + component] != GuardValue)
                    {
                        ++guardViolations;
                    }
                }
            }
            currentOut->Unmap();
            previousOut->Unmap();

            std::cout << TestName << " 今のフレーム: 比較=" << currentStats.Compared << " 不一致=" << currentStats.Mismatched
                      << " 最大誤差=" << currentStats.MaxError << std::endl;
            std::cout << TestName << " 前のフレーム: 比較=" << previousStats.Compared << " 不一致=" << previousStats.Mismatched
                      << " 最大誤差=" << previousStats.MaxError << std::endl;
            std::cout << TestName << " 今と前の位置の最大の差(CPU)=" << maxMotion << " 範囲外の書き込み=" << guardViolations
                      << std::endl;

            Expect(currentStats.Compared == totalVertices && previousStats.Compared == totalVertices,
                   "全頂点を今・前の両方で比較しなければならない");
            Expect(currentStats.Mismatched == 0, "今のフレームの頂点が CPU の値と一致しなければならない");
            Expect(previousStats.Mismatched == 0, "前のフレームの頂点が CPU の値と一致しなければならない");
            Expect(maxMotion > 0.1f, "今と前のパレットが同じでは前のフレームの検査にならない");
            Expect(guardViolations == 0, "出力の範囲の外へ書いてはならない");

            device->WaitIdle();
        }
        shaderManager.Shutdown();

        const uint32_t validationErrorCount = validationCapture.GetHitCount();
        std::cout << "VUID_COUNT=" << validationErrorCount << '\n';
        Expect(validationErrorCount == 0u, "Vulkan の検証エラーが出てはならない");

        std::cout << (g_failures == 0 ? "RESULT=PASS" : "RESULT=FAIL") << '\n';
        return g_failures == 0 ? 0 : 1;
    }
} // namespace

int main()
{
    try
    {
        return RunTest();
    }
    catch (const std::exception& exception)
    {
        std::cerr << TestName << "で例外が出ました: " << exception.what() << '\n';
        return 1;
    }
}
