#include "Rendering/GrassBladeMesh.h"

#include "Rendering/GrassGenerateReference.h"
#include "Rendering/Wind.h"
#include <cmath>

namespace NorvesLib::Core::Rendering
{
    namespace
    {
        // grass.vert の GrassLength / GrassNormalize と加算・除算の順を一致させる。
        float GrassLength(const Math::Vector3& value)
        {
            return std::sqrt((value.x * value.x + value.y * value.y) + value.z * value.z);
        }

        Math::Vector3 GrassNormalize(const Math::Vector3& value, const Math::Vector3& fallback)
        {
            const float size = GrassLength(value);
            return size > 1.0e-6f ? value * (1.0f / size) : fallback;
        }

        // grass.vert 42〜51 行の写し。mat4 の列を参照し、行ごとに左から積和を行う。
        Math::Vector4 GrassTransform(const float (&matrix)[4][4], const Math::Vector4& value)
        {
            float result[4];
            for (uint32_t row = 0; row < 4; ++row)
            {
                result[row] = ((matrix[0][row] * value.x + matrix[1][row] * value.y) +
                    matrix[2][row] * value.z) + matrix[3][row] * value.w;
            }
            return Math::Vector4(result[0], result[1], result[2], result[3]);
        }
    } // namespace

    Math::Vector3 EvaluateGrassBladeVertex(const GrassDrawConstants& draw,
        const GrassSpeciesGpuData& species, const GrassBladeRecord& blade, uint32_t lod,
        uint32_t vertexIndexValue, bool bPrevious)
    {
        // G1: grass.vert 74〜87 行「G1」の復号・セル・ハッシュに対応する。
        const Math::Vector3 root(blade.PositionX, blade.PositionY, blade.PositionZ);
        float h = (static_cast<float>(blade.Packed & 1023u) / 1023.0f) * draw.Quant[0];
        float w = (static_cast<float>((blade.Packed >> 10) & 255u) / 255.0f) * draw.Quant[1];
        const float fade = static_cast<float>(blade.Packed >> 24) / 255.0f;
        const float v = static_cast<float>(GrassBladeIndexRow(vertexIndexValue)) / static_cast<float>(draw.LodSegments[lod]);
        const uint32_t gx = static_cast<uint32_t>(static_cast<int32_t>(std::floor(root.x / draw.BandSpacingMeters[lod])));
        const uint32_t gz = static_cast<uint32_t>(static_cast<int32_t>(std::floor(root.z / draw.BandSpacingMeters[lod])));
        const uint32_t salt = lod * 8u;
        const float u = 2.0f * GrassHashToUnit(GrassHash(gx, gz, salt + kGrassHashSaltFacing)) - 1.0f;
        const Math::Vector3 facing = GrassNormalize(Math::Vector3(1.0f - u * u, 0.0f, 2.0f * u), Math::Vector3(1.0f, 0.0f, 0.0f));
        Math::Vector3 side(-facing.z, 0.0f, facing.x);
        const float phase = GrassHashToUnit(GrassHash(gx, gz, salt + kGrassHashSaltPhase));
        const float lean = GrassHashToUnit(GrassHash(gx, gz, salt + kGrassHashSaltLean));

        // G2: grass.vert 88〜97 行「G2」と同じカメラ選択と滑らかな近接縮小。
        const bool bHistory = bPrevious && (draw.Flags[0] & kGrassDrawFlagPreviousCamera) != 0u;
        const float* camera = bHistory ? draw.PreviousCameraPosition : draw.CameraPosition;
        const Math::Vector3 cameraPosition(camera[0], camera[1], camera[2]);
        const float time = bHistory ? draw.Wind[3] : draw.Wind[2];
        const float d = GrassLength(root - cameraPosition);
        const float nearSpan = draw.NearFade[0] - draw.NearFade[1];
        const float nearT = GrassSaturate((d - draw.NearFade[1]) / (nearSpan > 1.0e-6f ? nearSpan : 1.0e-6f));
        const float nearSmooth = (nearT * nearT) * (3.0f - 2.0f * nearT);
        h = (h * fade) * (0.25f + 0.75f * nearSmooth);
        w = w * fade;

        // G3: grass.vert 98〜118 行「G3」。WindVelocity と同じ CPU ノイズを使い、風向の角度への往復を避ける。
        const float leanSigned = 2.0f * lean - 1.0f;
        const Math::Vector3 leanDirection = GrassNormalize(facing * leanSigned + side * (1.0f - std::abs(leanSigned)), side);
        Math::Vector3 offset = leanDirection * ((h * species.Bend) * (0.4f + 0.6f * lean));
        if ((draw.Flags[0] & kGrassDrawFlagWind) != 0u)
        {
            WindParameters wind;
            wind.BaseSpeed = draw.WindParams[0];
            wind.GustStrength = draw.WindParams[1];
            wind.GustFrequencyHz = draw.WindParams[2];
            wind.GustScaleMeters = draw.WindParams[3];
            wind.Turbulence = draw.WindExtra[0];
            const float dirX = draw.Wind[0];
            const float dirZ = draw.Wind[1];
            const float arg = (root.x * dirX + root.z * dirZ) / wind.GustScaleMeters - time * wind.GustFrequencyHz;
            const float gust = WindGustNoise(wind, dirX, dirZ, root.x, root.z, time);
            const float turb = WindValueNoise1D(arg * 3.1f, WindTurbulenceSeed);
            const float speed = wind.BaseSpeed * (1.0f + wind.GustStrength * (gust * 2.0f - 1.0f));
            const float lateral = wind.BaseSpeed * wind.Turbulence * (turb * 2.0f - 1.0f);
            const Math::Vector3 velocity(dirX * speed - dirZ * lateral, 0.0f, dirZ * speed + dirX * lateral);
            const float flutter = 0.9f + 0.2f * WindValueNoise1D(time * 0.7f + phase * 19.0f, 0u);
            const float stiffness = species.Stiffness > 0.05f ? species.Stiffness : 0.05f;
            const float response = ((h * 0.04f) * species.WindResponse) / stiffness;
            offset = offset + velocity * (response * flutter);
        }
        offset = offset + Math::Vector3(draw.Trample[0], 0.0f, draw.Trample[2]) * species.TrampleResponse;
        // 全体の濡れ度 0 の写し。GLSL の WetDroop の加算も同じ位置・順に置く。
        offset = offset + leanDirection * ((h * species.WetDroop) * 0.0f);
        const float offsetLength = GrassLength(offset);
        const float maxOffset = h * 0.8f;
        if (offsetLength > maxOffset)
        {
            offset = offset * (maxOffset / offsetLength);
        }

        // G4: grass.vert 119〜127 行「G4」の制御点と二次ベジエ。接線は位置には不要なので省く。
        const float offsetSquared = (offset.x * offset.x + offset.y * offset.y) + offset.z * offset.z;
        const float heightSquared = h * h - offsetSquared;
        const float tipHeight = std::sqrt(heightSquared > 0.0f ? heightSquared : 0.0f);
        const Math::Vector3 p1 = root + Math::Vector3(0.0f, h * 0.5f, 0.0f);
        const Math::Vector3 p2 = (root + Math::Vector3(0.0f, tipHeight, 0.0f)) + offset;
        const float inverseV = 1.0f - v;
        const Math::Vector3 center = (root * (inverseV * inverseV) + p1 * ((2.0f * inverseV) * v)) + p2 * (v * v);

        // G5: grass.vert 128〜136 行「G5」の水平ビルボード。前のカメラでも同じ式を再評価する。
        const Math::Vector3 view = GrassNormalize(Math::Vector3(camera[0] - center.x, 0.0f, camera[2] - center.z), facing);
        const float facingDot = facing.x * view.x + facing.z * view.z;
        const float turn = GrassSaturate((0.5f - std::abs(facingDot)) * 2.0f);
        const Math::Vector3 viewFacing = view * (facingDot < 0.0f ? -1.0f : 1.0f);
        const Math::Vector3 viewSide(-viewFacing.z, 0.0f, viewFacing.x);
        side = GrassNormalize(side + (viewSide - side) * turn, side);

        // G6: grass.vert 137〜154 行「G6」の先細りと最小画素幅。前の画素寸法 0 は現在値へ戻す。
        float halfWidth = (0.5f * w) * (1.0f - 0.9f * v);
        const float pixelScale = bHistory && camera[3] <= 0.0f ? draw.CameraPosition[3] : camera[3];
        if (draw.Surface[1] > 0.0f && halfWidth > 1.0e-6f)
        {
            const float pixelMeters = pixelScale * GrassLength(center - cameraPosition);
            const float targetHalfWidth = (0.5f * draw.Surface[1]) * pixelMeters;
            if (halfWidth < targetHalfWidth)
            {
                const float limit = draw.Surface[2] > 1.0f ? draw.Surface[2] : 1.0f;
                const float widen = targetHalfWidth / halfWidth;
                halfWidth = halfWidth * (widen < limit ? widen : limit);
            }
        }
        const float sideSign = GrassBladeIndexSide(vertexIndexValue) == 0u ? -1.0f : 1.0f;
        return center + side * (halfWidth * sideSign);
    }

