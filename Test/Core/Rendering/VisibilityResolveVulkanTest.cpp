// ビジビリティバッファの幾何の解決（visbuffer_resolve.comp / VisibilityResolve）の GPU テスト。
// 合成した VisBuffer.Id の画像（100x60。8 で割れない部分タイルを含む）・描画の記録の表・頂点とインデックスのバッファ・
// 材質の表・インスタンスの表から、GBuffer の Albedo・Normal・Velocity と、画素ごとの中間の値（重心座標・UV・解析的な微分・
// 位置の微分・接線の基底・前のクリップ座標）が、CPU の独立した参照と一致することを確かめる。
//   参照はラスタライザと同じ式（頂点をクリップ座標へ投影し、画面上の重心座標を 1/w で透視補正する）で、倍精度で作る。
//   シェーダーは光線と三角形の平面の交点で求めるので、式が違う 2 つの計算が合えば、カメラの規約（Y 反転・深度）も合っている。
//   描画の種類ごとに次を確かめる:
//     手続きメッシュの塊（32bit インデックス・頂点の基点つき・法線は法線行列の行から）、
//     手続きメッシュの塊（16bit インデックス・奇数の先頭位置・前の変換は別のインスタンスの表の要素）、
//     MegaGeometry のクラスタ（先頭位置が 3 の倍数でない・負でない頂点の基点・法線は変換の上 3x3）、
//     スキニングの塊 2 体（1 つのバッファの別の位置から始まる 2 体。記録の頂点の基点は 0 で、アドレスが体ごとの先頭を持つ。
//       2 体目の位置・速度が 1 体目に取り違えられず、基点が二重に足されると範囲外を読んで落ちる）。
//   空の画素・引けない ID（表に無い記録・記録の三角形数以上の番号）は何も書かないこと。前のカメラが無いと速度は 0。
//   製品の版と検証用の版（書き出しつき）の両方で、どちらも Vulkan の validation error が 0 件。
// Vulkan デバイスが無い環境、または解決に対応しない装置では 125（スキップ）を返す。
#include "Container/Containers.h"
#include "Rendering/CameraViewConstants.h"
#include "Rendering/SceneProxy.h"
#include "Rendering/ShaderManager.h"
#include "Rendering/VisibilityBuffer.h"
#include "Rendering/VisibilityMaterialTable.h"
#include "Rendering/VisibilityResolvePass.h"

