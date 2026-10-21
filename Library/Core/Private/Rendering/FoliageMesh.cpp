#include "Rendering/FoliageMesh.h"

#include <algorithm>
#include <cmath>

namespace NorvesLib::Core::Rendering::FoliageMesh
{
    namespace
    {
        constexpr float kPi = 3.14159265358979f;

        /// 生成の中だけで使う 3 成分の値
        struct V3
        {
            float X = 0.0f;
            float Y = 0.0f;
            float Z = 0.0f;
        };

        V3 operator+(const V3 &a, const V3 &b) { return {a.X + b.X, a.Y + b.Y, a.Z + b.Z}; }
        V3 operator-(const V3 &a, const V3 &b) { return {a.X - b.X, a.Y - b.Y, a.Z - b.Z}; }
        V3 operator*(const V3 &a, float s) { return {a.X * s, a.Y * s, a.Z * s}; }
        float Dot(const V3 &a, const V3 &b) { return a.X * b.X + a.Y * b.Y + a.Z * b.Z; }
        V3 Cross(const V3 &a, const V3 &b)
        {
            return {a.Y * b.Z - a.Z * b.Y, a.Z * b.X - a.X * b.Z, a.X * b.Y - a.Y * b.X};
        }
        float Length(const V3 &a) { return std::sqrt(Dot(a, a)); }
        V3 Normalize(const V3 &a, const V3 &fallback = {0.0f, 1.0f, 0.0f})
        {
            const float length = Length(a);
            return length > 1.0e-8f ? a * (1.0f / length) : fallback;
        }
        float Clamp01(float value) { return std::clamp(value, 0.0f, 1.0f); }
        float Lerp(float a, float b, float t) { return a + (b - a) * t; }

        /// 種から決まる乱数（PCG32）
        class FoliageRandom
        {
        public:
            explicit FoliageRandom(uint32_t seed)
                : m_State(static_cast<uint64_t>(seed) * 0x9E3779B97F4A7C15ull + 0x632BE59BD9B4E019ull)
            {
                NextU32();
            }

            uint32_t NextU32()
            {
                const uint64_t old = m_State;
                m_State = old * 6364136223846793005ull + 1442695040888963407ull;
                const uint32_t xorShifted = static_cast<uint32_t>(((old >> 18u) ^ old) >> 27u);
                const uint32_t rotation = static_cast<uint32_t>(old >> 59u);
                return (xorShifted >> rotation) | (xorShifted << ((32u - rotation) & 31u));
            }

            /// [0, 1)
            float Next01() { return static_cast<float>(NextU32() >> 8) * (1.0f / 16777216.0f); }

            float Range(float minValue, float maxValue) { return minValue + (maxValue - minValue) * Next01(); }

        private:
            uint64_t m_State = 0;
        };

        /// 頂点と三角形を積む
        class MeshBuilder
        {
        public:
            explicit MeshBuilder(FoliageMeshData &mesh) : m_Mesh(mesh) {}

            uint32_t AddVertex(const V3 &position, const V3 &normal, const V3 &side, float flex, float tint,
                               float occlusion)
            {
                FoliageVertex vertex;
                vertex.Position[0] = position.X;
                vertex.Position[1] = position.Y;
                vertex.Position[2] = position.Z;
                const V3 n = Normalize(normal);
                vertex.Normal[0] = n.X;
                vertex.Normal[1] = n.Y;
                vertex.Normal[2] = n.Z;
                vertex.Side[0] = side.X;
                vertex.Side[1] = side.Y;
                vertex.Side[2] = side.Z;
                vertex.Flex = Clamp01(flex);
                vertex.Tint = Clamp01(tint);
                vertex.Occlusion = Clamp01(occlusion);
                vertex.Phase = m_Phase;
                m_Mesh.Vertices.push_back(vertex);
                m_Mesh.Height = std::max(m_Mesh.Height, position.Y);
                return static_cast<uint32_t>(m_Mesh.Vertices.size() - 1);
            }

            void AddTriangle(uint32_t a, uint32_t b, uint32_t c)
            {
                m_Mesh.Indices.push_back(a);
                m_Mesh.Indices.push_back(b);
                m_Mesh.Indices.push_back(c);
            }

            /// これから積む葉・枝の揺れの位相
            void SetPhase(float phase) { m_Phase = Clamp01(phase); }

