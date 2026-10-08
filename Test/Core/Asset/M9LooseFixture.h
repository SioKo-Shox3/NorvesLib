#pragma once
#include <cstddef>
#include <cstdint>
// ValidU8Float.gltfの416byte bufferを既存skeletal試験と依存試験で共有する。
// writerはlittle-endianの同じ境界を使い、fixture生成とエンジン実装を混同しない。
namespace NorvesLib::Tests::AssetFixtures
{
    template<typename Bytes, typename FloatWriter, typename ShortWriter, typename MatrixWriter>
    Bytes BuildM9LooseBuffer(FloatWriter writeFloat, ShortWriter writeLe16, MatrixWriter writeMatrix)
    {
        Bytes bytes(416, 0);
        constexpr float positions[9] = {0.0f, 0.0f, 0.0f, 1.0f, 0.0f, 0.0f, 0.0f, 1.0f, 0.0f};
        constexpr float normals[9] = {0.0f, 0.0f, 1.0f, 0.0f, 0.0f, 1.0f, 0.0f, 0.0f, 1.0f};
        constexpr float texCoords[6] = {0.0f, 0.0f, 1.0f, 0.0f, 0.0f, 1.0f};
        constexpr uint8_t joints[12] = {0, 1, 0, 0, 0, 1, 0, 0, 1, 0, 0, 0};
        constexpr float weights[12] = {
            0.75f, 0.25f, 0.0f, 0.0f,
            0.5f, 0.5f, 0.0f, 0.0f,
            1.0f, 0.0f, 0.0f, 0.0f};
        for (size_t index = 0; index < 9; ++index)
        {
            writeFloat(bytes, index * sizeof(float), positions[index]);
            writeFloat(bytes, 36 + index * sizeof(float), normals[index]);
        }
        for (size_t index = 0; index < 6; ++index)
        {
            writeFloat(bytes, 72 + index * sizeof(float), texCoords[index]);
        }
        for (size_t index = 0; index < 12; ++index)
        {
            bytes[96 + index] = joints[index];
            writeFloat(bytes, 132 + index * sizeof(float), weights[index]);
        }
        writeLe16(bytes, 216, 0);
        writeLe16(bytes, 218, 1);
        writeLe16(bytes, 220, 2);
        writeMatrix(bytes, 224, 0.0f);
        writeMatrix(bytes, 288, -1.0f);
        writeFloat(bytes, 352, 0.0f);
        writeFloat(bytes, 356, 2.0f);
        constexpr float translations[6] = {0.0f, 1.0f, 0.0f, 0.0f, 3.0f, 0.0f};
        constexpr float rotations[8] = {0.0f, 0.0f, 0.0f, 1.0f, 0.0f, 0.0f, 1.0f, 0.0f};
        for (size_t index = 0; index < 6; ++index)
        {
            writeFloat(bytes, 360 + index * sizeof(float), translations[index]);
        }
        for (size_t index = 0; index < 8; ++index)
        {
            writeFloat(bytes, 384 + index * sizeof(float), rotations[index]);
        }
        return bytes;
    }
}
