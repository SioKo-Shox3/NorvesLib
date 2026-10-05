// ビジビリティバッファの幾何の解決（visbuffer_resolve.comp / VisibilityResolve）の GPU テスト。
// 合成した VisBuffer.Id の画像（100x60。8 で割れない部分タイルを含む）・描画の記録の表・頂点とインデックスのバッファ・
// 材質の表・インスタンスの表から、GBuffer の Albedo・Normal・Material・Velocity と、画素ごとの中間の値（重心座標・UV・解析的な微分・
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
//   材質ごとのタイルの一覧から走る形（MaterialTileClassify の引数と一覧で、材質ごとに 1 回ずつ DispatchIndirect）は、同じ ID の画像の
//   画面全体の直接 dispatch の結果と、Albedo・Material・Velocity と検証用の画素ごとの中間の値がビット単位で一致すること
//   （空のタイル・3 つ以上の材質が混じるタイル・部分タイル・材質の表の外の材質・引けない ID・画面の外の画素・引数の x の上限を小さくして
//    y へ広げた一覧を含む）。分類の上限以上の材質の画素は、一覧に入らないので解決されない（直接版は解決する。違う点として記録する）。
//   Normal は、材質ごとの形がテクスチャの無い材質に既定の平坦な法線テクスチャ（128, 128, 255 の 8bit）を束ねるので、直接版（幾何の法線）と
//   わずかに違う（接空間の (0.004, 0.004, 1) を傾ける。ラスタも同じ既定のテクスチャを使う）。許容を付けて比べる。
//   材質ごとの形が材質のテクスチャを束ねて Albedo（インスタンスの色 × アルベド。α はテクスチャの α）・Normal（法線マップ。2 チャンネルの
//   法線の Z の復元を含む）・Material（ORM の 1 枚、別々の枠、スカラー値の 1x1、既定）を書くことは、1x1 の単色のテクスチャ
//   （標本が微分・ミップに依らない）を材質ごとに変えて、CPU の期待値と照合する。
//   VT の要求（フィードバック）は、材質 0 のアルベドを sparse の BC7 にして、リングの要求のバッファへ書かれる要求を読み戻し、
//   CPU の参照（UV の微分から求めたミップ・タイル）と照合する。常駐のテクスチャは 4×4 の画素のうち位相の 1 画素だけが書き、
//   非常駐（ミップテイルだけを結ぶ）の領域は全画素が書く。パラメータ 0（VT でない材質）は何も書かない。
// Vulkan デバイスが無い環境、または解決に対応しない装置では 125（スキップ）を返す。
#include "Container/Containers.h"
#include "Rendering/CameraViewConstants.h"
#include "Rendering/MaterialTileClassifyPass.h"
#include "Rendering/SceneProxy.h"
#include "Rendering/ShaderManager.h"
#include "Rendering/SparsePagePool.h"
#include "Rendering/VirtualTextureFeedbackRing.h"
#include "Rendering/VirtualTextureRequestSet.h"
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
    constexpr uint8_t MaterialGuard[4] = {0x9A, 0xBC, 0xDE, 0xF0};
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
        /** @brief ラスタの fragObjectColor と同じ色（MegaGeometry は区間の材質の基本色、手続きメッシュはインスタンスの色、スキニングは 1） */
        float ObjectColor[3] = {1.0f, 1.0f, 1.0f};
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
                       const Vertex quad[4],
                       const float* instanceColor = nullptr)
    {
        for (uint32_t triangle = 0; triangle < 2; ++triangle)
        {
            ReferenceTriangle ref;
            for (int channel = 0; channel < 3; ++channel)
            {
                if (instanceColor)
                {
                    ref.ObjectColor[channel] = instanceColor[channel];
                }
                else if (kind == static_cast<uint32_t>(VisibilityBuffer::RecordKind::MegaGeometryCluster))
                {
                    ref.ObjectColor[channel] = scene.Materials[material].BaseColor[channel];
                }
            }
            for (uint32_t k = 0; k < 3; ++k)
            {
                const uint32_t corner = QuadIndices[triangle * 3 + k];
                ref.Position[k] = pos[corner];
                ref.Previous[k] = prev[corner];
                // ラスタ（gbuffer.vert・megageometry.vert・skinned_gbuffer.vert）は、頂点ごとに変換した法線を
                // 正規化してから補間する。補間してから正規化すると、非一様なスケールで向きがずれる
                ref.Normal[k] = Normalize(nrm[corner]);
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
            // インスタンスの色は材質の基本色と違う値にする（Albedo はインスタンスの色を使い、材質の基本色は使わない）
            const float instanceColor[4] = {0.30f, 0.60f, 0.90f, 1.0f};
            std::memcpy(data.ObjectColor, instanceColor, sizeof(instanceColor));
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
            AddReferences(scene, number, record.Kind, record.MaterialIndex, pos, prev, nrm, quad, instanceColor);
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
            const float decoyColor[4] = {0.05f, 0.05f, 0.05f, 1.0f};
            std::memcpy(decoy.ObjectColor, decoyColor, sizeof(decoyColor));
            const float identityRows[12] = {1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0};
            std::memcpy(decoy.NormalRows, identityRows, sizeof(identityRows));
            scene.DrawInstances.push_back(decoy); // 1

            DrawInstanceData data = {};
            FillWorldMatrices(data, world, Multiply(Translate(-30, 0, 0), Scale(1, 1, 1)));
            const float rows[12] = {1.0f, 0.1f, 0.0f, 0.0f, 0.0f, 0.9f, 0.05f, 0.0f, 0.0f, -0.1f, 1.05f, 0.0f};
            std::memcpy(data.NormalRows, rows, sizeof(rows));
            const float instanceColor[4] = {0.85f, 0.40f, 0.20f, 1.0f};
            std::memcpy(data.ObjectColor, instanceColor, sizeof(instanceColor));
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
            AddReferences(scene, number, record.Kind, record.MaterialIndex, pos, prev, nrm, quad, instanceColor);
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
        Container::VariableArray<uint8_t> Material; // RGBA8（金属度・粗さ・AO）
        Container::VariableArray<uint16_t> Normal;  // RGBA16F
        Container::VariableArray<uint16_t> Velocity; // RG16F
        Container::VariableArray<float> Dump;       // 画素あたり 12 * 4 個
        Container::VariableArray<uint32_t> TileArgs; // 材質ごとのタイルの形のとき、分類が作った引数の表
        uint32_t DumpPitch = ScreenWidth;           // 検証用の書き出しの 1 行の画素数（画面の幅。シェーダーは y * 幅 + x で書く）
        bool bClassified = true;                    // 材質ごとのタイルの形のとき、分類を記録できたか
        bool bRecorded = false;
        bool bOk = false;
    };

    // 材質ごとのタイルの一覧から走る形（分類 + 材質ごとの間接 dispatch）で解決するときの設定
    struct TileRunOptions
    {
        uint32_t MaxMaterials = 16;
        uint32_t GroupCountXLimit = MaterialTiles::MAX_GROUP_COUNT_X;
    };

    // VT の要求（フィードバック）を書かせるときの入力（リング）と結果（読み戻した要求）
    struct FeedbackRun
    {
        VirtualTextureFeedbackRing* Ring = nullptr;
        uint64_t Serial = 0;
        VirtualTextureRequestSet Requests;
        bool bBuffer = false;  // このフレームの要求のバッファを獲得できた
        bool bBarrier = false; // 読み戻し用のバリアを記録できた
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
                        uint64_t frameSerial,
                        const TileRunOptions* tiles = nullptr,
                        uint32_t width = ScreenWidth,
                        uint32_t height = ScreenHeight,
                        const Container::VariableArray<VisibilityResolveMaterial>* materials = nullptr,
                        FeedbackRun* feedback = nullptr)
    {
        Readback result;
        result.DumpPitch = width;
        VisibilityResolve resolve;
        if (!resolve.Initialize(device.get(), &shaderManager, bDump, tiles != nullptr))
        {
            std::cerr << TestName << " 幾何の解決を初期化できませんでした（dump=" << bDump << "）" << std::endl;
            return result;
        }

        const ResourceUsage outputUsage = ResourceUsage::ShaderResource | ResourceUsage::ShaderWrite | ResourceUsage::TransferSrc;
        const uint16_t halfGuardPixel[4] = {HalfGuard, HalfGuard, HalfGuard, HalfGuard};
        TexturePtr albedo = CreateTexture(device, Format::R8G8B8A8_UNORM, 4, AlbedoGuard, outputUsage, "ResolveTestAlbedo");
        TexturePtr normal = CreateTexture(device, Format::R16G16B16A16_FLOAT, 8, halfGuardPixel, outputUsage, "ResolveTestNormal");
        TexturePtr velocity = CreateTexture(device, Format::R16G16_FLOAT, 4, halfGuardPixel, outputUsage, "ResolveTestVelocity");
        TexturePtr materialImage = CreateTexture(device, Format::R8G8B8A8_UNORM, 4, MaterialGuard, outputUsage, "ResolveTestMaterial");
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
        if (!albedo || !normal || !materialImage || !velocity || !commandList || (bDump && !dump))
        {
            std::cerr << TestName << " 出力の資源を作れませんでした" << std::endl;
            return result;
        }

        // 材質ごとのタイルの形: 分類の出力（引数は間接 dispatch の引数としても読むので IndirectBuffer の用途）
        MaterialTileClassify classify;
        MaterialTiles::Layout tileLayout;
        BufferPtr tileArgs;
        BufferPtr tileList;
        BufferPtr tileCursors;
        BufferPtr tileStats;
        if (tiles)
        {
            if (!classify.Initialize(device.get(), &shaderManager))
            {
                std::cerr << TestName << " 材質のタイルの分類を初期化できませんでした" << std::endl;
                return result;
            }
            tileLayout = MaterialTiles::ComputeLayout(width, height, tiles->MaxMaterials, MaterialTiles::MAX_MATERIALS_PER_TILE,
                                                      tiles->GroupCountXLimit);
            const ResourceUsage tileUsage = ResourceUsage::StorageBuffer | ResourceUsage::ShaderRead | ResourceUsage::TransferDst;
            tileArgs = device->CreateBuffer(
                BufferDesc(tileLayout.ArgsBytes(), tileUsage | ResourceUsage::IndirectBuffer, true, "ResolveTestTileArgs"));
            tileList = device->CreateBuffer(BufferDesc(tileLayout.ListBytes(), tileUsage, true, "ResolveTestTileList"));
            tileCursors = device->CreateBuffer(BufferDesc(tileLayout.CursorsBytes(), tileUsage, true, "ResolveTestTileCursors"));
            tileStats = device->CreateBuffer(BufferDesc(MaterialTiles::STATS_BYTES, tileUsage, true, "ResolveTestTileStats"));
            if (!tileLayout.IsValid() || !tileArgs || !tileList || !tileCursors || !tileStats)
            {
                std::cerr << TestName << " 材質のタイルの出力の資源を作れませんでした" << std::endl;
                return result;
            }
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
        dispatch.Material = materialImage;
        dispatch.Velocity = velocity;
        dispatch.Dump = dump;
        if (materials)
        {
            dispatch.Materials = *materials;
        }
        Viewport viewport;
        viewport.width = static_cast<float>(ScreenWidth);
        viewport.height = static_cast<float>(ScreenHeight);
        dispatch.Params = VisibilityResolveGeometry::BuildParams(cameras.Current,
                                                                 bPreviousCamera ? &cameras.Previous : nullptr,
                                                                 viewport,
                                                                 width,
                                                                 height,
                                                                 static_cast<uint32_t>(gpu.MaterialTable->GetSize() / sizeof(VisibilityBuffer::MaterialEntry)));
        dispatch.TileArgs = tileArgs;
        dispatch.TileList = tileList;
        if (feedback)
        {
            feedback->Ring->BeginFrame(feedback->Serial);
            dispatch.Feedback = feedback->Ring->GetCurrentBuffer();
            dispatch.FeedbackBytes = feedback->Ring->GetBufferBytes();
            feedback->bBuffer = dispatch.Feedback != nullptr;
        }

        resolve.BeginFrame(0, frameSerial);
        commandList->Begin();
        for (const TexturePtr& texture : {albedo, normal, materialImage, velocity})
        {
            commandList->TextureBarrier(texture, ResourceState::ShaderResource, ResourceState::UnorderedAccess, 0u, 0u, 0u, 0u);
        }
        if (bDump)
        {
            commandList->BufferBarrier(dump, ResourceState::Undefined, ResourceState::UnorderedAccess, 0u, dumpBytes);
        }
        if (tiles)
        {
            for (const BufferPtr& buffer : {tileArgs, tileList, tileCursors, tileStats})
            {
                commandList->BufferBarrier(buffer, ResourceState::Undefined, ResourceState::UnorderedAccess, 0u, buffer->GetSize());
            }
            MaterialTileClassifyDispatch classifyDispatch;
            classifyDispatch.IdTexture = gpu.IdTexture;
            classifyDispatch.RecordTable = gpu.RecordTable;
            classifyDispatch.RecordTableBytes = gpu.Records.SizeInBytes();
            classifyDispatch.Args = tileArgs;
            classifyDispatch.List = tileList;
            classifyDispatch.Cursors = tileCursors;
            classifyDispatch.Stats = tileStats;
            classifyDispatch.Width = width;
            classifyDispatch.Height = height;
            classifyDispatch.Layout = tileLayout;
            classify.BeginFrame(0, frameSerial);
            result.bClassified = classify.Record(commandList.get(), classifyDispatch);
            // 引数は間接 dispatch の引数と解決のシェーダーの読み取りの両方で読む（GenericRead はどちらも含む）
            commandList->BufferBarrier(tileArgs, ResourceState::UnorderedAccess, ResourceState::GenericRead, 0u, tileArgs->GetSize());
            commandList->BufferBarrier(tileList, ResourceState::UnorderedAccess, ResourceState::GenericRead, 0u, tileList->GetSize());
        }
        result.bRecorded = resolve.Record(commandList.get(), dispatch);
        if (feedback)
        {
            // 解決（計算シェーダー）の書き込みをホストの読み取りへ見せるバリア（フラグメント段と計算段の両方）
            feedback->bBarrier = feedback->Ring->RecordHostReadBarrier(*commandList);
        }
        if (bDump)
        {
            commandList->BufferBarrier(dump, ResourceState::UnorderedAccess, ResourceState::HostRead, 0u, dumpBytes);
        }
        if (tiles)
        {
            commandList->BufferBarrier(tileArgs, ResourceState::GenericRead, ResourceState::HostRead, 0u, tileArgs->GetSize());
        }
        commandList->End();
        commandList->Submit(true);
        device->WaitIdle();

        if (feedback)
        {
            // 書いたフレームから 2 フレーム以上経つと読み戻される。空回しのフレームは何も書かないので中止する
            feedback->Ring->CommitFrame(++feedback->Serial);
            for (int idle = 0; idle < 2; ++idle)
            {
                feedback->Ring->BeginFrame(feedback->Serial);
                feedback->Ring->AbortFrame();
            }
            feedback->Requests.Clear();
            feedback->Ring->TakeRequests(feedback->Requests);
        }

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
        if (tiles)
        {
            const uint32_t* mapped = static_cast<const uint32_t*>(tileArgs->Map(0u, tileArgs->GetSize()));
            if (mapped == nullptr)
            {
                return result;
            }
            result.TileArgs.assign(mapped, mapped + tileArgs->GetSize() / sizeof(uint32_t));
            tileArgs->Unmap();
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
        if (!ReadTexture(device, materialImage, 4, result.Material))
        {
            return result;
        }
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
        uint32_t badMaterial = 0;
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
                (void)material;
                // Albedo はインスタンスの色（MegaGeometry は材質の基本色、スキニングは 1）で α = 1（直接 dispatch はテクスチャを使わない）
                bool bAlbedoOk = albedo[3] == 255;
                for (int channel = 0; channel < 3; ++channel)
                {
                    bAlbedoOk = bAlbedoOk && std::fabs(albedo[channel] / 255.0 - triangle.ObjectColor[channel]) <= 1.0 / 255.0 + 1.0e-6;
                }
                if (!bAlbedoOk)
                {
                    ++badAlbedo;
                }
                // Material は材質の定数だけ（テクスチャも指定も無いので、金属度 0・粗さ 128/255・AO 1）
                const uint8_t* materialPixel = readback.Material.data() + pixel * 4;
                if (materialPixel[0] != 0 || materialPixel[1] != 128 || materialPixel[2] != 255)
                {
                    ++badMaterial;
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
        if (badAlbedo != 0 || badMaterial != 0 || badNormal != 0 || badVelocity != 0 || badGuard != 0)
        {
            std::cerr << TestName << " " << label << " 不一致: Albedo=" << badAlbedo << " Normal=" << badNormal
                      << " Velocity=" << badVelocity << " 見張り(書かれてはならない画素)=" << badGuard << std::endl;
        }
        Expect(badAlbedo == 0, "Albedo はインスタンスの色（α = 1）でなければならない");
        Expect(badMaterial == 0, "Material は材質の定数（金属度 0・粗さ 128/255・AO 1）でなければならない");
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

    // ========================================
    // 材質ごとのタイルの一覧から走る形（間接 dispatch）と、画面全体の直接 dispatch の一致
    // ========================================

    struct TileCase
    {
        const char* Name;
        uint32_t MaxMaterials;
        uint32_t GroupCountXLimit;
        uint32_t Width;
        uint32_t Height;
    };

    // 画面の中の画素 (x, y) の ID が解決できるか（表にある記録・三角形の番号が範囲内）と、その材質の番号
    bool TryGetMaterial(const VisibilityBuffer::RecordTable& records, uint32_t id, uint32_t& outMaterial)
    {
        const VisibilityBuffer::DrawRecord* record = nullptr;
        uint32_t triangleIndex = 0;
        if (!records.TryResolve(id, record, triangleIndex))
        {
            return false;
        }
        outMaterial = record->MaterialIndex;
        return true;
    }

    bool Contains(const Container::VariableArray<uint32_t>& values, uint32_t value)
    {
        for (const uint32_t existing : values)
        {
            if (existing == value)
            {
                return true;
            }
        }
        return false;
    }

    // ID の画像の CPU の分析（分類の参照）。画面は width x height
    struct TileAnalysis
    {
        // 画面の中に解決できる画素が 1 つも無いタイルの数
        uint32_t EmptyTiles = 0;
        // 1 つのタイルに出る、分類の上限未満の材質の種類の最大
        uint32_t MaxMaterialsInTile = 0;
        // 一覧に入る（タイル, 材質）の数
        uint32_t Entries = 0;
        // 画面の端の部分タイルにある、画面の中の解決できる画素の数
        uint32_t PartialTilePixels = 0;
        // 解決できるが、材質の番号が分類の上限以上で一覧に入らない画素の数
        uint32_t UnlistedPixels = 0;
        // 画面の外にあるが、解決できる ID が書かれている画素の数
        uint32_t OutsideIdPixels = 0;
    };

    TileAnalysis AnalyzeTiles(const VisibilityBuffer::RecordTable& records,
                              const Container::VariableArray<uint32_t>& ids,
                              uint32_t width,
                              uint32_t height,
                              uint32_t maxMaterials)
    {
        TileAnalysis analysis;
        const uint32_t tile = VisibilityResolveGeometry::TILE_SIZE;
        const uint32_t tilesX = (width + tile - 1) / tile;
        const uint32_t tilesY = (height + tile - 1) / tile;
        for (uint32_t tileY = 0; tileY < tilesY; ++tileY)
        {
            for (uint32_t tileX = 0; tileX < tilesX; ++tileX)
            {
                Container::VariableArray<uint32_t> materials;
                bool bAnyResolvable = false;
                for (uint32_t y = tileY * tile; y < tileY * tile + tile && y < ScreenHeight; ++y)
                {
                    for (uint32_t x = tileX * tile; x < tileX * tile + tile && x < ScreenWidth; ++x)
                    {
                        uint32_t material = 0;
                        if (!TryGetMaterial(records, ids[static_cast<size_t>(y) * ScreenWidth + x], material))
                        {
                            continue;
                        }
                        if (x >= width || y >= height)
                        {
                            ++analysis.OutsideIdPixels;
                            continue;
                        }
                        bAnyResolvable = true;
                        if (width % tile != 0 && tileX == tilesX - 1)
                        {
                            ++analysis.PartialTilePixels;
                        }
                        else if (height % tile != 0 && tileY == tilesY - 1)
                        {
                            ++analysis.PartialTilePixels;
                        }
                        if (material >= maxMaterials)
                        {
                            ++analysis.UnlistedPixels;
                        }
                        else if (!Contains(materials, material))
                        {
                            materials.push_back(material);
                        }
                    }
                }
                if (!bAnyResolvable)
                {
                    ++analysis.EmptyTiles;
                }
                analysis.Entries += static_cast<uint32_t>(materials.size());
                analysis.MaxMaterialsInTile = std::max(analysis.MaxMaterialsInTile, static_cast<uint32_t>(materials.size()));
            }
        }
        return analysis;
    }

    // 検証用の書き出しの画素の位置（画面の幅が行の長さ）。画面の外の画素は書き出しの枠が無いので false
    bool DumpPixelIndex(const Readback& readback, uint32_t x, uint32_t y, size_t& outIndex)
    {
        if (x >= readback.DumpPitch)
        {
            return false;
        }
        outIndex = static_cast<size_t>(y) * readback.DumpPitch + x;
        return true;
    }

    // 出力の見張りのままか（何も書かれていない）。検証用の書き出しがあれば、それも見張りのままか
    bool IsGuardPixel(const Readback& readback, uint32_t x, uint32_t y)
    {
        const size_t pixel = static_cast<size_t>(y) * ScreenWidth + x;
        const uint8_t* albedo = readback.Albedo.data() + pixel * 4;
        const uint16_t* normal = readback.Normal.data() + pixel * 4;
        const uint16_t* velocity = readback.Velocity.data() + pixel * 2;
        bool bGuard = std::memcmp(albedo, AlbedoGuard, 4) == 0 && normal[0] == HalfGuard && normal[1] == HalfGuard &&
                      normal[2] == HalfGuard && normal[3] == HalfGuard && velocity[0] == HalfGuard && velocity[1] == HalfGuard &&
                      std::memcmp(readback.Material.data() + pixel * 4, MaterialGuard, 4) == 0;
        size_t dumpPixel = 0;
        if (bGuard && !readback.Dump.empty() && DumpPixelIndex(readback, x, y, dumpPixel))
        {
            const float* dump = readback.Dump.data() + dumpPixel * VisibilityResolveGeometry::DUMP_STRIDE_VEC4 * 4u;
            for (uint32_t index = 0; index < VisibilityResolveGeometry::DUMP_STRIDE_VEC4 * 4u; ++index)
            {
                bGuard = bGuard && dump[index] == -7.0f;
            }
        }
        return bGuard;
    }

    // 材質ごとの形が、テクスチャの無い材質に既定の平坦な法線（128, 128, 255 の 8bit）を束ねたことによる法線のずれの許容
    // （接空間の (0.0039, 0.0039, 1) を T・B で傾ける。長い方が 1 の T・B なので、ずれは 0.0055 rad 未満）
    constexpr double DefaultFlatNormalTilt = 0.01;

    // 2 つの結果の画素が一致するか（Albedo・Material・Velocity と、あれば検証用の中間の値はビット単位で NaN の中身も含めて。
    // Normal は既定の平坦な法線の傾きぶんの許容つき）
    bool IsSamePixel(const Readback& a, const Readback& b, uint32_t x, uint32_t y)
    {
        const size_t pixel = static_cast<size_t>(y) * ScreenWidth + x;
        bool bSame = std::memcmp(a.Albedo.data() + pixel * 4, b.Albedo.data() + pixel * 4, 4) == 0 &&
                     std::memcmp(a.Material.data() + pixel * 4, b.Material.data() + pixel * 4, 4) == 0 &&
                     std::memcmp(a.Velocity.data() + pixel * 2, b.Velocity.data() + pixel * 2, 4) == 0;
        for (int channel = 0; channel < 3; ++channel)
        {
            bSame = bSame && std::fabs(HalfToFloat(a.Normal[pixel * 4 + channel]) - HalfToFloat(b.Normal[pixel * 4 + channel])) <=
                                 DefaultFlatNormalTilt;
        }
        size_t dumpPixel = 0;
        if (!a.Dump.empty() && !b.Dump.empty() && DumpPixelIndex(a, x, y, dumpPixel))
        {
            const size_t floatCount = VisibilityResolveGeometry::DUMP_STRIDE_VEC4 * 4u;
            bSame = bSame && std::memcmp(a.Dump.data() + dumpPixel * floatCount, b.Dump.data() + dumpPixel * floatCount,
                                         floatCount * sizeof(float)) == 0;
        }
        return bSame;
    }

    struct EquivalenceCounters
    {
        uint32_t Compared = 0;
        uint32_t Mismatched = 0;
        // 直接版は解決するが、材質ごとの形は（分類の上限以上の材質なので）解決しない画素
        uint32_t DesignDifferences = 0;
        uint32_t UnexpectedDifferences = 0;
        uint32_t GuardViolations = 0;
    };

    void CompareTileRun(const TileCase& testCase,
                        const char* variant,
                        const VisibilityBuffer::RecordTable& records,
                        const Container::VariableArray<uint32_t>& ids,
                        const Readback& direct,
                        const Readback& tiled,
                        EquivalenceCounters& counters)
    {
        Expect(direct.bOk && direct.bRecorded, "直接 dispatch の解決を記録して読み戻せなければならない");
        Expect(tiled.bOk && tiled.bRecorded && tiled.bClassified, "材質ごとの間接 dispatch の解決（と分類）を記録して読み戻せなければならない");
        if (!direct.bOk || !tiled.bOk)
        {
            return;
        }
        for (uint32_t y = 0; y < ScreenHeight; ++y)
        {
            for (uint32_t x = 0; x < ScreenWidth; ++x)
            {
                const size_t pixel = static_cast<size_t>(y) * ScreenWidth + x;
                uint32_t material = 0;
                const bool bInScreen = x < testCase.Width && y < testCase.Height;
                const bool bResolvable = bInScreen && TryGetMaterial(records, ids[pixel], material);
                if (!bResolvable)
                {
                    // 空・引けない ID・画面の外は、どちらも何も書かない
                    if (!IsGuardPixel(direct, x, y) || !IsGuardPixel(tiled, x, y))
                    {
                        ++counters.GuardViolations;
                    }
                    continue;
                }
                if (IsGuardPixel(direct, x, y))
                {
                    ++counters.UnexpectedDifferences; // 直接版が解決していない（検査が成り立たない）
                    continue;
                }
                if (material >= testCase.MaxMaterials)
                {
                    // 一覧に入らない材質は、材質ごとの形では解決されない（見張りのまま）
                    if (IsGuardPixel(tiled, x, y))
                    {
                        ++counters.DesignDifferences;
                    }
                    else
                    {
                        ++counters.UnexpectedDifferences;
                    }
                    continue;
                }
                ++counters.Compared;
                if (!IsSamePixel(direct, tiled, x, y))
                {
                    if (counters.Mismatched < 8)
                    {
                        std::cerr << TestName << " " << testCase.Name << " " << variant << " 不一致の画素 (" << x << "," << y
                                  << ") 材質=" << material << std::endl;
                    }
                    ++counters.Mismatched;
                }
            }
        }
    }

    TexturePtr Create1x1Texture(const DevicePtr& device, uint8_t r, uint8_t g, uint8_t b, uint8_t a, const char* name)
    {
        TextureDesc desc;
        desc.Width = 1;
        desc.Height = 1;
        desc.MipLevels = 1;
        desc.ArraySize = 1;
        desc.TextureFormat = Format::R8G8B8A8_UNORM;
        desc.Dimension = TextureDimension::Texture2D;
        desc.Usage = ResourceUsage::ShaderRead;
        desc.DebugName = name;
        TexturePtr texture = device->CreateTexture(desc);
        if (texture)
        {
            const uint8_t pixel[4] = {r, g, b, a};
            texture->Update(pixel, 4, 4);
        }
        return texture;
    }

    // ミップごとに色を変えた 256x256 のテクスチャ（レベル 0 = 赤、レベル 1 = 緑、それより粗いレベル = 青）。
    // 勾配（ミップの選択）を使わずに標本すると、粗いミップを引くべき画素で赤（レベル 0 だけが持つ成分）が出る
    TexturePtr CreateMipChainTexture(const DevicePtr& device)
    {
        constexpr uint32_t Size = 256;
        constexpr uint32_t Levels = 9;
        TextureDesc desc;
        desc.Width = Size;
        desc.Height = Size;
        desc.MipLevels = Levels;
        desc.ArraySize = 1;
        desc.TextureFormat = Format::R8G8B8A8_UNORM;
        desc.Dimension = TextureDimension::Texture2D;
        desc.Usage = ResourceUsage::ShaderRead;
        desc.DebugName = "ResolveTestMipChain";
        TexturePtr texture = device->CreateTexture(desc);
        if (!texture)
        {
            return nullptr;
        }
        for (uint32_t level = 0; level < Levels; ++level)
        {
            const uint32_t extent = Size >> level;
            const uint8_t color[4] = {static_cast<uint8_t>(level == 0 ? 255 : 0), static_cast<uint8_t>(level == 1 ? 255 : 0),
                                      static_cast<uint8_t>(level >= 2 ? 255 : 0), 255};
            Container::VariableArray<uint8_t> pixels(static_cast<size_t>(extent) * extent * 4);
            for (size_t pixel = 0; pixel < static_cast<size_t>(extent) * extent; ++pixel)
            {
                std::memcpy(pixels.data() + pixel * 4, color, 4);
            }
            texture->Update(pixels.data(), extent * 4, static_cast<uint32_t>(pixels.size()), level);
        }
        return texture;
    }

    // 材質ごとの形が材質のテクスチャを束ねて、Albedo・Normal・Material を書くことを確かめる。
    // 1x1 の単色のテクスチャ（標本が微分・ミップ・異方性に依らない）を材質ごとに変え、画素の CPU の期待値と照合する:
    //   材質 0: アルベド（α = 128）・法線（RGBA8。傾きあり）・ORM の 1 枚（金属度・粗さ・AO を 1 枚から）
    //   材質 1: アルベド・2 チャンネルの法線（Z はシェーダーが戻す）・金属度・粗さ・AO の別々の枠
    //   材質 2: テクスチャ無し。金属度・粗さのスカラー値（1x1 のテクスチャにする）
    //   材質 3: アルベドだけ、ミップごとに色を変えた 256x256（勾配からミップを選ぶこと。法線・Material は既定のテクスチャ）
    // 材質ごとに束ねるテクスチャが違うので、取り違える（別の材質のテクスチャを束ねる・枠をずらす・ORM の旗を外す）と落ちる。
    void RunMaterialTextureCase(const DevicePtr& device,
                                ShaderManager& shaderManager,
                                const Scene& baseScene,
                                const Container::VariableArray<uint32_t>& idImage,
                                const Container::VariableArray<PixelReference>& references,
                                const CameraSet& cameras)
    {
        Scene scene = baseScene;
        // 材質 1 の法線は 2 チャンネル（BC5 相当。Z は単位長から戻す）
        scene.Materials[1].Header[0] |= VisibilityBuffer::MATERIAL_FLAG_NORMAL_TWO_CHANNEL;
        GpuScene gpu;
        if (!BuildGpuScene(device, scene, idImage, gpu))
        {
            Expect(false, "材質のテクスチャの検査の GPU の資源を作れなければならない");
            return;
        }

        Container::VariableArray<VisibilityResolveMaterial> materials(4);
        materials[0].Albedo = Create1x1Texture(device, 200, 100, 50, 128, "ResolveTestM0Albedo");
        materials[0].Normal = Create1x1Texture(device, 192, 128, 255, 255, "ResolveTestM0Normal");
        materials[0].ORM = Create1x1Texture(device, 204, 77, 230, 255, "ResolveTestM0Orm");
        materials[1].Albedo = Create1x1Texture(device, 30, 220, 90, 255, "ResolveTestM1Albedo");
        materials[1].Normal = Create1x1Texture(device, 64, 160, 0, 255, "ResolveTestM1Normal");
        materials[1].Metallic = Create1x1Texture(device, 64, 0, 0, 255, "ResolveTestM1Metallic");
        materials[1].Roughness = Create1x1Texture(device, 191, 0, 0, 255, "ResolveTestM1Roughness");
        materials[1].AO = Create1x1Texture(device, 102, 0, 0, 255, "ResolveTestM1Ao");
        materials[2].MetallicConstant = 0.6f;
        materials[2].RoughnessConstant = 0.25f;
        materials[3].Albedo = CreateMipChainTexture(device);
        if (!materials[3].Albedo)
        {
            Expect(false, "ミップつきのテクスチャを作れなければならない");
            return;
        }
        for (size_t material = 0; material < 2; ++material)
        {
            const VisibilityResolveMaterial& input = materials[material];
            if (!input.Albedo || !input.Normal)
            {
                Expect(false, "材質のテクスチャの検査の 1x1 のテクスチャを作れなければならない");
                return;
            }
        }

        TileRunOptions options;
        const Readback readback =
            RunResolve(device, shaderManager, gpu, cameras, false, true, 21, &options, ScreenWidth, ScreenHeight, &materials);
        Expect(readback.bOk && readback.bRecorded && readback.bClassified, "材質のテクスチャつきの材質ごとの解決を記録して読み戻せなければならない");
        if (!readback.bOk)
        {
            return;
        }

        // 材質ごとの期待値（8bit の値 → 0..1）
        struct Expected
        {
            double AlbedoTexture[3];
            double Alpha;
            double TangentNormal[3];
            double Material[3]; // 金属度・粗さ・AO
        };
        auto unorm = [](int value) { return value / 255.0; };
        Expected expected[4] = {};
        expected[0] = {{unorm(200), unorm(100), unorm(50)}, unorm(128),
                       {unorm(192) * 2.0 - 1.0, unorm(128) * 2.0 - 1.0, 1.0}, {unorm(230), unorm(77), unorm(204)}};
        {
            const double nx = unorm(64) * 2.0 - 1.0;
            const double ny = unorm(160) * 2.0 - 1.0;
            expected[1] = {{unorm(30), unorm(220), unorm(90)}, 1.0, {nx, ny, std::sqrt(std::max(1.0 - nx * nx - ny * ny, 0.0))},
                           {unorm(64), unorm(191), unorm(102)}};
        }
        // 材質 2 のスカラー値は 8bit に丸めた 1x1 のテクスチャの値（0.6 → 153、0.25 → 64）
        expected[2] = {{1.0, 1.0, 1.0}, 1.0, {unorm(128) * 2.0 - 1.0, unorm(128) * 2.0 - 1.0, 1.0}, {unorm(153), unorm(64), 1.0}};
        expected[3] = {{1.0, 1.0, 1.0}, 1.0, {unorm(128) * 2.0 - 1.0, unorm(128) * 2.0 - 1.0, 1.0}, {0.0, unorm(128), 1.0}};

        uint32_t checked = 0;
        uint32_t mipChecked = 0;
        uint32_t badMip = 0;
        uint32_t badAlbedo = 0;
        uint32_t badMaterial = 0;
        uint32_t badNormal = 0;
        double maxNormalError = 0.0;
        uint32_t perMaterial[4] = {};
        for (uint32_t y = 0; y < ScreenHeight; ++y)
        {
            for (uint32_t x = 0; x < ScreenWidth; ++x)
            {
                const size_t pixel = static_cast<size_t>(y) * ScreenWidth + x;
                const PixelReference& ref = references[pixel];
                if (!ref.bCovered)
                {
                    continue;
                }
                const ReferenceTriangle& triangle = scene.References[ref.ReferenceIndex];
                if (triangle.Material >= 4)
                {
                    continue;
                }
                ++checked;
                ++perMaterial[triangle.Material];
                const Expected& want = expected[triangle.Material];

                // Albedo = インスタンスの色 × アルベド（8bit に丸めて ±1 の許容）、α = アルベドのテクスチャの α
                const uint8_t* albedo = readback.Albedo.data() + pixel * 4;
                bool bAlbedoOk = std::fabs(albedo[3] / 255.0 - want.Alpha) <= 1.0 / 255.0 + 1.0e-6;
                if (triangle.Material == 3)
                {
                    // ミップごとに色の違うテクスチャ。CPU の UV の微分から、異方性で下がる前のミップ（log2 の最大の辺）が 3.5 以上の画素は、
                    // 異方性の上限 4 を引いてもミップが 1.5 以上で、レベル 0 が混ざらない（赤が 0）はず。勾配を使わず
                    // レベル 0 を引くと、赤が出る
                    const double texelsPerPixelX = 256.0 * std::hypot(ref.DuvDx[0], ref.DuvDx[1]);
                    const double texelsPerPixelY = 256.0 * std::hypot(ref.DuvDy[0], ref.DuvDy[1]);
                    const double pMax = std::max(texelsPerPixelX, texelsPerPixelY);
                    if (pMax > 0.0 && std::log2(pMax) >= 3.5)
                    {
                        ++mipChecked;
                        if (albedo[0] > 1)
                        {
                            ++badMip;
                        }
                    }
                }
                else
                {
                    for (int channel = 0; channel < 3; ++channel)
                    {
                        const double value = triangle.ObjectColor[channel] * want.AlbedoTexture[channel];
                        bAlbedoOk = bAlbedoOk && std::fabs(albedo[channel] / 255.0 - value) <= 1.0 / 255.0 + 1.0e-6;
                    }
                }
                if (!bAlbedoOk)
                {
                    ++badAlbedo;
                }

                const uint8_t* materialPixel = readback.Material.data() + pixel * 4;
                bool bMaterialOk = true;
                for (int channel = 0; channel < 3; ++channel)
                {
                    bMaterialOk = bMaterialOk && std::fabs(materialPixel[channel] / 255.0 - want.Material[channel]) <= 1.0 / 255.0 + 1.0e-6;
                }
                if (!bMaterialOk)
                {
                    ++badMaterial;
                }

                // 法線 = 正規化(T * x + B * y + N * z)（接線の基底は幾何の解決の参照。法線マップは POM の前後に依らない 1x1）
                double nx = ref.TangentT.X * want.TangentNormal[0] + ref.TangentB.X * want.TangentNormal[1] + ref.Normal.X * want.TangentNormal[2];
                double ny = ref.TangentT.Y * want.TangentNormal[0] + ref.TangentB.Y * want.TangentNormal[1] + ref.Normal.Y * want.TangentNormal[2];
                double nz = ref.TangentT.Z * want.TangentNormal[0] + ref.TangentB.Z * want.TangentNormal[1] + ref.Normal.Z * want.TangentNormal[2];
                const double length = std::sqrt(nx * nx + ny * ny + nz * nz);
                nx /= length;
                ny /= length;
                nz /= length;
                const uint16_t* normal = readback.Normal.data() + pixel * 4;
                const double normalError = std::max({std::fabs(HalfToFloat(normal[0]) - nx), std::fabs(HalfToFloat(normal[1]) - ny),
                                                     std::fabs(HalfToFloat(normal[2]) - nz)});
                maxNormalError = std::max(maxNormalError, normalError);
                if (normalError > 8.0e-3)
                {
                    ++badNormal;
                }
            }
        }
        std::cout << TestName << " 材質のテクスチャ: 検査した画素=" << checked << " 材質ごと=[" << perMaterial[0] << "," << perMaterial[1]
                  << "," << perMaterial[2] << "," << perMaterial[3] << "] 法線の最大誤差=" << maxNormalError << std::endl;
        for (uint32_t material = 0; material < 4; ++material)
        {
            Expect(perMaterial[material] >= 20, "材質のテクスチャの検査は、4 つの材質すべてで十分な画素を確かめなければならない");
        }
        std::cout << TestName << " ミップの検査: 粗いミップを引くべき画素=" << mipChecked << " レベル 0 が混ざった画素=" << badMip << std::endl;
        Expect(mipChecked >= 20, "ミップの検査は、粗いミップを引くべき画素を十分に確かめなければならない");
        Expect(badMip == 0, "材質のテクスチャは、三角形から求めた微分で選んだミップで標本されなければならない（レベル 0 を引いてはならない）");
        Expect(badAlbedo == 0, "Albedo はインスタンスの色 × アルベドのテクスチャで、α はテクスチャの α でなければならない");
        Expect(badMaterial == 0, "Material は ORM の 1 枚・別々の枠・スカラー値の 1x1・既定のテクスチャの値でなければならない");
        Expect(badNormal == 0, "Normal は法線マップ（2 チャンネルの Z の復元を含む）を接線の基底で変換した値でなければならない");
    }

    // ========================================
    // VT の要求（フィードバック）
    // ========================================

    constexpr uint32_t FeedbackTextureIndex = 5u;
    constexpr uint32_t FeedbackTextureSize = 1024u;
    constexpr uint32_t FeedbackMipLevels = 11u;
    // 要求のタイルの大きさ（texel）。sparse の実際のタイルは 256 だが、材質のパラメータが持つ大きさをシェーダーはそのまま使うので、
    // 小さな値にして画素ごとのタイルの座標を散らす（この検査が確かめるのは、パラメータの詰め方・位相・ミップとタイルの式）
    constexpr uint32_t FeedbackTileSize = 8u;
    // 場面の UV を縮める倍率。材質 0 の画素の欲しいミップが 0〜2（ミップテイルはミップ 3 から）に収まり、
    // ミップテイルだけを結んだテクスチャでは全画素が非常駐になる
    constexpr double FeedbackUvScale = 1.0 / 40.0;

    void ScaleVertexUv(Container::VariableArray<Vertex>& vertices, double factor)
    {
        for (Vertex& vertex : vertices)
        {
            vertex.Uv[0] = static_cast<float>(vertex.Uv[0] * factor);
            vertex.Uv[1] = static_cast<float>(vertex.Uv[1] * factor);
        }
    }

    struct ExpectedRequest
    {
        uint32_t Mip = 0;
        uint32_t X = 0;
        uint32_t Y = 0;
    };

    // 画素の欲しいミップとタイル。ミップはシェーダーの VisQueryLodFromGradient（異方性の標本の数は floor(Pmax / Pmin)）、タイルは
    // WriteVirtualTextureFeedbackAtPixel の式。outLod にはミップを切り捨てる前の値を返す
    ExpectedRequest ExpectedRequestForPixel(const PixelReference& ref, double& outLod)
    {
        const double lengthX = std::hypot(ref.DuvDx[0], ref.DuvDx[1]) * FeedbackTextureSize;
        const double lengthY = std::hypot(ref.DuvDy[0], ref.DuvDy[1]) * FeedbackTextureSize;
        const double pMax = std::max(lengthX, lengthY);
        const double pMin = std::min(lengthX, lengthY);
        double lod = 0.0;
        if (pMax > 0.0)
        {
            const double ratio = pMin > 0.0 ? std::min(std::max(std::floor(pMax / pMin), 1.0), 4.0) : 4.0;
            lod = std::min(std::max(std::log2(pMax / ratio), 0.0), static_cast<double>(FeedbackMipLevels - 1));
        }
        outLod = lod;
        ExpectedRequest request;
        request.Mip = static_cast<uint32_t>(std::floor(lod));
        const uint32_t mipSize = std::max(FeedbackTextureSize >> request.Mip, 1u);
        const double u = ref.Uv[0] - std::floor(ref.Uv[0]);
        const double v = ref.Uv[1] - std::floor(ref.Uv[1]);
        const uint32_t texelX = std::min(static_cast<uint32_t>(u * mipSize), mipSize - 1u);
        const uint32_t texelY = std::min(static_cast<uint32_t>(v * mipSize), mipSize - 1u);
        request.X = std::min(texelX / FeedbackTileSize, 255u);
        request.Y = std::min(texelY / FeedbackTileSize, 255u);
        return request;
    }

    // sparse の BC7（1024x1024、11 ミップ。タイルは 256 texel でミップ 0〜2 がタイル、ミップ 3 以降がミップテイル）。
    // bResident ならミップ 0〜2 の全タイルとミップテイルを結び、そうでなければミップテイルだけを結ぶ（ミップ 0〜2 は非常駐）
    TexturePtr CreateSparseFeedbackTexture(const DevicePtr& device,
                                           SparsePagePool& pool,
                                           Container::VariableArray<SparsePagePool::PageLease>& leases,
                                           bool bResident,
                                           const char* name)
    {
        TextureDesc desc;
        desc.Width = FeedbackTextureSize;
        desc.Height = FeedbackTextureSize;
        desc.MipLevels = FeedbackMipLevels;
        desc.TextureFormat = Format::BC7_UNORM;
        desc.Usage = ResourceUsage::ShaderRead | ResourceUsage::TransferDst;
        desc.bSparse = true;
        desc.DebugName = name;
        TexturePtr texture = device->CreateTexture(desc);
        SparseTextureInfo info;
        if (!texture || !texture->GetSparseInfo(info) || info.TileWidth != 256 || info.TileHeight != 256 || info.MipTailFirstLevel != 3)
        {
            return nullptr;
        }

        SparseBindRequest bindRequest;
        if (bResident)
        {
            for (uint32_t mip = 0; mip < 3u; ++mip)
            {
                for (uint32_t tileY = 0; tileY < info.TilesX[mip]; ++tileY)
                {
                    for (uint32_t tileX = 0; tileX < info.TilesX[mip]; ++tileX)
                    {
                        leases.push_back(pool.Acquire());
                        if (!leases.back().IsValid())
                        {
                            return nullptr;
                        }
                        SparseTileBind tile;
                        tile.Texture = texture.get();
                        tile.MipLevel = mip;
                        tile.TileX = tileX;
                        tile.TileY = tileY;
                        tile.Page = leases.back().GetPage();
                        bindRequest.Tiles.push_back(tile);
                    }
                }
            }
        }
        const uint32_t tailPages = static_cast<uint32_t>((info.MipTailSize + SparsePageSizeBytes - 1) / SparsePageSizeBytes);
        for (uint32_t page = 0; page < tailPages; ++page)
        {
            leases.push_back(pool.Acquire());
            if (!leases.back().IsValid())
            {
                return nullptr;
            }
            SparseMipTailBind tail;
            tail.Texture = texture.get();
            tail.PageIndex = page;
            tail.Page = leases.back().GetPage();
            bindRequest.MipTails.push_back(tail);
        }
        if (!device->BindSparse(bindRequest))
        {
            return nullptr;
        }
        CommandListPtr transition = device->CreateCommandList();
        if (!transition)
        {
            return nullptr;
        }
        transition->Begin();
        transition->TextureBarrier(texture, ResourceState::Undefined, ResourceState::ShaderResource);
        transition->End();
        transition->Submit(true);
        device->WaitIdle();
        return texture;
    }

    // 読み戻した要求が、期待の画素ごとの要求と合うことを確かめる。
    // 件数の合計は厳密に一致（書いた画素の数 = 要求の件数の合計）、タイルは CPU の参照と一致する（浮動小数の境界で
    // 隣のミップ・タイルに割れる画素を許して 9 割以上）
    void CheckFeedbackRequests(const char* label,
                               const VirtualTextureRequestSet& set,
                               const Container::VariableArray<ExpectedRequest>& expected)
    {
        uint32_t totalHits = 0;
        uint32_t matchedHits = 0;
        bool bOtherTexture = false;
        for (uint32_t textureIndex : set.GetTextureIndices())
        {
            if (textureIndex != FeedbackTextureIndex)
            {
                bOtherTexture = true;
                continue;
            }
            for (const VirtualTextureTileRequest& request : set.GetRequests(textureIndex))
            {
                totalHits += request.HitCount;
                uint32_t expectedCount = 0;
                for (const ExpectedRequest& candidate : expected)
                {
                    if (candidate.Mip == request.Mip && candidate.X == request.X && candidate.Y == request.Y)
                    {
                        ++expectedCount;
                    }
                }
                matchedHits += std::min(request.HitCount, expectedCount);
            }
        }
        std::cout << TestName << " VT のフィードバック " << label << ": 期待の画素=" << expected.size() << " 要求の件数の合計=" << totalHits
                  << " タイルが参照と一致した件数=" << matchedHits << " タイルの種類=" << set.GetRequestCount() << std::endl;
        Expect(!bOtherTexture, "要求のテクスチャの番号は材質のパラメータの番号だけでなければならない");
        Expect(totalHits == expected.size(), "要求の件数の合計は、書くべき画素の数と同じでなければならない");
        Expect(matchedHits * 10u >= totalHits * 9u, "要求のミップ・タイルは UV の微分から求めた参照と一致しなければならない");
        Expect(set.GetOverflowCount() == 0, "capacity に収まる要求で溢れてはならない");
    }

    // 材質ごとの形が VT の要求を書くことを確かめる（材質 0 のアルベドを sparse にする）:
    //   常駐のテクスチャ: 4×4 の画素のうち位相の 1 画素だけが書く（位相 0・5・15 で、書く画素の数と位置が変わる）
    //   非常駐（ミップテイルだけ）: 巡回によらず全画素が書く
    //   パラメータ 0: 非常駐でも何も書かない
    void RunVirtualTextureFeedbackCase(const DevicePtr& device,
                                       ShaderManager& shaderManager,
                                       const Scene& baseScene,
                                       const Container::VariableArray<uint32_t>& idImage,
                                       const Container::VariableArray<PixelReference>& baseReferences,
                                       const CameraSet& cameras)
    {
        const auto& capabilities = device->GetCapabilities();
        if (!capabilities.SupportsVirtualTextureFeedback() || !capabilities.Sparse.bSparseBinding ||
            !capabilities.Sparse.bResidencyImage2D || !capabilities.bTextureCompressionBC)
        {
            std::cout << TestName << " VT のフィードバックの検査をスキップ: この装置は sparse の VT のフィードバックに対応しない" << std::endl;
            return;
        }

        // UV を縮めた場面と、その画素ごとの参照
        Scene scene = baseScene;
        ScaleVertexUv(scene.ProceduralVertices1, FeedbackUvScale);
        ScaleVertexUv(scene.ProceduralVertices2, FeedbackUvScale);
        ScaleVertexUv(scene.MegaVertices, FeedbackUvScale);
        ScaleVertexUv(scene.SkinnedCurrent, FeedbackUvScale);
        ScaleVertexUv(scene.SkinnedPrevious, FeedbackUvScale);
        for (ReferenceTriangle& triangle : scene.References)
        {
            for (int corner = 0; corner < 3; ++corner)
            {
                triangle.Uv[corner][0] *= FeedbackUvScale;
                triangle.Uv[corner][1] *= FeedbackUvScale;
            }
        }
        Container::VariableArray<PixelReference> references(baseReferences.size());
        for (uint32_t y = 0; y < ScreenHeight; ++y)
        {
            for (uint32_t x = 0; x < ScreenWidth; ++x)
            {
                const size_t pixel = static_cast<size_t>(y) * ScreenWidth + x;
                if (baseReferences[pixel].bCovered)
                {
                    ComputePixelReference(scene, cameras, baseReferences[pixel].ReferenceIndex, x, y, true, references[pixel]);
                }
            }
        }

        // 材質 0 の画素: 位相ごとの期待の要求。欲しいミップが 0〜2 に収まっていること（この検査が非常駐の領域を確かめる前提）
        Container::VariableArray<ExpectedRequest> allPixels;
        Container::VariableArray<ExpectedRequest> phasePixels[16];
        double minLod = 1.0e9;
        double maxLod = -1.0e9;
        for (uint32_t y = 0; y < ScreenHeight; ++y)
        {
            for (uint32_t x = 0; x < ScreenWidth; ++x)
            {
                const PixelReference& ref = references[static_cast<size_t>(y) * ScreenWidth + x];
                if (!ref.bCovered || scene.References[ref.ReferenceIndex].Material != 0)
                {
                    continue;
                }
                double lod = 0.0;
                const ExpectedRequest request = ExpectedRequestForPixel(ref, lod);
                minLod = std::min(minLod, lod);
                maxLod = std::max(maxLod, lod);
                allPixels.push_back(request);
                phasePixels[(y & 3u) * 4u + (x & 3u)].push_back(request);
            }
        }
        std::cout << TestName << " VT のフィードバック: 材質 0 の画素=" << allPixels.size() << " 欲しいミップの範囲=[" << minLod << ", "
                  << maxLod << "]" << std::endl;
        Expect(allPixels.size() >= 100, "VT の検査は、材質 0 の画素を十分に確かめなければならない");
        Expect(maxLod < 2.8, "欲しいミップは、ミップテイル（ミップ 3 から）の外に収まらなければならない");
        for (uint32_t phase : {0u, 5u, 15u})
        {
            Expect(phasePixels[phase].size() >= 3, "位相ごとに十分な画素がなければならない");
        }

        GpuScene gpu;
        if (!BuildGpuScene(device, scene, idImage, gpu))
        {
            Expect(false, "VT の検査の GPU の資源を作れなければならない");
            return;
        }

        // 先に宣言したものが後に破棄される（ページを返す前にテクスチャを破棄する）
        SparsePagePool pool(device, 48 * SparsePageSizeBytes);
        Container::VariableArray<SparsePagePool::PageLease> leases;
        TexturePtr residentTexture = CreateSparseFeedbackTexture(device, pool, leases, true, "ResolveTestVtResident");
        TexturePtr tailOnlyTexture = CreateSparseFeedbackTexture(device, pool, leases, false, "ResolveTestVtTailOnly");
        if (!residentTexture || !tailOnlyTexture)
        {
            Expect(false, "sparse の BC7 テクスチャを作って結べなければならない");
            return;
        }

        VirtualTextureFeedbackRing::Config ringConfig;
        ringConfig.Capacity = 1024u;
        VirtualTextureFeedbackRing ring(device, ringConfig);
        Expect(ring.SetEnabled(true), "要求のバッファのリングを有効にできなければならない");
        FeedbackRun feedback;
        feedback.Ring = &ring;

        uint64_t frameSerial = 40;
        auto run = [&](const TexturePtr& texture, bool bParamEnabled, uint32_t phase) {
            Container::VariableArray<VisibilityResolveMaterial> materials(1);
            materials[0].Albedo = texture;
            materials[0].FeedbackAlbedo =
                bParamEnabled ? VirtualTextureFeedback::PackMaterialParam(FeedbackTextureIndex, FeedbackTileSize, FeedbackTileSize, phase)
                              : 0u;
            Expect(!bParamEnabled || materials[0].FeedbackAlbedo != 0u, "材質のパラメータを詰められなければならない");
            TileRunOptions options;
            const Readback readback = RunResolve(device, shaderManager, gpu, cameras, false, true, ++frameSerial, &options, ScreenWidth,
                                                 ScreenHeight, &materials, &feedback);
            Expect(readback.bOk && readback.bRecorded && readback.bClassified, "VT の材質つきの材質ごとの解決を記録して読み戻せなければならない");
            Expect(feedback.bBuffer && feedback.bBarrier, "このフレームの要求のバッファを獲得して、読み戻し用のバリアを記録できなければならない");
        };

        // 常駐: 位相の 1 画素だけが書く
        struct PhaseCase
        {
            uint32_t Phase;
            const char* Label;
        };
        const PhaseCase phaseCases[] = {{0u, "常駐 位相=0"}, {5u, "常駐 位相=5"}, {15u, "常駐 位相=15"}};
        for (const PhaseCase& phaseCase : phaseCases)
        {
            run(residentTexture, true, phaseCase.Phase);
            CheckFeedbackRequests(phaseCase.Label, feedback.Requests, phasePixels[phaseCase.Phase]);
        }

        // 非常駐: 巡回によらず全画素が書く（位相 5 でも材質 0 の全画素）
        run(tailOnlyTexture, true, 5u);
        CheckFeedbackRequests("非常駐 位相=5", feedback.Requests, allPixels);

        // パラメータ 0（VT でない材質）は、非常駐のテクスチャでも何も書かない
        run(tailOnlyTexture, false, 5u);
        CheckFeedbackRequests("パラメータ 0", feedback.Requests, Container::VariableArray<ExpectedRequest>());

        device->WaitIdle();
    }

    void RunTileEquivalence(const DevicePtr& device,
                            ShaderManager& shaderManager,
                            const GpuScene& baseGpu,
                            const Container::VariableArray<uint32_t>& baseIds,
                            const CameraSet& cameras)
    {
        // 材質の表（4 件）の外の材質 7 を持つ記録（記録 1 の複製）を足した記録の表を作る
        GpuScene gpu = baseGpu;
        VisibilityBuffer::DrawRecord outOfTableRecord = baseGpu.Records.Data()[1];
        outOfTableRecord.MaterialIndex = 7;
        const uint32_t outOfTableNumber = gpu.Records.Add(outOfTableRecord);
        Expect(outOfTableNumber == 6, "材質の表の外の材質を持つ記録は 6 番のはず");
        gpu.RecordTable = CreateHostBuffer(device, gpu.Records.Data(), gpu.Records.SizeInBytes(),
                                           ResourceUsage::StorageBuffer | ResourceUsage::ShaderRead, "ResolveTestRecordsExtended");

        // 分類の参照が混じるように、ID の画像へ画素を足す（直接版との一致だけを見るので、足した画素は幾何として一貫していなくてよい）
        Container::VariableArray<uint32_t> ids = baseIds;
        const uint32_t tile = VisibilityResolveGeometry::TILE_SIZE;
        const uint32_t tilesX = (ScreenWidth + tile - 1) / tile;
        const uint32_t tilesY = (ScreenHeight + tile - 1) / tile;
        auto idOf = [](uint32_t recordNumber, uint32_t triangleIndex) -> uint32_t
        {
            uint32_t id = 0;
            VisibilityBuffer::TryEncode(recordNumber, triangleIndex, id);
            return id;
        };
        auto put = [&ids](uint32_t x, uint32_t y, uint32_t id) { ids[static_cast<size_t>(y) * ScreenWidth + x] = id; };

        // 空のタイル（全部の画素が空。ID の画像の外れの画素を足す前に探す）のうち最後のものへ、材質 0・1・2・3・7 と引けない ID を混ぜる
        uint32_t mixTileX = tilesX;
        uint32_t mixTileY = tilesY;
        for (uint32_t tileY = 0; tileY + 1 < tilesY; ++tileY)
        {
            for (uint32_t tileX = 0; tileX + 1 < tilesX; ++tileX)
            {
                bool bEmpty = true;
                for (uint32_t y = tileY * tile; y < tileY * tile + tile; ++y)
                {
                    for (uint32_t x = tileX * tile; x < tileX * tile + tile; ++x)
                    {
                        bEmpty = bEmpty && ids[static_cast<size_t>(y) * ScreenWidth + x] == VisibilityBuffer::EMPTY_ID;
                    }
                }
                if (bEmpty)
                {
                    mixTileX = tileX;
                    mixTileY = tileY;
                }
            }
        }
        Expect(mixTileX < tilesX, "材質を混ぜるための空のタイルが場面に無い");
        if (mixTileX < tilesX)
        {
            const uint32_t x0 = mixTileX * tile;
            const uint32_t y0 = mixTileY * tile;
            put(x0 + 0, y0, idOf(1, 0));
            put(x0 + 1, y0, idOf(2, 1));
            put(x0 + 2, y0, idOf(3, 0));
            put(x0 + 3, y0, idOf(4, 0));
            put(x0 + 4, y0, idOf(6, 0));
            put(x0 + 5, y0, idOf(99, 0)); // 表に無い記録
            put(x0 + 6, y0, idOf(1, 1));
            put(x0 + 0, y0 + 1, idOf(5, 1));
            put(x0 + 7, y0 + 7, idOf(3, 1));
        }
        // 右の端・下の端の部分タイル（幅 100 は 8 で割れず、高さ 60 も割れない）に、別の材質を混ぜる。画面の外になる画素も含む
        put(98, 5, idOf(1, 0));
        put(99, 5, idOf(2, 1));
        put(99, 6, idOf(6, 1));
        put(3, 59, idOf(3, 0));
        put(4, 59, idOf(4, 1));
        put(96, 57, idOf(1, 1));
        put(99, 59, idOf(5, 0));
        put(97, 2, idOf(3, 1));
        put(96, 10, idOf(4, 0));
        put(2, 58, idOf(2, 0));
        put(5, 56, idOf(1, 0));
        gpu.IdTexture = CreateIdTexture(device, ids);
        if (!gpu.RecordTable || !gpu.IdTexture)
        {
            std::cerr << TestName << " 材質のタイルの一致の検査の資源を作れませんでした" << std::endl;
            ++g_failures;
            return;
        }

        const TileCase cases[] = {
            {"A(既定)", 16, MaterialTiles::MAX_GROUP_COUNT_X, ScreenWidth, ScreenHeight},
            {"B(引数の x の上限 3 で y へ広げる)", 16, 3, ScreenWidth, ScreenHeight},
            {"C(分類の材質の上限 3)", 3, MaterialTiles::MAX_GROUP_COUNT_X, ScreenWidth, ScreenHeight},
            {"D(画面 99x59 = 画面の外を読み書きしない)", 16, 4, ScreenWidth - 1, ScreenHeight - 1},
        };
        uint64_t frameSerial = 100;
        for (const TileCase& testCase : cases)
        {
            const TileAnalysis analysis =
                AnalyzeTiles(gpu.Records, ids, testCase.Width, testCase.Height, testCase.MaxMaterials);
            TileRunOptions options;
            options.MaxMaterials = testCase.MaxMaterials;
            options.GroupCountXLimit = testCase.GroupCountXLimit;

            EquivalenceCounters counters;
            uint32_t wrappedMaterials = 0;
            for (const bool bDump : {false, true})
            {
                const Readback direct =
                    RunResolve(device, shaderManager, gpu, cameras, bDump, true, frameSerial++, nullptr, testCase.Width, testCase.Height);
                const Readback tiled =
                    RunResolve(device, shaderManager, gpu, cameras, bDump, true, frameSerial++, &options, testCase.Width, testCase.Height);
                CompareTileRun(testCase, bDump ? "検証用の版" : "製品の版", gpu.Records, ids, direct, tiled, counters);

                // 分類の引数（一覧に入る（タイル, 材質）の数が参照と一致すること。x の上限で y へ広がった材質の数）
                uint32_t entries = 0;
                wrappedMaterials = 0;
                for (uint32_t material = 0; material < testCase.MaxMaterials; ++material)
                {
                    const size_t base = static_cast<size_t>(material) * MaterialTiles::ARGS_STRIDE_WORDS;
                    if (base + MaterialTiles::ARG_TILE_COUNT >= tiled.TileArgs.size())
                    {
                        break;
                    }
                    entries += tiled.TileArgs[base + MaterialTiles::ARG_TILE_COUNT];
                    wrappedMaterials += tiled.TileArgs[base + MaterialTiles::ARG_GROUP_Y] > 1u ? 1u : 0u;
                }
                Expect(entries == analysis.Entries, "分類が一覧へ入れた（タイル, 材質）の数が参照と一致しなければならない");
            }

            Expect(analysis.EmptyTiles >= 1, "空のタイルを含まなければならない");
            Expect(analysis.PartialTilePixels >= 4, "部分タイルの解決できる画素を含まなければならない");
            Expect(testCase.MaxMaterials < 8 || analysis.MaxMaterialsInTile >= 4, "4 つ以上の材質が混じるタイルを含まなければならない");
            Expect(counters.Compared >= 700, "直接版と比べた画素が少なすぎる（場面が検査になっていない）");
            Expect(counters.Mismatched == 0, "材質ごとの間接 dispatch の解決は、直接 dispatch の結果と画素でビット単位に一致しなければならない");
            Expect(counters.GuardViolations == 0, "空・引けない ID・画面の外の画素は、どちらの形でも何も書かれてはならない");
            Expect(counters.UnexpectedDifferences == 0, "直接版との違いは、分類の上限以上の材質の画素（一覧に入らない）だけでなければならない");
            // 上限以上の材質の画素は、検証用の版・製品の版のそれぞれで数える（2 回）
            Expect(counters.DesignDifferences == 2u * analysis.UnlistedPixels,
                   "一覧に入らない（分類の材質の上限以上の）画素の数が参照と一致しなければならない");
            if (testCase.MaxMaterials < 8)
            {
                Expect(analysis.UnlistedPixels > 0, "C は分類の上限以上の材質の画素を含まなければならない");
            }
            else
            {
                Expect(analysis.UnlistedPixels == 0, "分類の上限が材質の表の外の材質（7）以上なら、一覧に入らない画素は無いはず");
            }
            if (testCase.GroupCountXLimit < MaterialTiles::MAX_GROUP_COUNT_X)
            {
                Expect(wrappedMaterials > 0, "引数の x の上限を小さくしたとき、y へ広げる材質を含まなければならない");
            }
            if (testCase.Width < ScreenWidth)
            {
                Expect(analysis.OutsideIdPixels > 0, "D は画面の外に ID が書かれた画素を含まなければならない");
            }

            std::cout << TestName << " 材質のタイル " << testCase.Name << ": 比べた画素=" << counters.Compared
                      << " 不一致=" << counters.Mismatched << " 一覧に入らない画素(違う点)=" << analysis.UnlistedPixels
                      << " 画面の外のID=" << analysis.OutsideIdPixels << " 空のタイル=" << analysis.EmptyTiles
                      << " 1タイルの最大の材質数=" << analysis.MaxMaterialsInTile << " 一覧の(タイル,材質)=" << analysis.Entries
                      << " yへ広げた材質=" << wrappedMaterials << std::endl;
        }
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

            // 材質ごとのタイルの一覧から走る形（間接 dispatch）は、同じ ID の画像の直接 dispatch の結果とビット単位で一致する
            RunTileEquivalence(device, shaderManager, gpu, idImage, cameras);

            // 材質ごとの形は、材質のテクスチャで Albedo・Normal・Material を書く
            RunMaterialTextureCase(device, shaderManager, scene, idImage, references, cameras);

            // 材質ごとの形は、VT の要求（フィードバック）も要求のバッファへ書く
            RunVirtualTextureFeedbackCase(device, shaderManager, scene, idImage, references, cameras);

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