        private:
            FoliageMeshData &m_Mesh;
            float m_Phase = 0.0f;
        };

        /// v を軸 axis（長さ 1）のまわりに angle[rad] 回す
        V3 RotateAround(const V3 &v, const V3 &axis, float angle)
        {
            const float c = std::cos(angle);
            const float s = std::sin(angle);
            return v * c + Cross(axis, v) * s + axis * (Dot(axis, v) * (1.0f - c));
        }

        /// t（長さ 1）に垂直な単位ベクトルを 1 つ
        V3 AnyPerpendicular(const V3 &t)
        {
            const V3 reference = std::abs(t.Y) < 0.9f ? V3{0.0f, 1.0f, 0.0f} : V3{1.0f, 0.0f, 0.0f};
            return Normalize(Cross(t, reference));
        }

        /// 1 枚の帯（葉・穂）を、中心線の点の列から積む。最後の点は先の 1 頂点にする。
        /// halfWidths は点ごとの半分の幅、sideDirs は点ごとの横の向き、flex・tint・occlusion は点ごとの値
        void AddRibbon(MeshBuilder &builder, const V3 *points, const V3 *sideDirs, const float *halfWidths,
                       const float *flex, const float *tint, const float *occlusion, uint32_t pointCount,
                       const V3 &upBias, float upBiasWeight)
        {
            if (pointCount < 2)
            {
                return;
            }
            uint32_t previousLeft = 0;
            uint32_t previousRight = 0;
            for (uint32_t k = 0; k < pointCount; ++k)
            {
                const V3 tangent = Normalize(k + 1 < pointCount ? points[k + 1] - points[k] : points[k] - points[k - 1]);
                V3 face = Normalize(Cross(sideDirs[k], tangent));
                // 表の向きは上（と外）の側にそろえる。裏はフラグメントが帯の面で鏡に映して使う
                if (Dot(face, upBias) < 0.0f)
                {
                    face = face * -1.0f;
                }
                // 細い葉は面の向きだけで光を受けると明暗が強すぎるので、法線を上へ寄せる（帯の面の中の成分は裏でも保つ）
                const V3 normal = Normalize(face * (1.0f - upBiasWeight) + upBias * upBiasWeight);
                if (k + 1 == pointCount)
                {
                    const uint32_t tip = builder.AddVertex(points[k], normal, V3{}, flex[k], tint[k], occlusion[k]);
                    builder.AddTriangle(previousLeft, previousRight, tip);
                    break;
                }
                const V3 side = sideDirs[k] * halfWidths[k];
                const uint32_t left = builder.AddVertex(points[k] - side, normal, side * -1.0f, flex[k], tint[k], occlusion[k]);
                const uint32_t right = builder.AddVertex(points[k] + side, normal, side, flex[k], tint[k], occlusion[k]);
                if (k > 0)
                {
                    builder.AddTriangle(previousLeft, previousRight, left);
                    builder.AddTriangle(previousRight, right, left);
                }
                previousLeft = left;
                previousRight = right;
            }
        }

        /// 細い角柱（三角の断面）を、中心線の点の列から積む。最後の点は先の 1 頂点（尖り）にする
        void AddPrism(MeshBuilder &builder, const V3 *points, const float *radii, const float *flex, const float *tint,
                      const float *occlusion, uint32_t pointCount)
        {
            constexpr uint32_t kSides = 3;
            if (pointCount < 2)
            {
                return;
            }
            V3 u = AnyPerpendicular(Normalize(points[1] - points[0]));
            uint32_t previousRing = 0;
            for (uint32_t k = 0; k < pointCount; ++k)
            {
                const V3 tangent = Normalize(k + 1 < pointCount ? points[k + 1] - points[k] : points[k] - points[k - 1]);
                // 前の点の横の向きを今の接線に垂直へ直して、断面がねじれないようにする
                u = Normalize(u - tangent * Dot(u, tangent), AnyPerpendicular(tangent));
                const V3 v = Cross(tangent, u);
                if (k + 1 == pointCount)
                {
                    const uint32_t tip = builder.AddVertex(points[k], tangent, V3{}, flex[k], tint[k], occlusion[k]);
                    for (uint32_t side = 0; side < kSides; ++side)
                    {
                        builder.AddTriangle(previousRing + side, previousRing + (side + 1) % kSides, tip);
                    }
                    break;
                }
                uint32_t first = 0;
                for (uint32_t side = 0; side < kSides; ++side)
                {
                    const float angle = 2.0f * kPi * static_cast<float>(side) / static_cast<float>(kSides);
                    const V3 offset = (u * std::cos(angle) + v * std::sin(angle)) * radii[k];
                    const uint32_t index =
                        builder.AddVertex(points[k] + offset, offset, offset, flex[k], tint[k], occlusion[k]);
                    if (side == 0)
                    {
                        first = index;
                    }
                }
                if (k > 0)
                {
                    for (uint32_t side = 0; side < kSides; ++side)
                    {
                        const uint32_t a = previousRing + side;
                        const uint32_t b = previousRing + (side + 1) % kSides;
                        const uint32_t c = first + side;
                        const uint32_t d = first + (side + 1) % kSides;
                        builder.AddTriangle(a, b, c);
                        builder.AddTriangle(b, d, c);
                    }
                }
                previousRing = first;
            }
        }

