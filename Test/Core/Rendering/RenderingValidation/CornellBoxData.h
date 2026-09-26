// Cornell Bowers公開データ（https://bowers.cornell.edu/computer-graphics/data）のCornell box。
// 実測寸法(mm)・反射率・面光源・カメラを、R4のCornell fixtureとPTの公開参照比較が共有する。
#pragma once

#include "Container/VariableArray.h"
#include "Rendering/ProceduralMeshGenerator.h"

#include <cmath>
#include <cstdint>

namespace NorvesLib::Test::RenderingValidation::CornellBox
{
    // 実測寸法(mm)からworld単位への倍率。
    inline constexpr float WorldScale = 0.01f;

    // 公開データの頂点順の四角形(mm)。
    struct Quad
    {
        float Positions[4][3];
    };

    inline constexpr Quad Floor = {{
        {552.8f, 0.0f, 0.0f}, {0.0f, 0.0f, 0.0f}, {0.0f, 0.0f, 559.2f}, {549.6f, 0.0f, 559.2f}}};

    // 天井は面光源の穴を囲む4枚（左・右・手前・奥）。
    inline constexpr Quad Ceiling[4] = {
        {{{213.0f, 548.8f, 0.0f}, {213.0f, 548.8f, 559.2f}, {0.0f, 548.8f, 559.2f}, {0.0f, 548.8f, 0.0f}}},
        {{{556.0f, 548.8f, 0.0f}, {556.0f, 548.8f, 559.2f}, {343.0f, 548.8f, 559.2f}, {343.0f, 548.8f, 0.0f}}},
        {{{343.0f, 548.8f, 0.0f}, {343.0f, 548.8f, 227.0f}, {213.0f, 548.8f, 227.0f}, {213.0f, 548.8f, 0.0f}}},
        {{{343.0f, 548.8f, 332.0f}, {343.0f, 548.8f, 559.2f}, {213.0f, 548.8f, 559.2f}, {213.0f, 548.8f, 332.0f}}}};

    inline constexpr Quad BackWall = {{
        {549.6f, 0.0f, 559.2f}, {0.0f, 0.0f, 559.2f}, {0.0f, 548.8f, 559.2f}, {556.0f, 548.8f, 559.2f}}};

    inline constexpr Quad ShortBlock[5] = {
        {{{130.0f, 165.0f, 65.0f}, {82.0f, 165.0f, 225.0f}, {240.0f, 165.0f, 272.0f}, {290.0f, 165.0f, 114.0f}}},
        {{{290.0f, 0.0f, 114.0f}, {290.0f, 165.0f, 114.0f}, {240.0f, 165.0f, 272.0f}, {240.0f, 0.0f, 272.0f}}},
        {{{130.0f, 0.0f, 65.0f}, {130.0f, 165.0f, 65.0f}, {290.0f, 165.0f, 114.0f}, {290.0f, 0.0f, 114.0f}}},
        {{{82.0f, 0.0f, 225.0f}, {82.0f, 165.0f, 225.0f}, {130.0f, 165.0f, 65.0f}, {130.0f, 0.0f, 65.0f}}},
        {{{240.0f, 0.0f, 272.0f}, {240.0f, 165.0f, 272.0f}, {82.0f, 165.0f, 225.0f}, {82.0f, 0.0f, 225.0f}}}};

    inline constexpr Quad TallBlock[5] = {
        {{{423.0f, 330.0f, 247.0f}, {265.0f, 330.0f, 296.0f}, {314.0f, 330.0f, 456.0f}, {472.0f, 330.0f, 406.0f}}},
        {{{423.0f, 0.0f, 247.0f}, {423.0f, 330.0f, 247.0f}, {472.0f, 330.0f, 406.0f}, {472.0f, 0.0f, 406.0f}}},
        {{{472.0f, 0.0f, 406.0f}, {472.0f, 330.0f, 406.0f}, {314.0f, 330.0f, 456.0f}, {314.0f, 0.0f, 456.0f}}},
        {{{314.0f, 0.0f, 456.0f}, {314.0f, 330.0f, 456.0f}, {265.0f, 330.0f, 296.0f}, {265.0f, 0.0f, 296.0f}}},
        {{{265.0f, 0.0f, 296.0f}, {265.0f, 330.0f, 296.0f}, {423.0f, 330.0f, 247.0f}, {423.0f, 0.0f, 247.0f}}}};

    // 緑の壁（カメラから見て右、x=0側）と赤の壁（左、x≒556側）。
    inline constexpr Quad GreenWall = {{
        {0.0f, 0.0f, 559.2f}, {0.0f, 0.0f, 0.0f}, {0.0f, 548.8f, 0.0f}, {0.0f, 548.8f, 559.2f}}};
    inline constexpr Quad RedWall = {{
        {552.8f, 0.0f, 0.0f}, {549.6f, 0.0f, 559.2f}, {556.0f, 548.8f, 559.2f}, {556.0f, 548.8f, 0.0f}}};