    Math::Vector4 EvaluateGrassBladeVertexClip(const GrassDrawConstants& draw,
        const GrassSpeciesGpuData& species, const GrassBladeRecord& blade, uint32_t lod,
        uint32_t vertexIndexValue, bool bPrevious)
    {
        const Math::Vector3 position = EvaluateGrassBladeVertex(draw, species, blade, lod, vertexIndexValue, bPrevious);
        const bool bHistory = bPrevious && (draw.Flags[0] & kGrassDrawFlagPreviousCamera) != 0u;
        const Math::Vector4 viewPosition = GrassTransform(bHistory ? draw.PreviousView : draw.View, Math::Vector4(position, 1.0f));
        return GrassTransform(bHistory ? draw.PreviousProjection : draw.Projection, viewPosition);
    }

    void BuildGrassBladeIndices(const uint32_t (&lodSegments)[3],
        Container::VariableArray<uint32_t>& outIndices, uint32_t (&outFirstIndex)[3])
    {
        outIndices.clear();
        const size_t indexCount = (static_cast<size_t>(lodSegments[0]) + lodSegments[1] + lodSegments[2]) * 6u;
        outIndices.reserve(indexCount);
        uint32_t firstIndex = 0;
        for (uint32_t lod = 0; lod < 3; ++lod)
        {
            outFirstIndex[lod] = firstIndex;
            const uint32_t lodBits = lod << 16;
            for (uint32_t row = 0; row < lodSegments[lod]; ++row)
            {
                const uint32_t left = lodBits | (row << 1);
                const uint32_t right = left | 1u;
                const uint32_t nextLeft = lodBits | ((row + 1u) << 1);
                const uint32_t nextRight = nextLeft | 1u;
                // 隣り合う左右の行を、同じ巻き順の 2 三角形で結ぶ。
                outIndices.push_back(left);
                outIndices.push_back(right);
                outIndices.push_back(nextLeft);
                outIndices.push_back(right);
                outIndices.push_back(nextRight);
                outIndices.push_back(nextLeft);
            }
            firstIndex += lodSegments[lod] * 6u;
        }
    }
} // namespace NorvesLib::Core::Rendering