        // ---- 枯れ草の株 ----

        constexpr uint32_t kMaxRibbonPoints = 16;

        /// 1 枚の葉か穂の茎を、根元・向き・反りから作る
        struct BladeShape
        {
            V3 Root;
            float Azimuth = 0.0f;
            float Length = 0.3f;
            /// 根元の傾き[rad]と、先までに増える傾き[rad]（反って垂れる）
            float Lean = 0.2f;
            float Bend = 0.8f;
            float HalfWidth = 0.003f;
            /// 先へ細くなる割合（1 で先が 0）
            float Taper = 0.85f;
            float Twist = 0.0f;
            uint32_t Segments = 6;
            float TintScale = 1.0f;
            float BaseTint = 0.15f;
            float BaseOcclusion = 0.5f;
            /// 根元のしなりの重み（茎の先に付く穂は茎の先と同じ 1 から始める）
            float FlexStart = 0.0f;
        };

        /// 葉 1 枚の中心線を積み、終わりの点と向きを返す
        void AddBlade(MeshBuilder &builder, const BladeShape &shape, V3 *outTip = nullptr, V3 *outTipTangent = nullptr)
        {
            V3 points[kMaxRibbonPoints];
            V3 sideDirs[kMaxRibbonPoints];
            float halfWidths[kMaxRibbonPoints];
            float flex[kMaxRibbonPoints];
            float tint[kMaxRibbonPoints];
            float occlusion[kMaxRibbonPoints];
            const uint32_t segments = std::clamp(shape.Segments, 2u, kMaxRibbonPoints - 1);
            const V3 outward = {std::cos(shape.Azimuth), 0.0f, std::sin(shape.Azimuth)};
            const V3 baseSide = {-outward.Z, 0.0f, outward.X};
            V3 position = shape.Root;
            V3 tangent = {0.0f, 1.0f, 0.0f};
            const float step = shape.Length / static_cast<float>(segments);
            for (uint32_t k = 0; k <= segments; ++k)
            {
                const float s = static_cast<float>(k) / static_cast<float>(segments);
                // 根元は立ち、先へ行くほど自重で大きく反って垂れる（傾きの増え方を先に寄せる）
                const float angle = shape.Lean + shape.Bend * s * s;
                tangent = Normalize(outward * std::sin(angle) + V3{0.0f, std::cos(angle), 0.0f});
                points[k] = position;
                // 横の向きは接線のまわりに少しずつねじる
                sideDirs[k] = Normalize(RotateAround(baseSide, tangent, shape.Twist * s));
                // 幅は根元の少し細い所から広がって、葉の大半で平行に保ち、先の 3 割で細くなる（三角の板にしない）
                const float baseNarrow = 0.7f + 0.3f * Clamp01(s / 0.15f);
                halfWidths[k] = shape.HalfWidth * baseNarrow * (1.0f - shape.Taper * std::pow(s, 3.0f));
                flex[k] = Lerp(shape.FlexStart, 1.0f, s);
                tint[k] = Lerp(shape.BaseTint, 1.0f, std::pow(s, 0.6f)) * shape.TintScale;
                occlusion[k] = Lerp(shape.BaseOcclusion, 1.0f, std::pow(s, 0.8f));
                position = position + tangent * step;
            }
            const V3 upBias = Normalize(V3{0.0f, 1.0f, 0.0f} + outward * 0.3f);
            AddRibbon(builder, points, sideDirs, halfWidths, flex, tint, occlusion, segments + 1, upBias, 0.45f);
            if (outTip != nullptr)
            {
                *outTip = points[segments];
            }
            if (outTipTangent != nullptr)
            {
                *outTipTangent = tangent;
            }
        }