    inline constexpr Quad AreaLight = {{
        {343.0f, 548.8f, 227.0f}, {343.0f, 548.8f, 332.0f}, {213.0f, 548.8f, 332.0f}, {213.0f, 548.8f, 227.0f}}};

    // 拡散反射率(RGB)と面光源の色・輝度。
    inline constexpr float WhiteReflectance[3] = {0.712f, 0.744f, 0.765f};
    inline constexpr float RedReflectance[3] = {0.660f, 0.062f, 0.063f};
    inline constexpr float GreenReflectance[3] = {0.114f, 0.406f, 0.104f};
    inline constexpr float LightReflectance[3] = {0.78f, 0.78f, 0.78f};
    inline constexpr float LightColor[3] = {1.378f, 0.937f, 0.482f};
    inline constexpr float LightLuminanceNits = 45000.0f;

    // カメラ（world単位）。縦の視野39.31度は焦点距離35 mm・フィルム25 mm。公開RGBEは512x512。
    inline constexpr float CameraPosition[3] = {2.78f, 2.73f, -8.0f};
    inline constexpr float CameraTarget[3] = {2.78f, 2.73f, 0.0f};
    inline constexpr float CameraFieldOfViewDegrees = 39.31f;
    inline constexpr float CameraNearPlane = 0.05f;
    inline constexpr float CameraFarPlane = 20.0f;
    inline constexpr uint32_t ImageWidth = 512u;
    inline constexpr uint32_t ImageHeight = 512u;

    // 四角形をworld単位の2枚の三角形として追加する。GBufferはFrontFace::Clockwiseで描画するため、
    // indicesは(0,2,1)/(0,3,2)を使う。これはラスタ面の法線を外側へ向ける順序であり、vertex normalは
    // 閉じたCornell室の内側を向けるため、ラスタ面とは反対側の幾何法線を元の頂点順序から保持する。
    // 通常のメッシュ経路やDDGI shaderでは表裏を補正しない。
    inline void AppendQuad(Core::Container::VariableArray<Core::Rendering::Mesh3DVertex>& outVertices,
                           Core::Container::VariableArray<uint32_t>& outIndices,
                           const Quad& quad)
    {
        const float (&positions)[4][3] = quad.Positions;
        const uint32_t firstVertex = static_cast<uint32_t>(outVertices.size());
        const double edgeAX = static_cast<double>(positions[1][0] - positions[0][0]);
        const double edgeAY = static_cast<double>(positions[1][1] - positions[0][1]);
        const double edgeAZ = static_cast<double>(positions[1][2] - positions[0][2]);
        const double edgeBX = static_cast<double>(positions[2][0] - positions[0][0]);
        const double edgeBY = static_cast<double>(positions[2][1] - positions[0][1]);
        const double edgeBZ = static_cast<double>(positions[2][2] - positions[0][2]);
        double normalX = edgeAY * edgeBZ - edgeAZ * edgeBY;
        double normalY = edgeAZ * edgeBX - edgeAX * edgeBZ;
        double normalZ = edgeAX * edgeBY - edgeAY * edgeBX;
        const double normalLength = std::sqrt(normalX * normalX + normalY * normalY + normalZ * normalZ);
        if (normalLength > 0.0)
        {
            normalX /= normalLength;
            normalY /= normalLength;
            normalZ /= normalLength;
        }
        constexpr float texCoords[4][2] = {
            {0.0f, 0.0f},
            {1.0f, 0.0f},
            {1.0f, 1.0f},
            {0.0f, 1.0f}};
        for (uint32_t vertexIndex = 0; vertexIndex < 4u; ++vertexIndex)
        {
            Core::Rendering::Mesh3DVertex vertex{};
            for (uint32_t component = 0; component < 3u; ++component)
            {
                vertex.Position[component] = positions[vertexIndex][component] * WorldScale;
            }
            vertex.Normal[0] = static_cast<float>(normalX);
            vertex.Normal[1] = static_cast<float>(normalY);
            vertex.Normal[2] = static_cast<float>(normalZ);
            vertex.TexCoord[0] = texCoords[vertexIndex][0];
            vertex.TexCoord[1] = texCoords[vertexIndex][1];
            outVertices.push_back(vertex);
        }

        outIndices.push_back(firstVertex);
        outIndices.push_back(firstVertex + 2u);
        outIndices.push_back(firstVertex + 1u);
        outIndices.push_back(firstVertex);
        outIndices.push_back(firstVertex + 3u);
        outIndices.push_back(firstVertex + 2u);
    }
}