#include "RHI/DeviceCapabilities.h"
#include "RHI/IBuffer.h"
#include "RHI/ICommandList.h"
#include "RHI/IDevice.h"
#include "RHI/ITexture.h"
#include "RHI/RHIDeviceDesc.h"
#include "RHI/RHIDeviceFactory.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <iostream>
#include <stdexcept>

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

    constexpr const char* TestName = "VisibilityResolveVulkanTest";
    constexpr int GpuTestSkipReturnCode = 125;
    constexpr uint32_t ScreenWidth = 100;
    constexpr uint32_t ScreenHeight = 60;
    // 出力の画像の、何も書かれなかった画素の見張りの値
    constexpr uint8_t AlbedoGuard[4] = {0x12, 0x34, 0x56, 0x78};
    constexpr uint16_t HalfGuard = 0xC700; // -7.0

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
    // 倍精度の小さな数学（列優先の 4x4。GLSL の mat4 と同じ並び）
    // ========================================

    struct Vec3
    {
        double X = 0.0;
        double Y = 0.0;
        double Z = 0.0;
    };

    Vec3 operator+(Vec3 a, Vec3 b) { return {a.X + b.X, a.Y + b.Y, a.Z + b.Z}; }
    Vec3 operator-(Vec3 a, Vec3 b) { return {a.X - b.X, a.Y - b.Y, a.Z - b.Z}; }
    Vec3 operator*(Vec3 a, double s) { return {a.X * s, a.Y * s, a.Z * s}; }
    double Dot(Vec3 a, Vec3 b) { return a.X * b.X + a.Y * b.Y + a.Z * b.Z; }
    Vec3 Cross(Vec3 a, Vec3 b)
    {
        return {a.Y * b.Z - a.Z * b.Y, a.Z * b.X - a.X * b.Z, a.X * b.Y - a.Y * b.X};
    }
    Vec3 Normalize(Vec3 a)
    {
        const double length = std::sqrt(Dot(a, a));
        return length > 0.0 ? a * (1.0 / length) : a;
    }

    struct Mat4
    {
        double M[16] = {1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1};
    };

    Mat4 Multiply(const Mat4& a, const Mat4& b)
    {
        Mat4 result;
        for (int column = 0; column < 4; ++column)
        {
            for (int row = 0; row < 4; ++row)
            {
                double sum = 0.0;
                for (int k = 0; k < 4; ++k)
                {
                    sum += a.M[k * 4 + row] * b.M[column * 4 + k];
                }
                result.M[column * 4 + row] = sum;
            }
        }
        return result;
    }

    Mat4 Translate(double x, double y, double z)
    {
        Mat4 m;
        m.M[12] = x;
        m.M[13] = y;
        m.M[14] = z;
        return m;
    }

    Mat4 Scale(double x, double y, double z)
    {
        Mat4 m;
        m.M[0] = x;
        m.M[5] = y;
        m.M[10] = z;
        return m;
    }

    Mat4 RotateY(double angle)
    {
        Mat4 m;
        m.M[0] = std::cos(angle);
        m.M[2] = -std::sin(angle);
        m.M[8] = std::sin(angle);
        m.M[10] = std::cos(angle);
        return m;
    }

    Vec3 TransformPoint(const Mat4& m, Vec3 p)
    {
        return {m.M[0] * p.X + m.M[4] * p.Y + m.M[8] * p.Z + m.M[12],
                m.M[1] * p.X + m.M[5] * p.Y + m.M[9] * p.Z + m.M[13],
                m.M[2] * p.X + m.M[6] * p.Y + m.M[10] * p.Z + m.M[14]};
    }

    // mat3(m) * n（上 3x3 をそのまま掛ける）
    Vec3 TransformUpper3x3(const Mat4& m, Vec3 n)
    {
        return {m.M[0] * n.X + m.M[4] * n.Y + m.M[8] * n.Z,
                m.M[1] * n.X + m.M[5] * n.Y + m.M[9] * n.Z,
                m.M[2] * n.X + m.M[6] * n.Y + m.M[10] * n.Z};
    }

    void ToFloats(const Mat4& m, float* out)
    {
        for (int index = 0; index < 16; ++index)
        {
            out[index] = static_cast<float>(m.M[index]);
        }
    }

    struct Vec4
    {
        double X = 0.0;
        double Y = 0.0;
        double Z = 0.0;
        double W = 0.0;
    };

    // clip = viewProjection * (p, 1)（viewProjection は CopyShaderViewProjection の列優先の並び）
    Vec4 ProjectToClip(const float* viewProjection, Vec3 p)
    {
        Vec4 clip;
        double* out[4] = {&clip.X, &clip.Y, &clip.Z, &clip.W};
        for (int row = 0; row < 4; ++row)
        {
            *out[row] = static_cast<double>(viewProjection[0 * 4 + row]) * p.X +
                        static_cast<double>(viewProjection[1 * 4 + row]) * p.Y +
                        static_cast<double>(viewProjection[2 * 4 + row]) * p.Z +
                        static_cast<double>(viewProjection[3 * 4 + row]);
        }
        return clip;
    }

    float HalfToFloat(uint16_t half)
    {
        const uint32_t sign = (half >> 15) & 1u;
        const uint32_t exponent = (half >> 10) & 0x1Fu;
        const uint32_t mantissa = half & 0x3FFu;
        float value = 0.0f;
        if (exponent == 0)
        {
            value = std::ldexp(static_cast<float>(mantissa), -24);
        }
        else if (exponent == 31)
        {
            value = mantissa == 0 ? INFINITY : NAN;
        }
        else
        {
            value = std::ldexp(static_cast<float>(mantissa | 0x400u), static_cast<int>(exponent) - 25);
        }
        return sign ? -value : value;
    }

    // ========================================
    // 合成する場面
    // ========================================

    struct Vertex
    {
        float Position[3];
        float Normal[3];
        float Uv[2];
    };
    static_assert(sizeof(Vertex) == 32, "シェーダーの VisVertex と同じ 32 バイト");

    struct DrawInstanceData
    {
        float World[16];
        float PreviousWorld[16];
        float NormalRows[12];
        float ObjectColor[4];
        float CustomData[4];
    };
    static_assert(sizeof(DrawInstanceData) == 208, "シェーダーの VisDrawInstance と同じ 208 バイト");

    struct MegaInstanceData
    {
        float World[16];
        float PreviousWorld[16];
        float LodSphere[4];
        uint32_t ClusterInfo[4];
        uint32_t DrawInfo[4];
        uint32_t BvhInfo[4];
    };
    static_assert(sizeof(MegaInstanceData) == 192, "シェーダーの VisMegaInstance と同じ 192 バイト");

    // 1 つの三角形の、ワールド空間での参照のデータ（倍精度）
    struct ReferenceTriangle
    {
        Vec3 Position[3];
        Vec3 Previous[3];
        Vec3 Normal[3];
        double Uv[3][2] = {};
        uint32_t RecordNumber = 0;
        uint32_t TriangleIndex = 0;
        uint32_t Material = 0;
        uint32_t Kind = 0;
    };

    // 単位の四角形の 4 頂点（局所）。UV は [0, uvScale]、法線は頂点ごとに少しずつ傾ける（補間と正規化の検査）
    void MakeQuadVertices(double uvScaleU, double uvScaleV, double normalTilt, Vertex out[4])
    {
        const float positions[4][3] = {{0, 0, 0}, {1, 0, 0}, {0, 1, 0}, {1, 1, 0}};
        const float uvs[4][2] = {{0, 0}, {1, 0}, {0, 1}, {1, 1}};
        const float tilts[4][2] = {{0.0f, 0.0f}, {1.0f, 0.0f}, {0.0f, 1.0f}, {0.7f, 0.7f}};
        for (int index = 0; index < 4; ++index)
        {
            for (int axis = 0; axis < 3; ++axis)
            {
                out[index].Position[axis] = positions[index][axis];
            }
            out[index].Normal[0] = static_cast<float>(tilts[index][0] * normalTilt);
            out[index].Normal[1] = static_cast<float>(tilts[index][1] * normalTilt);
            out[index].Normal[2] = 1.0f;
            out[index].Uv[0] = static_cast<float>(uvs[index][0] * uvScaleU);
            out[index].Uv[1] = static_cast<float>(uvs[index][1] * uvScaleV);
        }
    }

    Vec3 VertexPosition(const Vertex& v) { return {v.Position[0], v.Position[1], v.Position[2]}; }
    Vec3 VertexNormal(const Vertex& v) { return {v.Normal[0], v.Normal[1], v.Normal[2]}; }

    // 四角形（頂点 0..3）の三角形 2 つ（0,1,2）と（2,1,3）
    constexpr uint32_t QuadIndices[6] = {0, 1, 2, 2, 1, 3};

    struct Scene
    {
        VisibilityBuffer::RecordTable Records;
        Container::VariableArray<VisibilityBuffer::MaterialEntry> Materials;
        Container::VariableArray<DrawInstanceData> DrawInstances;
        Container::VariableArray<MegaInstanceData> MegaInstances;
        Container::VariableArray<Vertex> ProceduralVertices1;
        Container::VariableArray<uint32_t> ProceduralIndices1;
        Container::VariableArray<Vertex> ProceduralVertices2;
        Container::VariableArray<uint16_t> ProceduralIndices2;
        Container::VariableArray<Vertex> MegaVertices;
        Container::VariableArray<uint32_t> MegaIndices;
        Container::VariableArray<Vertex> SkinnedCurrent;
        Container::VariableArray<Vertex> SkinnedPrevious;
        Container::VariableArray<uint32_t> SkinnedIndices;
        Container::VariableArray<ReferenceTriangle> References;
    };

    constexpr uint32_t SkinnedBaseB = 6; // 2 体目の頂点の先頭（1 体目の後ろに 2 頂点の隙間を置く）

    void AddReferences(Scene& scene,
                       uint32_t recordNumber,
                       uint32_t kind,
                       uint32_t material,
                       const Vec3 pos[4],
                       const Vec3 prev[4],
                       const Vec3 nrm[4],
                       const Vertex quad[4])
    {
        for (uint32_t triangle = 0; triangle < 2; ++triangle)
        {
            ReferenceTriangle ref;
            for (uint32_t k = 0; k < 3; ++k)
            {
                const uint32_t corner = QuadIndices[triangle * 3 + k];
                ref.Position[k] = pos[corner];
                ref.Previous[k] = prev[corner];
                ref.Normal[k] = nrm[corner];
                ref.Uv[k][0] = quad[corner].Uv[0];
                ref.Uv[k][1] = quad[corner].Uv[1];
            }
            ref.RecordNumber = recordNumber;
            ref.TriangleIndex = triangle;
            ref.Material = material;
            ref.Kind = kind;
            scene.References.push_back(ref);
        }
    }

    VisibilityBuffer::MaterialEntry MakeMaterial(float r, float g, float b)
    {
        VisibilityBuffer::MaterialEntry entry;
        entry.BaseColor[0] = r;
        entry.BaseColor[1] = g;
        entry.BaseColor[2] = b;
        return entry;
    }

    void FillWorldMatrices(DrawInstanceData& data, const Mat4& world, const Mat4& previousWorld)
    {
        ToFloats(world, data.World);
        ToFloats(previousWorld, data.PreviousWorld);
    }

    Scene BuildScene()
    {
        Scene scene;
        scene.Materials.push_back(MakeMaterial(0.9f, 0.1f, 0.2f));
        scene.Materials.push_back(MakeMaterial(0.2f, 0.8f, 0.3f));
        scene.Materials.push_back(MakeMaterial(0.1f, 0.3f, 0.9f));
        scene.Materials.push_back(MakeMaterial(0.8f, 0.7f, 0.1f));

        Vertex quad[4];

        // ---- 手続きメッシュ 1: 32bit インデックス・頂点の基点 2・法線は法線行列の行から ----
        MakeQuadVertices(2.0, 3.0, 0.35, quad);
        for (int pad = 0; pad < 2; ++pad)
        {
            Vertex garbage = {};
            garbage.Position[0] = 100.0f;
            scene.ProceduralVertices1.push_back(garbage);
        }
        for (const Vertex& v : quad)
        {
            scene.ProceduralVertices1.push_back(v);
        }
        for (const uint32_t index : QuadIndices)
        {
            scene.ProceduralIndices1.push_back(index);
        }
        {
            const Mat4 world = Multiply(Multiply(Translate(-3.2, -0.5, 0.0), RotateY(0.35)), Scale(2.2, 1.8, 1.0));
            const Mat4 previousWorld = Multiply(Multiply(Translate(-3.35, -0.55, 0.1), RotateY(0.30)), Scale(2.2, 1.8, 1.0));
            DrawInstanceData data = {};
            FillWorldMatrices(data, world, previousWorld);
            const float rows[12] = {0.95f, 0.05f, 0.0f, 0.0f, 0.0f, 1.1f, 0.1f, 0.0f, -0.1f, 0.0f, 0.9f, 0.0f};
            std::memcpy(data.NormalRows, rows, sizeof(rows));
            scene.DrawInstances.push_back(data); // 0

            Vec3 pos[4];
            Vec3 prev[4];
            Vec3 nrm[4];
            for (int corner = 0; corner < 4; ++corner)
            {
                pos[corner] = TransformPoint(world, VertexPosition(quad[corner]));
                prev[corner] = TransformPoint(previousWorld, VertexPosition(quad[corner]));
                const Vec3 n = VertexNormal(quad[corner]);
                nrm[corner] = {rows[0] * n.X + rows[4] * n.Y + rows[8] * n.Z,
                               rows[1] * n.X + rows[5] * n.Y + rows[9] * n.Z,
                               rows[2] * n.X + rows[6] * n.Y + rows[10] * n.Z};
            }
            VisibilityBuffer::DrawRecord record;
            record.Kind = static_cast<uint32_t>(VisibilityBuffer::RecordKind::ProceduralChunk);
            record.InstanceIndex = 0;
            record.MaterialIndex = 0;
            record.TriangleCount = 2;
            record.FirstIndex = 0;
            record.VertexBase = 2;
            record.PreviousTransformIndex = 0;
            const uint32_t number = scene.Records.Add(record);
            AddReferences(scene, number, record.Kind, record.MaterialIndex, pos, prev, nrm, quad);
        }

        // ---- 手続きメッシュ 2: 16bit インデックス・奇数の先頭位置・前の変換は別のインスタンスの表の要素 ----
        MakeQuadVertices(1.0, 1.0, 0.2, quad);
        for (const Vertex& v : quad)
        {
            scene.ProceduralVertices2.push_back(v);
        }
        // 先頭の 1 つは使わない（先頭位置 1 から読む）。後ろに 1 つ足して、16bit の並びを偶数個にする
        scene.ProceduralIndices2.push_back(3);
        for (const uint32_t index : QuadIndices)
        {
            scene.ProceduralIndices2.push_back(static_cast<uint16_t>(index));
        }
        scene.ProceduralIndices2.push_back(0);
        {
            const Mat4 world = Multiply(Multiply(Translate(-0.8, -0.4, -0.5), RotateY(-0.4)), Scale(1.9, 1.6, 1.0));
            // 前の変換は、記録が指す要素（1）の previousWorld から引く。要素 2 の previousWorld は引かれない（遠い別の値）
            const Mat4 previousWorldUsed = Multiply(Multiply(Translate(-0.9, -0.45, -0.4), RotateY(-0.35)), Scale(1.9, 1.6, 1.0));
            DrawInstanceData decoy = {};
            FillWorldMatrices(decoy, Multiply(Translate(50, 50, 50), Scale(1, 1, 1)), previousWorldUsed);
            const float identityRows[12] = {1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0};
            std::memcpy(decoy.NormalRows, identityRows, sizeof(identityRows));
            scene.DrawInstances.push_back(decoy); // 1

            DrawInstanceData data = {};
            FillWorldMatrices(data, world, Multiply(Translate(-30, 0, 0), Scale(1, 1, 1)));
            const float rows[12] = {1.0f, 0.1f, 0.0f, 0.0f, 0.0f, 0.9f, 0.05f, 0.0f, 0.0f, -0.1f, 1.05f, 0.0f};
            std::memcpy(data.NormalRows, rows, sizeof(rows));
            scene.DrawInstances.push_back(data); // 2

            Vec3 pos[4];
            Vec3 prev[4];
            Vec3 nrm[4];
            for (int corner = 0; corner < 4; ++corner)
            {
                pos[corner] = TransformPoint(world, VertexPosition(quad[corner]));
                prev[corner] = TransformPoint(previousWorldUsed, VertexPosition(quad[corner]));
                const Vec3 n = VertexNormal(quad[corner]);
                nrm[corner] = {rows[0] * n.X + rows[4] * n.Y + rows[8] * n.Z,
                               rows[1] * n.X + rows[5] * n.Y + rows[9] * n.Z,
                               rows[2] * n.X + rows[6] * n.Y + rows[10] * n.Z};
            }
            VisibilityBuffer::DrawRecord record;
            record.Kind = static_cast<uint32_t>(VisibilityBuffer::RecordKind::ProceduralChunk);
            record.InstanceIndex = 2;
            record.MaterialIndex = 1;
            record.TriangleCount = 2;
            record.FirstIndex = 1;
            record.VertexBase = 0;
            record.PreviousTransformIndex = 1;
            record.Flags = VisibilityBuffer::RECORD_FLAG_INDEX16;
            const uint32_t number = scene.Records.Add(record);
            AddReferences(scene, number, record.Kind, record.MaterialIndex, pos, prev, nrm, quad);
        }

        // ---- MegaGeometry のクラスタ: 先頭位置 4（3 の倍数でない）・頂点の基点 3・法線は変換の上 3x3 ----
        MakeQuadVertices(1.5, 2.5, 0.3, quad);
        for (int pad = 0; pad < 3; ++pad)
        {
            Vertex garbage = {};
            garbage.Position[1] = -100.0f;
            scene.MegaVertices.push_back(garbage);
        }
        for (const Vertex& v : quad)
        {
            scene.MegaVertices.push_back(v);
        }
        for (int pad = 0; pad < 4; ++pad)
        {
            scene.MegaIndices.push_back(77);
        }
        for (const uint32_t index : QuadIndices)
        {
            scene.MegaIndices.push_back(index);
        }
        {
            const Mat4 world = Multiply(Multiply(Translate(1.3, -0.3, 0.2), RotateY(0.2)), Scale(1.7, 2.1, 1.4));
            const Mat4 previousWorld = Multiply(Multiply(Translate(1.2, -0.35, 0.25), RotateY(0.15)), Scale(1.7, 2.1, 1.4));
            MegaInstanceData decoy = {};
            ToFloats(Translate(80, 80, 80), decoy.World);
            ToFloats(Translate(80, 80, 80), decoy.PreviousWorld);
            scene.MegaInstances.push_back(decoy); // 0
            MegaInstanceData data = {};
            ToFloats(world, data.World);
            ToFloats(previousWorld, data.PreviousWorld);
            scene.MegaInstances.push_back(data); // 1

            Vec3 pos[4];
            Vec3 prev[4];
            Vec3 nrm[4];
            for (int corner = 0; corner < 4; ++corner)
            {
                pos[corner] = TransformPoint(world, VertexPosition(quad[corner]));
                prev[corner] = TransformPoint(previousWorld, VertexPosition(quad[corner]));
                nrm[corner] = TransformUpper3x3(world, VertexNormal(quad[corner]));
            }
            VisibilityBuffer::DrawRecord record;
            record.Kind = static_cast<uint32_t>(VisibilityBuffer::RecordKind::MegaGeometryCluster);
            record.InstanceIndex = 1;
            record.MaterialIndex = 2;
            record.TriangleCount = 2;
            record.FirstIndex = 4;
            record.VertexBase = 3;
            const uint32_t number = scene.Records.Add(record);
            AddReferences(scene, number, record.Kind, record.MaterialIndex, pos, prev, nrm, quad);
        }

        // ---- スキニング 2 体: 1 つのバッファの 0 番と SkinnedBaseB 番から始まる、変形済み（ワールド空間）の頂点 ----
        scene.SkinnedCurrent.resize(SkinnedBaseB + 4);
        scene.SkinnedPrevious.resize(SkinnedBaseB + 4);
        for (const uint32_t index : QuadIndices)
        {
            scene.SkinnedIndices.push_back(index);
        }
        for (uint32_t body = 0; body < 2; ++body)
        {
            const uint32_t baseVertex = body == 0 ? 0u : SkinnedBaseB;
            const double bodyY = body == 0 ? -0.9 : 0.5;
            MakeQuadVertices(1.0, 1.0, 0.25, quad);
            Vec3 pos[4];
            Vec3 prev[4];
            Vec3 nrm[4];
            for (int corner = 0; corner < 4; ++corner)
            {
                const Vec3 local = VertexPosition(quad[corner]);
                // 少し曲げる（平面でない法線）。前のフレームは別の位置・曲がり
                pos[corner] = {3.5 + local.X * 1.1, bodyY + local.Y * 1.1, 0.1 + 0.3 * local.X * local.Y};
                prev[corner] = {3.42 + local.X * 1.1, bodyY - 0.07 + local.Y * 1.1, 0.12 + 0.2 * local.X * local.Y};
                nrm[corner] = VertexNormal(quad[corner]);
                Vertex current = quad[corner];
                current.Position[0] = static_cast<float>(pos[corner].X);
                current.Position[1] = static_cast<float>(pos[corner].Y);
                current.Position[2] = static_cast<float>(pos[corner].Z);
                Vertex previous = current;
                previous.Position[0] = static_cast<float>(prev[corner].X);
                previous.Position[1] = static_cast<float>(prev[corner].Y);
                previous.Position[2] = static_cast<float>(prev[corner].Z);
                scene.SkinnedCurrent[baseVertex + corner] = current;
                scene.SkinnedPrevious[baseVertex + corner] = previous;
            }
            VisibilityBuffer::DrawRecord record;
            record.Kind = static_cast<uint32_t>(VisibilityBuffer::RecordKind::SkinnedChunk);
            record.InstanceIndex = body;
            record.MaterialIndex = 3;
            record.TriangleCount = 2;
            record.FirstIndex = 0;
            record.VertexBase = 0; // アドレスが体ごとの先頭まで加算済み（二重に足さない）
            const uint32_t number = scene.Records.Add(record);
            AddReferences(scene, number, record.Kind, record.MaterialIndex, pos, prev, nrm, quad);
        }
        // 隙間の頂点（2 体の間）は、読まれると目立つ値にする
        for (uint32_t gap = 4; gap < SkinnedBaseB; ++gap)
        {
            Vertex garbage = {};
            garbage.Position[0] = -999.0f;
            scene.SkinnedCurrent[gap] = garbage;
            scene.SkinnedPrevious[gap] = garbage;
        }
        return scene;
    }

    // ========================================
    // 参照の計算（ラスタライザと同じ式）
    // ========================================

    struct CameraSet
    {
        CameraViewConstants Current;
        CameraViewConstants Previous;
        float CurrentViewProjection[16] = {};
        float PreviousViewProjection[16] = {};
    };

    CameraProxy MakeCamera(double x, double y, double z, Vec3 forward)
    {
        CameraProxy camera;
        camera.PositionX = static_cast<float>(x);
        camera.PositionY = static_cast<float>(y);
        camera.PositionZ = static_cast<float>(z);
        const Vec3 f = Normalize(forward);
        camera.ForwardX = static_cast<float>(f.X);
        camera.ForwardY = static_cast<float>(f.Y);
        camera.ForwardZ = static_cast<float>(f.Z);
        const Vec3 right = Normalize(Cross(f, Vec3{0, 1, 0}));
        const Vec3 up = Cross(right, f);
        camera.RightX = static_cast<float>(right.X);
        camera.RightY = static_cast<float>(right.Y);
        camera.RightZ = static_cast<float>(right.Z);
        camera.UpX = static_cast<float>(up.X);
        camera.UpY = static_cast<float>(up.Y);
        camera.UpZ = static_cast<float>(up.Z);
        camera.FieldOfView = 55.0f;
        camera.NearPlane = 0.1f;
        camera.FarPlane = 200.0f;
        camera.AspectRatio = static_cast<float>(ScreenWidth) / static_cast<float>(ScreenHeight);
        return camera;
    }

    // 三角形の頂点のクリップ座標と、画素の位置（NDC）から、透視を補正した重心座標を求める。
    // 3 頂点とも w > 0 のとき（この場面のカメラの前の三角形）に成り立つ。
    bool RasterBarycentric(const Vec4 clip[3], double ndcX, double ndcY, double out[3])
    {
        double sx[3];
        double sy[3];
        for (int k = 0; k < 3; ++k)
        {
            if (clip[k].W <= 0.0)
            {
                return false;
            }
            sx[k] = clip[k].X / clip[k].W;
            sy[k] = clip[k].Y / clip[k].W;
        }
        const double area = (sx[1] - sx[0]) * (sy[2] - sy[0]) - (sx[2] - sx[0]) * (sy[1] - sy[0]);
        if (std::fabs(area) < 1.0e-18)
        {
            return false;
        }
        double screen[3];
        screen[1] = ((ndcX - sx[0]) * (sy[2] - sy[0]) - (sx[2] - sx[0]) * (ndcY - sy[0])) / area;
        screen[2] = ((sx[1] - sx[0]) * (ndcY - sy[0]) - (ndcX - sx[0]) * (sy[1] - sy[0])) / area;
        screen[0] = 1.0 - screen[1] - screen[2];
        double sum = 0.0;
        double weighted[3];
        for (int k = 0; k < 3; ++k)
        {
            weighted[k] = screen[k] / clip[k].W;
            sum += weighted[k];
        }
        for (int k = 0; k < 3; ++k)
        {
            out[k] = weighted[k] / sum;
        }
        return true;
    }

    // 画素 (x, y) の中心の NDC（描画範囲は画面全体）
    double PixelNdcX(double x) { return (x + 0.5) / ScreenWidth * 2.0 - 1.0; }
    double PixelNdcY(double y) { return (y + 0.5) / ScreenHeight * 2.0 - 1.0; }

    struct PixelReference
    {
        bool bCovered = false;
        uint32_t ReferenceIndex = 0;
        double Barycentric[3] = {};
        double Uv[2] = {};
        double DuvDx[2] = {};
        double DuvDy[2] = {};
        Vec3 Hit;      // カメラ相対
        Vec3 DposDx;
        Vec3 DposDy;
        Vec3 Normal;
        Vec3 TangentT;
        Vec3 TangentB;
        Vec3 PreviousClipXYZ;
        double PreviousClipW = 0.0;
        double Velocity[2] = {};
    };

    Vec3 InterpolateVec(const Vec3 values[3], const double bary[3])
    {
        return values[0] * bary[0] + values[1] * bary[1] + values[2] * bary[2];
    }

    // CalculateCotangentFrame と同じ式（倍精度）
    void CotangentFrame(Vec3 normalUnnormalized, Vec3 dpdx, Vec3 dpdyVulkan, const double duvdx[2], const double duvdyVulkan[2],
                        Vec3& outT, Vec3& outB)
    {
        const Vec3 dp1 = dpdx;
        const Vec3 dp2 = dpdyVulkan * -1.0;
        const double duv1[2] = {duvdx[0], duvdx[1]};
        const double duv2[2] = {-duvdyVulkan[0], -duvdyVulkan[1]};
        const Vec3 n = Normalize(normalUnnormalized);
        const Vec3 dp2perp = Cross(dp2, n);
        const Vec3 dp1perp = Cross(n, dp1);
        Vec3 t = dp2perp * duv1[0] + dp1perp * duv2[0];
        Vec3 b = dp2perp * duv1[1] + dp1perp * duv2[1];
        const double maxLen2 = std::max(Dot(t, t), Dot(b, b));
        const double invmax = 1.0 / std::sqrt(maxLen2);
        outT = t * invmax;
        outB = b * invmax;
    }

    void ComputePixelReference(const Scene& scene,
                               const CameraSet& cameras,
                               uint32_t referenceIndex,
                               uint32_t x,
                               uint32_t y,
                               bool bPreviousValid,
                               PixelReference& out)
    {
        const ReferenceTriangle& ref = scene.References[referenceIndex];
        Vec4 clip[3];
        Vec4 previousClip[3];
        for (int k = 0; k < 3; ++k)
        {
            clip[k] = ProjectToClip(cameras.CurrentViewProjection, ref.Position[k]);
            previousClip[k] = ProjectToClip(cameras.PreviousViewProjection, ref.Previous[k]);
        }
        out.bCovered = true;
        out.ReferenceIndex = referenceIndex;
        const double ndcX = PixelNdcX(x);
        const double ndcY = PixelNdcY(y);
        double bary[3];
        double baryX[3];
        double baryY[3];
        RasterBarycentric(clip, ndcX, ndcY, bary);
        RasterBarycentric(clip, ndcX + 2.0 / ScreenWidth, ndcY, baryX);
        RasterBarycentric(clip, ndcX, ndcY + 2.0 / ScreenHeight, baryY);
        for (int k = 0; k < 3; ++k)
        {
            out.Barycentric[k] = bary[k];
        }
        for (int axis = 0; axis < 2; ++axis)
        {
            const auto interpolateUv = [&](const double b[3]) {
                return ref.Uv[0][axis] * b[0] + ref.Uv[1][axis] * b[1] + ref.Uv[2][axis] * b[2];
            };
            out.Uv[axis] = interpolateUv(bary);
            out.DuvDx[axis] = interpolateUv(baryX) - out.Uv[axis];
            out.DuvDy[axis] = interpolateUv(baryY) - out.Uv[axis];
        }
        const Vec3 camera = {cameras.Current.CameraPosition[0], cameras.Current.CameraPosition[1], cameras.Current.CameraPosition[2]};
        const Vec3 hitWorld = InterpolateVec(ref.Position, bary);
        out.Hit = hitWorld - camera;
        out.DposDx = InterpolateVec(ref.Position, baryX) - hitWorld;
        out.DposDy = InterpolateVec(ref.Position, baryY) - hitWorld;
        const Vec3 normalSum = InterpolateVec(ref.Normal, bary);
        out.Normal = Normalize(normalSum);
        CotangentFrame(normalSum, out.DposDx, out.DposDy, out.DuvDx, out.DuvDy, out.TangentT, out.TangentB);
        const Vec3 previousWorld = InterpolateVec(ref.Previous, bary);
        const Vec4 prevClip = ProjectToClip(cameras.PreviousViewProjection, previousWorld);
        out.PreviousClipXYZ = {prevClip.X, prevClip.Y, prevClip.Z};
        out.PreviousClipW = prevClip.W;
        if (bPreviousValid && std::fabs(prevClip.W) > 1.0e-6)
        {
            out.Velocity[0] = (ndcX - prevClip.X / prevClip.W) * 0.5;
            out.Velocity[1] = (ndcY - prevClip.Y / prevClip.W) * 0.5;
        }
    }

    // ========================================
    // GPU 資源
    // ========================================

    BufferPtr CreateHostBuffer(const DevicePtr& device, const void* data, uint64_t bytes, ResourceUsage usage, const char* name)
    {
        const uint64_t size = bytes == 0 ? 16 : bytes;
        BufferPtr buffer = device->CreateBuffer(BufferDesc(size, usage, true, name));
        if (buffer && bytes != 0)
        {
            void* mapped = buffer->Map(0u, bytes);
            if (mapped == nullptr)
            {
                return nullptr;
            }
            std::memcpy(mapped, data, static_cast<size_t>(bytes));
            buffer->Unmap();
        }
        return buffer;
    }

    template <typename T>
    BufferPtr CreateArrayBuffer(const DevicePtr& device, const Container::VariableArray<T>& array, ResourceUsage usage, const char* name)
    {
        return CreateHostBuffer(device, array.data(), static_cast<uint64_t>(array.size()) * sizeof(T), usage, name);
    }

    TexturePtr CreateTexture(const DevicePtr& device,
                             Format format,
                             uint32_t bytesPerPixel,
                             const void* guardPixel,
                             ResourceUsage usage,
                             const char* name)
    {
        TextureDesc desc;
        desc.Width = ScreenWidth;
        desc.Height = ScreenHeight;
        desc.MipLevels = 1;
        desc.ArraySize = 1;
        desc.TextureFormat = format;
        desc.Dimension = TextureDimension::Texture2D;
        desc.Usage = usage;
        desc.DebugName = name;
        TexturePtr texture = device->CreateTexture(desc);
        if (!texture)
        {
            return nullptr;
        }
        Container::VariableArray<uint8_t> pixels(static_cast<size_t>(ScreenWidth) * ScreenHeight * bytesPerPixel);
        for (size_t pixel = 0; pixel < static_cast<size_t>(ScreenWidth) * ScreenHeight; ++pixel)
        {
            std::memcpy(pixels.data() + pixel * bytesPerPixel, guardPixel, bytesPerPixel);
        }
        texture->Update(pixels.data(), ScreenWidth * bytesPerPixel, pixels.size());
        return texture;
    }

    struct Readback
    {
        Container::VariableArray<uint8_t> Albedo;   // RGBA8
        Container::VariableArray<uint16_t> Normal;  // RGBA16F
        Container::VariableArray<uint16_t> Velocity; // RG16F
        Container::VariableArray<float> Dump;       // 画素あたり 12 * 4 個
        bool bRecorded = false;
        bool bOk = false;
    };

    bool ReadTexture(const DevicePtr& device,
                     const TexturePtr& texture,
                     uint32_t bytesPerPixel,
                     Container::VariableArray<uint8_t>& out)
    {
        const uint64_t bytes = static_cast<uint64_t>(ScreenWidth) * ScreenHeight * bytesPerPixel;
        BufferPtr readback = device->CreateBuffer(BufferDesc(bytes, ResourceUsage::TransferDst, true, "VisibilityResolveReadback"));
        CommandListPtr commandList = device->CreateCommandList();
        if (!readback || !commandList)
        {
            return false;
        }
        commandList->Begin();
        commandList->TextureBarrier(texture, ResourceState::UnorderedAccess, ResourceState::CopySource, 0u, 0u, 0u, 0u);
        commandList->BufferBarrier(readback, ResourceState::Undefined, ResourceState::CopyDest, 0u, bytes);
        commandList->CopyTextureToBuffer(texture, readback, ScreenWidth, ScreenHeight, 0u, 0u, 0u);
        commandList->BufferBarrier(readback, ResourceState::CopyDest, ResourceState::HostRead, 0u, bytes);
        commandList->End();
        commandList->Submit(true);
        device->WaitIdle();
        const uint8_t* mapped = static_cast<const uint8_t*>(readback->Map(0u, bytes));
        if (mapped == nullptr)
        {
            return false;
        }
        out.assign(mapped, mapped + bytes);
        readback->Unmap();
        return true;
    }

    struct GpuScene
    {
        BufferPtr RecordTable;
        BufferPtr MaterialTable;
        BufferPtr MegaInstances;
        BufferPtr DrawInstances;
        TexturePtr IdTexture;
        // BDA で読む頂点・インデックス（記録のアドレスが指す）
        BufferPtr ProceduralVertices1;
        BufferPtr ProceduralIndices1;
        BufferPtr ProceduralVertices2;
        BufferPtr ProceduralIndices2;
        BufferPtr MegaVertices;
        BufferPtr MegaIndices;
        BufferPtr SkinnedCurrent;
        BufferPtr SkinnedPrevious;
        BufferPtr SkinnedIndices;
        VisibilityBuffer::RecordTable Records; // アドレスを書き込んだ記録の表
    };

    TexturePtr CreateIdTexture(const DevicePtr& device, const Container::VariableArray<uint32_t>& image)
    {
        TextureDesc desc;
        desc.Width = ScreenWidth;
        desc.Height = ScreenHeight;
        desc.MipLevels = 1;
        desc.ArraySize = 1;
        desc.TextureFormat = Format::R32_UINT;
        desc.Dimension = TextureDimension::Texture2D;
        desc.Usage = ResourceUsage::ShaderResource;
        desc.DebugName = "VisibilityResolveTestId";
        TexturePtr texture = device->CreateTexture(desc);
        if (texture)
        {
            texture->Update(image.data(), ScreenWidth * sizeof(uint32_t), image.size() * sizeof(uint32_t));
        }
        return texture;
    }

    // 記録のアドレス（頂点・インデックス・前の頂点）を、作ったバッファのデバイスアドレスで埋めた記録の表を作る
    bool BuildGpuScene(const DevicePtr& device, const Scene& scene, const Container::VariableArray<uint32_t>& idImage, GpuScene& gpu)
    {
        const ResourceUsage addressUsage =
            ResourceUsage::StorageBuffer | ResourceUsage::ShaderRead | ResourceUsage::BufferDeviceAddress;
        gpu.ProceduralVertices1 = CreateArrayBuffer(device, scene.ProceduralVertices1, addressUsage, "ResolveTestProcVertices1");
        gpu.ProceduralIndices1 = CreateArrayBuffer(device, scene.ProceduralIndices1, addressUsage, "ResolveTestProcIndices1");
        gpu.ProceduralVertices2 = CreateArrayBuffer(device, scene.ProceduralVertices2, addressUsage, "ResolveTestProcVertices2");
        gpu.ProceduralIndices2 = CreateArrayBuffer(device, scene.ProceduralIndices2, addressUsage, "ResolveTestProcIndices2");
        gpu.MegaVertices = CreateArrayBuffer(device, scene.MegaVertices, addressUsage, "ResolveTestMegaVertices");
        gpu.MegaIndices = CreateArrayBuffer(device, scene.MegaIndices, addressUsage, "ResolveTestMegaIndices");
        gpu.SkinnedCurrent = CreateArrayBuffer(device, scene.SkinnedCurrent, addressUsage, "ResolveTestSkinnedCurrent");
        gpu.SkinnedPrevious = CreateArrayBuffer(device, scene.SkinnedPrevious, addressUsage, "ResolveTestSkinnedPrevious");
        gpu.SkinnedIndices = CreateArrayBuffer(device, scene.SkinnedIndices, addressUsage, "ResolveTestSkinnedIndices");
        if (!gpu.ProceduralVertices1 || !gpu.ProceduralIndices1 || !gpu.ProceduralVertices2 || !gpu.ProceduralIndices2 ||
            !gpu.MegaVertices || !gpu.MegaIndices || !gpu.SkinnedCurrent || !gpu.SkinnedPrevious || !gpu.SkinnedIndices)
        {
            return false;
        }

        // 記録の表: 場面の記録に、種類ごとのアドレスを書き込む（番号は場面を作った順: 1 手続き、2 手続き 16bit、3 Mega、4・5 スキニング）
        for (uint32_t number = 1; number <= scene.Records.RecordCount(); ++number)
        {
            VisibilityBuffer::DrawRecord record = scene.Records.Data()[number];
            switch (number)
            {
            case 1:
                record.VertexAddress = gpu.ProceduralVertices1->GetDeviceAddress();
                record.IndexAddress = gpu.ProceduralIndices1->GetDeviceAddress();
                break;
            case 2:
                record.VertexAddress = gpu.ProceduralVertices2->GetDeviceAddress();
                record.IndexAddress = gpu.ProceduralIndices2->GetDeviceAddress();
                break;
            case 3:
                record.VertexAddress = gpu.MegaVertices->GetDeviceAddress();
                record.IndexAddress = gpu.MegaIndices->GetDeviceAddress();
                break;
            case 4:
                record.VertexAddress = gpu.SkinnedCurrent->GetDeviceAddress();
                record.PreviousVertexAddress = gpu.SkinnedPrevious->GetDeviceAddress();
                record.IndexAddress = gpu.SkinnedIndices->GetDeviceAddress();
                break;
            default:
                record.VertexAddress = gpu.SkinnedCurrent->GetDeviceAddress() + SkinnedBaseB * sizeof(Vertex);
                record.PreviousVertexAddress = gpu.SkinnedPrevious->GetDeviceAddress() + SkinnedBaseB * sizeof(Vertex);
                record.IndexAddress = gpu.SkinnedIndices->GetDeviceAddress();
                break;
            }
            gpu.Records.Add(record);
        }
        const ResourceUsage tableUsage = ResourceUsage::StorageBuffer | ResourceUsage::ShaderRead;
        gpu.RecordTable = CreateHostBuffer(device, gpu.Records.Data(), gpu.Records.SizeInBytes(), tableUsage, "ResolveTestRecords");
        gpu.MaterialTable = CreateArrayBuffer(device, scene.Materials, tableUsage, "ResolveTestMaterials");
        gpu.MegaInstances = CreateArrayBuffer(device, scene.MegaInstances, tableUsage, "ResolveTestMegaInstances");
        gpu.DrawInstances = CreateArrayBuffer(device, scene.DrawInstances, tableUsage, "ResolveTestDrawInstances");
        gpu.IdTexture = CreateIdTexture(device, idImage);
        return gpu.RecordTable && gpu.MaterialTable && gpu.MegaInstances && gpu.DrawInstances && gpu.IdTexture;
    }

    Readback RunResolve(const DevicePtr& device,
                        ShaderManager& shaderManager,
                        const GpuScene& gpu,
                        const CameraSet& cameras,
                        bool bDump,
                        bool bPreviousCamera,
                        uint64_t frameSerial)
    {
        Readback result;
        VisibilityResolve resolve;
        if (!resolve.Initialize(device.get(), &shaderManager, bDump))
        {
            std::cerr << TestName << " 幾何の解決を初期化できませんでした（dump=" << bDump << "）" << std::endl;
            return result;
        }

        const ResourceUsage outputUsage = ResourceUsage::ShaderResource | ResourceUsage::ShaderWrite | ResourceUsage::TransferSrc;
        const uint16_t halfGuardPixel[4] = {HalfGuard, HalfGuard, HalfGuard, HalfGuard};
        TexturePtr albedo = CreateTexture(device, Format::R8G8B8A8_UNORM, 4, AlbedoGuard, outputUsage, "ResolveTestAlbedo");
        TexturePtr normal = CreateTexture(device, Format::R16G16B16A16_FLOAT, 8, halfGuardPixel, outputUsage, "ResolveTestNormal");
        TexturePtr velocity = CreateTexture(device, Format::R16G16_FLOAT, 4, halfGuardPixel, outputUsage, "ResolveTestVelocity");
        const uint64_t dumpBytes =
            static_cast<uint64_t>(ScreenWidth) * ScreenHeight * VisibilityResolveGeometry::DUMP_STRIDE_BYTES;
        BufferPtr dump;
        if (bDump)
        {
            Container::VariableArray<float> guard(static_cast<size_t>(dumpBytes / sizeof(float)), -7.0f);
            dump = CreateHostBuffer(device, guard.data(), dumpBytes,
                                    ResourceUsage::StorageBuffer | ResourceUsage::ShaderRead | ResourceUsage::TransferDst,
                                    "ResolveTestDump");
        }
        CommandListPtr commandList = device->CreateCommandList();
        if (!albedo || !normal || !velocity || !commandList || (bDump && !dump))
        {
            std::cerr << TestName << " 出力の資源を作れませんでした" << std::endl;
            return result;
        }

        VisibilityResolveDispatch dispatch;
        dispatch.IdTexture = gpu.IdTexture;
        dispatch.RecordTable = gpu.RecordTable;
        dispatch.RecordTableBytes = gpu.Records.SizeInBytes();
        dispatch.MaterialTable = gpu.MaterialTable;
        dispatch.MaterialTableBytes = gpu.MaterialTable->GetSize();
        dispatch.MegaInstances = gpu.MegaInstances;
        dispatch.MegaInstancesBytes = gpu.MegaInstances->GetSize();
        dispatch.DrawInstances = gpu.DrawInstances;
        dispatch.DrawInstancesBytes = gpu.DrawInstances->GetSize();
        dispatch.Albedo = albedo;
        dispatch.Normal = normal;
        dispatch.Velocity = velocity;
        dispatch.Dump = dump;
        Viewport viewport;
        viewport.width = static_cast<float>(ScreenWidth);
        viewport.height = static_cast<float>(ScreenHeight);
        dispatch.Params = VisibilityResolveGeometry::BuildParams(cameras.Current,
                                                                 bPreviousCamera ? &cameras.Previous : nullptr,
                                                                 viewport,
                                                                 ScreenWidth,
                                                                 ScreenHeight,
                                                                 static_cast<uint32_t>(gpu.MaterialTable->GetSize() / sizeof(VisibilityBuffer::MaterialEntry)));

        resolve.BeginFrame(0, frameSerial);
        commandList->Begin();
        for (const TexturePtr& texture : {albedo, normal, velocity})
        {
            commandList->TextureBarrier(texture, ResourceState::ShaderResource, ResourceState::UnorderedAccess, 0u, 0u, 0u, 0u);
        }
        if (bDump)
        {
            commandList->BufferBarrier(dump, ResourceState::Undefined, ResourceState::UnorderedAccess, 0u, dumpBytes);
        }
        result.bRecorded = resolve.Record(commandList.get(), dispatch);
        if (bDump)
        {
            commandList->BufferBarrier(dump, ResourceState::UnorderedAccess, ResourceState::HostRead, 0u, dumpBytes);
        }
        commandList->End();
        commandList->Submit(true);
        device->WaitIdle();

        if (bDump)
        {
            const float* mapped = static_cast<const float*>(dump->Map(0u, dumpBytes));
            if (mapped == nullptr)
            {
                return result;
            }
            result.Dump.assign(mapped, mapped + dumpBytes / sizeof(float));
            dump->Unmap();
        }

        Container::VariableArray<uint8_t> bytes;
        if (!ReadTexture(device, albedo, 4, result.Albedo))
        {
            return result;
        }
        if (!ReadTexture(device, normal, 8, bytes))
        {
            return result;
        }
        result.Normal.resize(bytes.size() / 2);
        std::memcpy(result.Normal.data(), bytes.data(), bytes.size());
        if (!ReadTexture(device, velocity, 4, bytes))
        {
            return result;
        }
        result.Velocity.resize(bytes.size() / 2);
        std::memcpy(result.Velocity.data(), bytes.data(), bytes.size());
        result.bOk = true;
        return result;
    }

    // ========================================
    // 検査
    // ========================================

    bool NearlyEqual(double a, double b, double tolerance)
    {
        return std::fabs(a - b) <= tolerance;
    }

    struct Tolerances
    {
        double Barycentric = 3.0e-4;
        double Uv = 1.0e-3;
        double UvDerivative = 2.0e-4;
        double PositionDerivative = 1.5e-4;
        double Tangent = 4.0e-3;
        double Normal = 3.0e-3;  // fp16 の格納
        double Velocity = 3.0e-4; // fp16 の格納
        double PreviousClip = 1.0e-3;
    };

    struct CheckCounters
    {
        uint32_t CoveredPixels = 0;
        double MaxBarycentricError = 0.0;
        double MaxUvDerivativeError = 0.0;
        double MaxPositionDerivativeError = 0.0;
        double MaxNormalError = 0.0;
        double MaxVelocityError = 0.0;
        double MaxTangentError = 0.0;
        uint32_t PerRecordPixels[16] = {};
    };

    void CheckImages(const char* label,
                     const Scene& scene,
                     const Container::VariableArray<PixelReference>& references,
                     const Readback& readback,
                     bool bPreviousValid,
                     const Tolerances& tolerances,
                     CheckCounters& counters)
    {
        Expect(readback.bOk && readback.bRecorded, "解決を記録して、出力を読み戻せなければならない");
        if (!readback.bOk)
        {
            return;
        }
        uint32_t badAlbedo = 0;
        uint32_t badNormal = 0;
        uint32_t badVelocity = 0;
        uint32_t badGuard = 0;
        for (uint32_t y = 0; y < ScreenHeight; ++y)
        {
            for (uint32_t x = 0; x < ScreenWidth; ++x)
            {
                const size_t pixel = static_cast<size_t>(y) * ScreenWidth + x;
                const PixelReference& ref = references[pixel];
                const uint8_t* albedo = readback.Albedo.data() + pixel * 4;
                const uint16_t* normal = readback.Normal.data() + pixel * 4;
                const uint16_t* velocity = readback.Velocity.data() + pixel * 2;
                if (!ref.bCovered)
                {
                    // 書かれていない画素は見張りのまま
                    const bool bGuard = std::memcmp(albedo, AlbedoGuard, 4) == 0 && normal[0] == HalfGuard &&
                                        normal[1] == HalfGuard && normal[2] == HalfGuard && normal[3] == HalfGuard &&
                                        velocity[0] == HalfGuard && velocity[1] == HalfGuard;
                    if (!bGuard)
                    {
                        ++badGuard;
                    }
                    continue;
                }
                ++counters.CoveredPixels;
                const ReferenceTriangle& triangle = scene.References[ref.ReferenceIndex];
                ++counters.PerRecordPixels[triangle.RecordNumber];
                const VisibilityBuffer::MaterialEntry& material = scene.Materials[triangle.Material];
                bool bAlbedoOk = albedo[3] == 255;
                for (int channel = 0; channel < 3; ++channel)
                {
                    bAlbedoOk = bAlbedoOk && std::fabs(albedo[channel] / 255.0 - material.BaseColor[channel]) <= 1.0 / 255.0 + 1.0e-6;
                }
                if (!bAlbedoOk)
                {
                    ++badAlbedo;
                }
                const double normalError = std::max({std::fabs(HalfToFloat(normal[0]) - ref.Normal.X),
                                                     std::fabs(HalfToFloat(normal[1]) - ref.Normal.Y),
                                                     std::fabs(HalfToFloat(normal[2]) - ref.Normal.Z)});
                counters.MaxNormalError = std::max(counters.MaxNormalError, normalError);
                if (normalError > tolerances.Normal || HalfToFloat(normal[3]) != 0.0f)
                {
                    ++badNormal;
                }
                const double expectedVx = bPreviousValid ? ref.Velocity[0] : 0.0;
                const double expectedVy = bPreviousValid ? ref.Velocity[1] : 0.0;
                const double velocityError = std::max(std::fabs(HalfToFloat(velocity[0]) - expectedVx),
                                                      std::fabs(HalfToFloat(velocity[1]) - expectedVy));
                counters.MaxVelocityError = std::max(counters.MaxVelocityError, velocityError);
                if (velocityError > tolerances.Velocity)
                {
                    ++badVelocity;
                }
            }
        }
        if (badAlbedo != 0 || badNormal != 0 || badVelocity != 0 || badGuard != 0)
        {
            std::cerr << TestName << " " << label << " 不一致: Albedo=" << badAlbedo << " Normal=" << badNormal
                      << " Velocity=" << badVelocity << " 見張り(書かれてはならない画素)=" << badGuard << std::endl;
        }
        Expect(badAlbedo == 0, "Albedo は材質の基本色（α = 1）でなければならない");
        Expect(badNormal == 0, "Normal は補間して正規化したワールド法線でなければならない");
        Expect(badVelocity == 0, "Velocity は前のフレームの頂点から求めた (現在の NDC - 前の NDC) * 0.5 でなければならない");
        Expect(badGuard == 0, "空の画素・引けない ID の画素は何も書かれてはならない");
    }

    void CheckDump(const Scene& scene,
                   const Container::VariableArray<PixelReference>& references,
                   const Readback& readback,
                   const Tolerances& tolerances,
                   CheckCounters& counters)
    {
        Expect(!readback.Dump.empty(), "検証用の書き出しを読み戻せなければならない");
        if (readback.Dump.empty())
        {
            return;
        }
        uint32_t badBarycentric = 0;
        uint32_t badUv = 0;
        uint32_t badUvDerivative = 0;
        uint32_t badPositionDerivative = 0;
        uint32_t badTangent = 0;
        uint32_t badPreviousClip = 0;
        uint32_t badMisc = 0;
        uint32_t badUntouched = 0;
        constexpr uint32_t Stride = VisibilityResolveGeometry::DUMP_STRIDE_VEC4 * 4u;
        for (uint32_t y = 0; y < ScreenHeight; ++y)
        {
            for (uint32_t x = 0; x < ScreenWidth; ++x)
            {
                const size_t pixel = static_cast<size_t>(y) * ScreenWidth + x;
                const PixelReference& ref = references[pixel];
                const float* dump = readback.Dump.data() + pixel * Stride;
                if (!ref.bCovered)
                {
                    if (dump[0] != -7.0f || dump[Stride - 1] != -7.0f)
                    {
                        ++badUntouched;
                    }
                    continue;
                }
                const ReferenceTriangle& triangle = scene.References[ref.ReferenceIndex];
                double baryError = 0.0;
                for (int k = 0; k < 3; ++k)
                {
                    baryError = std::max(baryError, std::fabs(dump[k] - ref.Barycentric[k]));
                }
                counters.MaxBarycentricError = std::max(counters.MaxBarycentricError, baryError);
                if (baryError > tolerances.Barycentric || static_cast<uint32_t>(dump[3]) != triangle.Kind)
                {
                    ++badBarycentric;
                }
                const float* uv = dump + 1 * 4;
                if (!NearlyEqual(uv[0], ref.Uv[0], tolerances.Uv) || !NearlyEqual(uv[1], ref.Uv[1], tolerances.Uv))
                {
                    ++badUv;
                }
                const float* duvDy = dump + 2 * 4;
                const double uvDerivativeError = std::max({std::fabs(uv[2] - ref.DuvDx[0]), std::fabs(uv[3] - ref.DuvDx[1]),
                                                           std::fabs(duvDy[0] - ref.DuvDy[0]), std::fabs(duvDy[1] - ref.DuvDy[1])});
                counters.MaxUvDerivativeError = std::max(counters.MaxUvDerivativeError, uvDerivativeError);
                if (uvDerivativeError > tolerances.UvDerivative)
                {
                    ++badUvDerivative;
                }
                const float* hit = dump + 3 * 4;
                const float* dposDx = dump + 4 * 4;
                const float* dposDy = dump + 5 * 4;
                const double positionDerivativeError =
                    std::max({std::fabs(hit[0] - ref.Hit.X), std::fabs(hit[1] - ref.Hit.Y), std::fabs(hit[2] - ref.Hit.Z),
                              std::fabs(dposDx[0] - ref.DposDx.X), std::fabs(dposDx[1] - ref.DposDx.Y), std::fabs(dposDx[2] - ref.DposDx.Z),
                              std::fabs(dposDy[0] - ref.DposDy.X), std::fabs(dposDy[1] - ref.DposDy.Y), std::fabs(dposDy[2] - ref.DposDy.Z)});
                counters.MaxPositionDerivativeError = std::max(counters.MaxPositionDerivativeError, positionDerivativeError);
                if (positionDerivativeError > tolerances.PositionDerivative)
                {
                    ++badPositionDerivative;
                }
                const float* tangentT = dump + 6 * 4;
                const float* tangentB = dump + 7 * 4;
                const float* tangentN = dump + 8 * 4;
                const double tangentError =
                    std::max({std::fabs(tangentT[0] - ref.TangentT.X), std::fabs(tangentT[1] - ref.TangentT.Y), std::fabs(tangentT[2] - ref.TangentT.Z),
                              std::fabs(tangentB[0] - ref.TangentB.X), std::fabs(tangentB[1] - ref.TangentB.Y), std::fabs(tangentB[2] - ref.TangentB.Z),
                              std::fabs(tangentN[0] - ref.Normal.X), std::fabs(tangentN[1] - ref.Normal.Y), std::fabs(tangentN[2] - ref.Normal.Z)});
                counters.MaxTangentError = std::max(counters.MaxTangentError, tangentError);
                if (tangentError > tolerances.Tangent)
                {
                    ++badTangent;
                }
                const float* previousClip = dump + 9 * 4;
                if (!NearlyEqual(previousClip[0], ref.PreviousClipXYZ.X, tolerances.PreviousClip) ||
                    !NearlyEqual(previousClip[1], ref.PreviousClipXYZ.Y, tolerances.PreviousClip) ||
                    !NearlyEqual(previousClip[3], ref.PreviousClipW, tolerances.PreviousClip))
                {
                    ++badPreviousClip;
                }
                const float* misc = dump + 10 * 4;
                if (static_cast<uint32_t>(misc[2]) != triangle.Material || static_cast<uint32_t>(misc[3]) != triangle.TriangleIndex)
                {
                    ++badMisc;
                }
            }
        }
        if (badBarycentric + badUv + badUvDerivative + badPositionDerivative + badTangent + badPreviousClip + badMisc + badUntouched != 0)
        {
            std::cerr << TestName << " 書き出しの不一致: 重心座標=" << badBarycentric << " UV=" << badUv << " UVの微分=" << badUvDerivative
                      << " 位置の微分=" << badPositionDerivative << " 接線の基底=" << badTangent
                      << " 前のクリップ=" << badPreviousClip << " 材質・三角形の番号=" << badMisc
                      << " 触れてはならない画素=" << badUntouched << std::endl;
        }
        Expect(badBarycentric == 0, "透視の補正つきの重心座標（と描画の種類）が参照と一致しなければならない");
        Expect(badUv == 0, "補間した UV が参照と一致しなければならない");
        Expect(badUvDerivative == 0, "UV の解析的な微分（x・y）が参照の前進差分と一致しなければならない");
        Expect(badPositionDerivative == 0, "カメラ相対の位置と位置の解析的な微分が参照と一致しなければならない");
        Expect(badTangent == 0, "接線の基底が CalculateCotangentFrame と同じ規約の参照と一致しなければならない");
        Expect(badPreviousClip == 0, "前のフレームの頂点から求めた前のクリップ座標が参照と一致しなければならない");
        Expect(badMisc == 0, "材質の番号・三角形の番号が一致しなければならない");
        Expect(badUntouched == 0, "空の画素・引けない ID の画素は書き出しにも触れてはならない");
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
        if (!VisibilityResolveGeometry::IsSupported(device->GetCapabilities()))
        {
            return SkipGpuTest("ビジビリティバッファの解決に対応しない装置です");
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
            const Scene scene = BuildScene();

            // カメラ（今・前）。ラスタと同じ CameraViewConstants::BuildForDevice
            const CameraProxy currentProxy = MakeCamera(0.4, 1.0, 7.0, {0.0, 0.0, -1.0});
            const CameraProxy previousProxy = MakeCamera(0.55, 1.05, 7.2, {-0.02, -0.005, -1.0});
            CameraSet cameras;
            const float aspect = static_cast<float>(ScreenWidth) / static_cast<float>(ScreenHeight);
            cameras.Current = CameraViewConstants::BuildForDevice(currentProxy, aspect, device.get());
            cameras.Previous = CameraViewConstants::BuildForDevice(previousProxy, aspect, device.get());
            cameras.Current.CopyShaderViewProjection(cameras.CurrentViewProjection);
            cameras.Previous.CopyShaderViewProjection(cameras.PreviousViewProjection);

            // ID の画像と、画素ごとの参照: 各三角形を CPU でラスタライズする（三角形は画面の上で重ならない）
            Container::VariableArray<uint32_t> idImage(static_cast<size_t>(ScreenWidth) * ScreenHeight, VisibilityBuffer::EMPTY_ID);
            Container::VariableArray<PixelReference> references(static_cast<size_t>(ScreenWidth) * ScreenHeight);
            uint32_t overlapped = 0;
            for (uint32_t referenceIndex = 0; referenceIndex < scene.References.size(); ++referenceIndex)
            {
                const ReferenceTriangle& triangle = scene.References[referenceIndex];
                Vec4 clip[3];
                for (int k = 0; k < 3; ++k)
                {
                    clip[k] = ProjectToClip(cameras.CurrentViewProjection, triangle.Position[k]);
                }
                for (uint32_t y = 0; y < ScreenHeight; ++y)
                {
                    for (uint32_t x = 0; x < ScreenWidth; ++x)
                    {
                        double bary[3];
                        if (!RasterBarycentric(clip, PixelNdcX(x), PixelNdcY(y), bary))
                        {
                            continue;
                        }
                        // 画面上の重心座標（透視補正前）が全部 0 以上なら三角形の中。透視補正後の符号は同じ
                        if (bary[0] < 0.0 || bary[1] < 0.0 || bary[2] < 0.0)
                        {
                            continue;
                        }
                        const size_t pixel = static_cast<size_t>(y) * ScreenWidth + x;
                        if (references[pixel].bCovered)
                        {
                            ++overlapped;
                            continue;
                        }
                        uint32_t id = 0;
                        VisibilityBuffer::TryEncode(triangle.RecordNumber, triangle.TriangleIndex, id);
                        idImage[pixel] = id;
                        ComputePixelReference(scene, cameras, referenceIndex, x, y, true, references[pixel]);
                    }
                }
            }
            Expect(overlapped == 0, "場面の三角形は画面の上で重ならない（参照が一意になる）");

            // 引けない ID: 表に無い記録、三角形の番号が記録の三角形数以上
            uint32_t missingId = 0;
            VisibilityBuffer::TryEncode(99, 0, missingId);
            uint32_t outOfRangeTriangleId = 0;
            VisibilityBuffer::TryEncode(1, 5, outOfRangeTriangleId);
            // 空の場所へ置く（左上の隅は場面の外）
            Expect(!references[0].bCovered && !references[1].bCovered, "左上の隅は何も描かれない画素のはず");
            idImage[0] = missingId;
            idImage[1] = outOfRangeTriangleId;

            GpuScene gpu;
            if (!BuildGpuScene(device, scene, idImage, gpu))
            {
                std::cerr << TestName << " GPU の資源を作れませんでした" << std::endl;
                return 1;
            }

            CheckCounters counters;
            const Tolerances tolerances;

            // 製品の版: 出力の画像だけを検査する
            const Readback production = RunResolve(device, shaderManager, gpu, cameras, false, true, 1);
            CheckImages("製品の版", scene, references, production, true, tolerances, counters);

            // 検証用の版: 画像に加えて、画素ごとの中間の値を検査する
            const Readback dumped = RunResolve(device, shaderManager, gpu, cameras, true, true, 2);
            CheckCounters dumpedCounters;
            CheckImages("検証用の版", scene, references, dumped, true, tolerances, dumpedCounters);
            CheckDump(scene, references, dumped, tolerances, dumpedCounters);

            // 前のカメラが無いときは速度が 0
            const Readback withoutPrevious = RunResolve(device, shaderManager, gpu, cameras, false, false, 3);
            CheckCounters noPreviousCounters;
            CheckImages("前のカメラなし", scene, references, withoutPrevious, false, tolerances, noPreviousCounters);

            // この検査が何も確かめていない状態にならないよう、覆われた画素の数を見る
            Expect(counters.CoveredPixels >= 700, "覆われた画素が少なすぎる（場面が検査になっていない）");
            for (uint32_t record = 1; record <= scene.Records.RecordCount(); ++record)
            {
                Expect(counters.PerRecordPixels[record] >= 60, "記録ごとに十分な画素が覆われなければならない（2 体目のスキニングを含む）");
            }

            std::cout << TestName << " 覆われた画素=" << counters.CoveredPixels << " 記録ごと=[";
            for (uint32_t record = 1; record <= scene.Records.RecordCount(); ++record)
            {
                std::cout << counters.PerRecordPixels[record] << (record == scene.Records.RecordCount() ? "" : ",");
            }
            std::cout << "] 最大誤差: 重心座標=" << dumpedCounters.MaxBarycentricError
                      << " UVの微分=" << dumpedCounters.MaxUvDerivativeError
                      << " 位置の微分=" << dumpedCounters.MaxPositionDerivativeError
                      << " 接線=" << dumpedCounters.MaxTangentError << " 法線(fp16)=" << counters.MaxNormalError
                      << " 速度(fp16)=" << counters.MaxVelocityError << std::endl;

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