        // ---- 細い枝の低木 ----

        constexpr uint32_t kMaxBranchPoints = 8;
        /// 1 回に作る影の簡略のメッシュの数の上限
        constexpr uint32_t kMaxShrubProxies = 4;

        /// 影の簡略のメッシュを積む先と作り方
        struct ShrubProxyTarget
        {
            MeshBuilder *Builder = nullptr;
            ShrubShadowProxyDesc Desc;
            /// 葉の長さと幅に掛ける倍率（√LeafKeepEvery）
            float LeafScale = 1.0f;
        };

        struct ShrubContext
        {
            MeshBuilder *Builder = nullptr;
            FoliageRandom *Random = nullptr;
            const ShrubDesc *Desc = nullptr;
            /// 根元から最も遠い枝先までの道のりの目安[m]（しなりの重みの分母）
            float MaxReach = 1.0f;
            /// 影の簡略のメッシュ（乱数を使わない。本体と同じ値から積む）
            ShrubProxyTarget Proxies[kMaxShrubProxies];
            uint32_t ProxyCount = 0;
            /// 株の中で積んだ葉の通し番号（簡略のメッシュの間引きに使う）
            uint32_t LeafSerial = 0;
        };

        /// 樹冠の外形（楕円体）に対して、点がどれだけ外にあるか（1 で外形の面、1 未満が内側）
        float CrownExtent(const ShrubDesc &desc, const V3 &p)
        {
            if (desc.Form == ShrubForm::Rounded)
            {
                // 丸い株: 中心を高さの 45 % に置いた楕円体。地面より下は外形の内側として扱う
                const float centerY = desc.Height * 0.45f;
                const float halfHeight = desc.Height * 0.55f;
                const float dx = p.X / desc.CrownRadius;
                const float dz = p.Z / desc.CrownRadius;
                const float dy = (p.Y - centerY) / halfHeight;
                return std::sqrt(dx * dx + dz * dz + (p.Y < centerY ? 0.0f : dy * dy));
            }
            // ほうき状: 上へ開く逆円錐（高さ 0 で半径の 3 割、上端で半径いっぱい）と、高さの上限
            const float h = Clamp01(p.Y / desc.Height);
            const float allowedRadius = desc.CrownRadius * (0.3f + 0.7f * h);
            const float radial = std::sqrt(p.X * p.X + p.Z * p.Z) / std::max(allowedRadius, 1.0e-3f);
            return std::max(radial, p.Y / desc.Height);
        }

        /// 樹冠の中の位置からの遮蔽（外側・上ほど明るい）
        float CrownOcclusion(const ShrubDesc &desc, const V3 &p)
        {
            const float radial = std::sqrt(p.X * p.X + p.Z * p.Z) / std::max(desc.CrownRadius, 1.0e-3f);
            const float height = Clamp01(p.Y / std::max(desc.Height, 1.0e-3f));
            if (desc.Form == ShrubForm::Rounded)
            {
                return Clamp01(0.25f + 0.55f * Clamp01(radial) + 0.35f * height);
            }
            return Clamp01(0.4f + 0.25f * Clamp01(radial) + 0.45f * height);
        }

        /// 枝先の短い枝に、小さな葉を数枚付ける
        void AddLeaves(ShrubContext &context, const V3 &start, const V3 &end, float pathStart, float pathLength)
        {
            const ShrubDesc &desc = *context.Desc;
            FoliageRandom &random = *context.Random;
            const V3 axis = Normalize(end - start);
            for (uint32_t leaf = 0; leaf < desc.LeavesPerTwig; ++leaf)
            {
                const float t = random.Range(0.35f, 1.0f);
                const V3 base = start + (end - start) * t;
                // 枝から 35〜70° 開き、枝のまわりのでたらめな向きへ出す
                const V3 perpendicular = RotateAround(AnyPerpendicular(axis), axis, random.Range(0.0f, 2.0f * kPi));
                const float openAngle = random.Range(0.6f, 1.2f);
                const V3 direction = Normalize(axis * std::cos(openAngle) + perpendicular * std::sin(openAngle));
                const float length = desc.LeafLength * random.Range(0.7f, 1.3f);
                const float halfWidth = desc.LeafWidth * 0.5f * random.Range(0.8f, 1.2f);
                const V3 sideDir = Normalize(Cross(direction, perpendicular), AnyPerpendicular(direction));
                const V3 middle = base + direction * (length * 0.45f);
                const V3 tip = base + direction * length;
                V3 face = Normalize(Cross(sideDir, direction));
                const V3 outward = Normalize(V3{middle.X, 0.4f * desc.Height, middle.Z});
                if (Dot(face, outward) < 0.0f)
                {
                    face = face * -1.0f;
                }
                const V3 normal = Normalize(face * 0.6f + outward * 0.4f);
                const float flex = Clamp01((pathStart + pathLength * t + length) / context.MaxReach);
                const float occlusion = CrownOcclusion(desc, middle);
                // 葉の明るさは枝の色から先の色までばらつかせる（明るい葉だけが点在して見えない）
                const float tint = random.Range(0.35f, 0.9f);
                MeshBuilder &builder = *context.Builder;
                const uint32_t i0 = builder.AddVertex(base, normal, V3{}, flex, tint, occlusion);
                const uint32_t i1 = builder.AddVertex(middle - sideDir * halfWidth, normal, sideDir * -halfWidth, flex, tint,
                                                      occlusion);
                const uint32_t i2 = builder.AddVertex(middle + sideDir * halfWidth, normal, sideDir * halfWidth, flex, tint,
                                                      occlusion);
                const uint32_t i3 = builder.AddVertex(tip, normal, V3{}, flex, tint, occlusion);
                builder.AddTriangle(i0, i1, i2);
                builder.AddTriangle(i1, i3, i2);

                // 影の簡略のメッシュ: LeafKeepEvery 枚に 1 枚を、葉の中ほどを中心に長さと幅を広げて積む
                const uint32_t serial = context.LeafSerial++;
                for (uint32_t proxyIndex = 0; proxyIndex < context.ProxyCount; ++proxyIndex)
                {
                    const ShrubProxyTarget &proxy = context.Proxies[proxyIndex];
                    if (serial % std::max(proxy.Desc.LeafKeepEvery, 1u) != 0u)
                    {
                        continue;
                    }
                    const float scale = proxy.LeafScale;
                    const V3 proxyBase = middle - direction * (length * 0.45f * scale);
                    const V3 proxyTip = middle + direction * (length * 0.55f * scale);
                    const float proxyHalfWidth = halfWidth * scale;
                    MeshBuilder &proxyBuilder = *proxy.Builder;
                    const uint32_t p0 = proxyBuilder.AddVertex(proxyBase, normal, V3{}, flex, tint, occlusion);
                    const uint32_t p1 = proxyBuilder.AddVertex(middle - sideDir * proxyHalfWidth, normal, sideDir * -proxyHalfWidth, flex,
                                                               tint, occlusion);
                    const uint32_t p2 = proxyBuilder.AddVertex(middle + sideDir * proxyHalfWidth, normal, sideDir * proxyHalfWidth, flex,
                                                               tint, occlusion);
                    const uint32_t p3 = proxyBuilder.AddVertex(proxyTip, normal, V3{}, flex, tint, occlusion);
                    proxyBuilder.AddTriangle(p0, p1, p2);
                    proxyBuilder.AddTriangle(p1, p3, p2);
                }
            }
        }

        /// 枝を 1 本積み、段（level）に応じて子の枝・葉を出す
        void AddBranch(ShrubContext &context, const V3 &start, const V3 &direction, float length, float radiusStart,
                       uint32_t level, float pathStart)
        {
            const ShrubDesc &desc = *context.Desc;
            FoliageRandom &random = *context.Random;
            const bool bRounded = desc.Form == ShrubForm::Rounded;
            const uint32_t maxLevel = std::clamp(desc.BranchLevels, 1u, 4u) - 1u;
            // 枝先が外形の外へ出るなら、内側に収まるまで短くする（丸い樹冠・ほうきの外形を作る）
            for (uint32_t attempt = 0; attempt < 6 && CrownExtent(desc, start + direction * length) > 1.0f; ++attempt)
            {
                length *= 0.8f;
            }
            if (length < 0.01f)
            {
                return;
            }

            // 区切りの数は太い枝ほど多い（根元の枝 4、細い枝 2、棘のような短い枝先 1）
            const uint32_t segments = level == 0 ? 4u : (level == maxLevel ? 1u : (level == 1 ? 3u : 2u));
            V3 points[kMaxBranchPoints];
            float radii[kMaxBranchPoints];
            float flex[kMaxBranchPoints];
            float tint[kMaxBranchPoints];
            float occlusion[kMaxBranchPoints];
            const float radiusEnd = level == maxLevel ? desc.TwigRadius : std::max(desc.TwigRadius, radiusStart * 0.55f);
            // 根元の枝は株全体で同じ位相（株の揺れ）、細い枝ほど別々に揺れる
            const float branchPhase = level == 0 ? 0.0f : random.Next01();
            context.Builder->SetPhase(branchPhase);
            for (uint32_t proxyIndex = 0; proxyIndex < context.ProxyCount; ++proxyIndex)
            {
                context.Proxies[proxyIndex].Builder->SetPhase(branchPhase);
            }
            const float step = length / static_cast<float>(segments);
            V3 position = start;
            V3 heading = direction;
            // 茎の色（0）から先の色（1）へ。丸い株は木質の灰褐色の枝で、先の色は葉だけ。ほうき状の株は緑の茎そのものが先の色
            const float tintStart = bRounded ? 0.0f : (level == 0 ? 0.35f : 0.75f);
            const float tintEnd = bRounded ? (level == maxLevel ? 0.15f : 0.05f) : 1.0f;
            for (uint32_t k = 0; k <= segments; ++k)
            {
                const float s = static_cast<float>(k) / static_cast<float>(segments);
                points[k] = position;
                radii[k] = Lerp(radiusStart, radiusEnd, s);
                const float path = pathStart + length * s;
                flex[k] = Clamp01(path / context.MaxReach);
                tint[k] = Lerp(tintStart, tintEnd, s);
                occlusion[k] = CrownOcclusion(desc, position);
                // 少しずつ曲げる: でたらめな横の揺れと、上への向き（ほうき状の株は強く上へ）
                const V3 wobble = RotateAround(AnyPerpendicular(heading), heading, random.Range(0.0f, 2.0f * kPi));
                const float upPull = bRounded ? 0.05f : 0.18f;
                heading = Normalize(heading + wobble * random.Range(0.05f, 0.25f) + V3{0.0f, upPull, 0.0f});
                position = position + heading * step;
            }
            AddPrism(*context.Builder, points, radii, flex, tint, occlusion, segments + 1);
            for (uint32_t proxyIndex = 0; proxyIndex < context.ProxyCount; ++proxyIndex)
            {
                const ShrubProxyTarget &proxy = context.Proxies[proxyIndex];
                if (static_cast<int32_t>(level) <= proxy.Desc.MaxPrismLevel)
                {
                    AddPrism(*proxy.Builder, points, radii, flex, tint, occlusion, segments + 1);
                }
            }

            // 葉は細い枝と枝先に付ける（丸い株の灰緑の小さな葉の房）
            if (desc.LeavesPerTwig > 0 && level + 1 >= maxLevel)
            {
                AddLeaves(context, points[0], points[segments], pathStart, length);
            }
            if (level == maxLevel)
            {
                return;
            }

            // 子の枝: 丸い株は大きく開いて詰まり、ほうき状の株は節ごとに上向きの細い茎を出す。
            // 最後の段は短い棘のような枝先（長さは記述の SpurLength 前後）
            const bool bSpur = level + 1 == maxLevel && maxLevel >= 2;
            uint32_t childCount = 0;
            if (bRounded)
            {
                childCount = level == 0 ? 3u + random.NextU32() % 3u : (bSpur ? 3u + random.NextU32() % 2u : 4u + random.NextU32() % 2u);
            }
            else
            {
                childCount = level == 0 ? 4u + random.NextU32() % 3u : 2u + random.NextU32() % 2u;
            }
            for (uint32_t child = 0; child < childCount; ++child)
            {
                const float s = bRounded ? random.Range(0.25f, 0.98f)
                                         : (static_cast<float>(child) + random.Range(0.6f, 1.0f)) /
                                               static_cast<float>(childCount + 1);
                const uint32_t segment = std::min(segments - 1, static_cast<uint32_t>(s * static_cast<float>(segments)));
                const float local = s * static_cast<float>(segments) - static_cast<float>(segment);
                const V3 origin = points[segment] + (points[segment + 1] - points[segment]) * local;
                const V3 parentAxis = Normalize(points[segment + 1] - points[segment]);
                const float openAngle = bRounded ? (bSpur ? random.Range(0.9f, 1.4f) : random.Range(0.55f, 1.15f))
                                                 : random.Range(0.25f, 0.5f);
                const V3 perpendicular = RotateAround(AnyPerpendicular(parentAxis), parentAxis, random.Range(0.0f, 2.0f * kPi));
                const V3 childDirection = Normalize(parentAxis * std::cos(openAngle) + perpendicular * std::sin(openAngle));
                const float remaining = length * (1.0f - s);
                float childLength = 0.0f;
                if (bSpur)
                {
                    childLength = desc.SpurLength * random.Range(0.6f, 1.4f);
                }
                else if (bRounded)
                {
                    childLength = Lerp(remaining, length, 0.5f) * random.Range(0.4f, 0.7f);
                }
                else
                {
                    childLength = Lerp(remaining, length, 0.3f) * random.Range(0.55f, 0.85f);
                }
                const float childRadius = Lerp(radiusStart, radiusEnd, s) * 0.7f;
                AddBranch(context, origin, childDirection, childLength, childRadius, level + 1, pathStart + length * s);
            }
        }
    } // namespace

    Container::TSharedPtr<FoliageMeshData> BuildGrassClump(const GrassClumpDesc &desc)
    {
        auto mesh = Container::MakeShared<FoliageMeshData>();
        MeshBuilder builder(*mesh);
        FoliageRandom random(desc.Seed);
        const uint32_t bladeCount = std::max(1u, desc.BladeCount);
        for (uint32_t blade = 0; blade < bladeCount; ++blade)
        {
            BladeShape shape;
            // 向きは株のまわりに散らす（偏りすぎないよう区切りの中でずらす）
            shape.Azimuth = 2.0f * kPi * (static_cast<float>(blade) + random.Range(0.0f, 0.9f)) / static_cast<float>(bladeCount);
            const float radial = std::sqrt(random.Next01());
            shape.Root = {std::cos(shape.Azimuth) * desc.BaseRadius * radial, 0.0f,
                          std::sin(shape.Azimuth) * desc.BaseRadius * radial};
            shape.Azimuth += random.Range(-0.35f, 0.35f);
            // 長さは株の中心の葉ほど長く、外側は短い。葉ごとのばらつきも大きくする（そろった針の放射にしない）
            const float lengthRatio = Lerp(1.15f, 0.7f, radial) * random.Range(0.5f, 1.25f);
            shape.Length = desc.Height * lengthRatio;
            // 外側の葉ほど寝る。長い葉ほど自重で大きく反り、先が水平より下へ垂れる
            shape.Lean = random.Range(0.05f, 0.3f) + 0.45f * radial;
            shape.Bend = random.Range(0.2f, 0.7f) + 1.5f * lengthRatio * random.Range(0.45f, 1.0f);
            shape.HalfWidth = desc.BladeWidth * 0.5f * random.Range(0.75f, 1.2f);
            shape.Twist = random.Range(-0.4f, 0.4f);
            shape.Segments = desc.Segments;
            shape.TintScale = random.Range(0.8f, 1.0f);
            shape.BaseOcclusion = Lerp(0.45f, 0.7f, radial);
            builder.SetPhase(random.Next01());
            AddBlade(builder, shape);
        }
        // 穂の茎: ほぼまっすぐ立つ細い茎の先に、小さな穂（短い帯）を数枚付ける
        for (uint32_t culm = 0; culm < desc.CulmCount; ++culm)
        {
            BladeShape shape;
            shape.Azimuth = random.Range(0.0f, 2.0f * kPi);
            const float radial = std::sqrt(random.Next01()) * 0.6f;
            shape.Root = {std::cos(shape.Azimuth) * desc.BaseRadius * radial, 0.0f,
                          std::sin(shape.Azimuth) * desc.BaseRadius * radial};
            shape.Length = desc.Height * random.Range(1.1f, 1.45f);
            shape.Lean = random.Range(0.02f, 0.22f);
            shape.Bend = random.Range(0.02f, 0.15f);
            shape.HalfWidth = 0.0011f;
            shape.Taper = 0.2f;
            shape.Twist = 0.0f;
            shape.Segments = 4;
            shape.BaseTint = 0.5f;
            shape.TintScale = 1.0f;
            shape.BaseOcclusion = 0.6f;
            V3 tip;
            V3 tipTangent;
            // 穂は茎と同じ位相で揺らす（茎の先から離れない）
            builder.SetPhase(random.Next01());
            AddBlade(builder, shape, &tip, &tipTangent);
            const uint32_t spikelets = 4;
            for (uint32_t spikelet = 0; spikelet < spikelets; ++spikelet)
            {
                BladeShape spike;
                spike.Azimuth = random.Range(0.0f, 2.0f * kPi);
                spike.Root = tip - tipTangent * (0.04f * static_cast<float>(spikelet) / static_cast<float>(spikelets));
                spike.Length = random.Range(0.02f, 0.035f);
                spike.Lean = random.Range(0.2f, 0.6f);
                spike.Bend = 0.1f;
                spike.HalfWidth = 0.0016f;
                spike.Taper = 0.9f;
                spike.Segments = 2;
                spike.BaseTint = 1.0f;
                spike.BaseOcclusion = 0.95f;
                spike.FlexStart = 1.0f;
                AddBlade(builder, spike);
            }
        }
        return mesh;
    }

    Container::TSharedPtr<FoliageMeshData> BuildShrub(const ShrubDesc &desc)
    {
        return BuildShrub(desc, nullptr, 0u, nullptr);
    }

    Container::TSharedPtr<FoliageMeshData> BuildShrub(const ShrubDesc &desc, const ShrubShadowProxyDesc *proxies, uint32_t proxyCount,
                                                      Container::TSharedPtr<FoliageMeshData> *outProxies)
    {
        auto mesh = Container::MakeShared<FoliageMeshData>();
        MeshBuilder builder(*mesh);
        FoliageRandom random(desc.Seed);
        ShrubContext context;
        context.Builder = &builder;
        context.Random = &random;
        context.Desc = &desc;
        // 影の簡略のメッシュの積む先（MeshBuilder は積む先のメッシュを参照で持つので、この関数の終わりまで置いておく）
        proxyCount = (proxies != nullptr && outProxies != nullptr) ? std::min(proxyCount, kMaxShrubProxies) : 0u;
        Container::VariableArray<MeshBuilder> proxyBuilders;
        proxyBuilders.reserve(proxyCount);
        for (uint32_t proxyIndex = 0; proxyIndex < proxyCount; ++proxyIndex)
        {
            outProxies[proxyIndex] = Container::MakeShared<FoliageMeshData>();
            proxyBuilders.emplace_back(*outProxies[proxyIndex]);
            ShrubProxyTarget &target = context.Proxies[proxyIndex];
            target.Builder = &proxyBuilders.back();
            target.Desc = proxies[proxyIndex];
            target.LeafScale = std::sqrt(static_cast<float>(std::max(proxies[proxyIndex].LeafKeepEvery, 1u)));
        }
        context.ProxyCount = proxyCount;
        context.MaxReach = std::sqrt(desc.Height * desc.Height + desc.CrownRadius * desc.CrownRadius) * 1.1f;
        const bool bRounded = desc.Form == ShrubForm::Rounded;
        const uint32_t stemCount = std::max(1u, desc.StemCount);
        for (uint32_t stem = 0; stem < stemCount; ++stem)
        {
            const float azimuth =
                2.0f * kPi * (static_cast<float>(stem) + random.Range(0.0f, 0.8f)) / static_cast<float>(stemCount);
            // 水平からの立ち上がり: 丸い株は 40〜77°（根元から壺形に開く）、ほうき状の株は 60〜85°
            const float elevation = bRounded ? random.Range(0.7f, 1.35f) : random.Range(1.05f, 1.48f);
            const V3 direction = Normalize(
                V3{std::cos(azimuth) * std::cos(elevation), std::sin(elevation), std::sin(azimuth) * std::cos(elevation)});
            const V3 root = {std::cos(azimuth) * 0.03f * random.Next01(), 0.0f, std::sin(azimuth) * 0.03f * random.Next01()};
            const float length = bRounded ? desc.Height * random.Range(0.75f, 1.05f) : desc.Height * random.Range(0.8f, 1.1f);
            AddBranch(context, root, direction, length, desc.StemRadius * random.Range(0.8f, 1.2f), 0, 0.0f);
        }
        return mesh;
    }

} // namespace NorvesLib::Core::Rendering::FoliageMesh
