#include "Rendering/Terrain/TerrainPlacementGround.h"

#include "Asset/AssetFileReader.h"
#include "Rendering/Terrain/TerrainHeightField.h"

#include <algorithm>
#include <bit>
#include <cmath>
#include <exception>
#include <limits>

namespace NorvesLib::Core::Rendering::Terrain
{
    namespace
    {
        /// 先に残りの長さを確かめ、固定幅の値をホストのバイト順に依存せず読む。
        struct Reader
        {
            Container::Span<const uint8_t> Bytes;
            size_t Offset = 0;
            size_t Remaining() const
            {
                return Bytes.size() - Offset;
            }
            uint64_t UInt(size_t width)
            {
                uint64_t value = 0;
                for (size_t i = 0; i < width; ++i)
                {
                    value |= static_cast<uint64_t>(Bytes[Offset++]) << (8u * i);
                }
                return value;
            }
            float Float()
            {
                return std::bit_cast<float>(static_cast<uint32_t>(UInt(4)));
            }
            double Double()
            {
                return std::bit_cast<double>(UInt(8));
            }
            bool Text(Container::AnsiString &output)
            {
                if (Remaining() < 4u)
                {
                    return false;
                }
                const uint64_t length = UInt(4);
                if (length == 0 || length > Remaining())
                {
                    return false;
                }
                for (size_t i = 0; i < length; ++i)
                {
                    if (Bytes[Offset + i] == 0)
                    {
                        return false;
                    }
                }
                output.append(reinterpret_cast<const char *>(Bytes.data() + Offset), static_cast<size_t>(length));
                Offset += static_cast<size_t>(length);
                return true;
            }
        };
    } // namespace

    Container::TSharedPtr<const TerrainPlacementGround> TerrainPlacementGround::Load(Container::AnsiStringView path,
                                                                                     Container::AnsiString &outError)
    {
        return Load(path, Asset::AssetFileReader(), outError);
    }

    Container::TSharedPtr<const TerrainPlacementGround> TerrainPlacementGround::Load(Container::AnsiStringView path,
                                                                                     const Asset::AssetFileReader &fileReader,
                                                                                     Container::AnsiString &outError)
    {
        outError.clear();
        try
        {
            const auto reject = [&](const char *reason) -> Container::TSharedPtr<const TerrainPlacementGround>
            {
                outError = reason;
                return {};
            };
            const Asset::AssetReadResult file = fileReader.Read(path);
            if (!file.Succeeded())
            {
                outError = "接地用の高さ格子を読めません: ";
                outError += file.Reason;
                return {};
            }
            Reader reader{file.Blob.GetSpan()};
            using namespace PlacementGroundFormat;
            if (reader.Remaining() < kHeaderBytes)
            {
                return reject("接地用の高さ格子の見出しが途中で切れています");
            }
            const uint64_t magic = reader.UInt(4);
            const uint64_t version = reader.UInt(4);
            const float blockMeters = reader.Float();
            const float spacing = reader.Float();
            const uint64_t side = reader.UInt(4);
            if (magic == kMagic && version == 1u)
            {
                return reject("接地用の高さ格子の版 1 は升目の信頼の印を持たないため読めません");
            }
            if (magic != kMagic || version != kVersion || blockMeters != kBlockMeters || spacing != kSpacing ||
                side != kSide)
            {
                return reject("接地用の高さ格子の形式・版・間隔が対応していません");
            }
            Container::TSharedPtr<TerrainPlacementGround> ground(new TerrainPlacementGround);
            ground->m_FirstX = reader.Double();
            ground->m_FirstZ = reader.Double();
            ground->m_MaxX = reader.Double();
            ground->m_MaxZ = reader.Double();
            const uint64_t blockCount = reader.UInt(4);
            const uint64_t sourceCount = reader.UInt(4);
            const double spanX = ground->m_MaxX - ground->m_FirstX;
            const double spanZ = ground->m_MaxZ - ground->m_FirstZ;
            if (!std::isfinite(ground->m_FirstX) || !std::isfinite(ground->m_FirstZ) ||
                !std::isfinite(ground->m_MaxX) || !std::isfinite(ground->m_MaxZ) || !(spanX > 0.0 && spanZ > 0.0) ||
                !(spanX / kBlockMeters <= INT32_MAX && spanZ / kBlockMeters <= INT32_MAX))
            {
                return reject("接地用の高さ格子の範囲が不正です");
            }
            if (sourceCount > reader.Remaining() / 18u || blockCount > reader.Remaining() / kBlockBytes ||
                (blockCount != 0 && sourceCount == 0))
            {
                return reject("接地用の高さ格子の個数がファイルの大きさと合いません");
            }
            ground->m_Sources.reserve(static_cast<size_t>(sourceCount));
            for (uint64_t i = 0; i < sourceCount; ++i)
            {
                TerrainPlacementGroundSource source;
                if (!reader.Text(source.LogicalPath) || !reader.Text(source.CookedPackage) || reader.Remaining() < 8u)
                {
                    return reject("接地用の高さ格子の区画の参照が壊れています");
                }
                source.CookedHash = reader.UInt(8);
                for (const auto &previous : ground->m_Sources)
                {
                    if (previous.LogicalPath == source.LogicalPath)
                    {
                        return reject("接地用の高さ格子の区画の参照が重複しています");
                    }
                }
                ground->m_Sources.push_back(std::move(source));
            }
            if (reader.Remaining() != blockCount * kBlockBytes)
            {
                return reject("接地用の高さ格子の塊の大きさが合いません");
            }
            ground->m_Blocks.resize(static_cast<size_t>(blockCount));
            for (Block &block : ground->m_Blocks)
            {
                block.X = std::bit_cast<int32_t>(static_cast<uint32_t>(reader.UInt(4)));
                block.Z = std::bit_cast<int32_t>(static_cast<uint32_t>(reader.UInt(4)));
                if (block.X < 0 || block.Z < 0 || block.X * static_cast<double>(kBlockMeters) >= spanX ||
                    block.Z * static_cast<double>(kBlockMeters) >= spanZ)
                {
                    return reject("接地用の高さ格子の塊が範囲の外です");
                }
                for (float &height : block.Heights)
                {
                    height = reader.Float();
                    if (std::isinf(height))
                    {
                        return reject("接地用の高さ格子の高さに無限大があります");
                    }
                }
                for (uint8_t &bits : block.ReliableCells)
                {
                    bits = static_cast<uint8_t>(reader.UInt(1));
                }
                for (uint32_t cell = 0; cell < kCells; ++cell)
                {
                    if ((block.ReliableCells[cell / 8u] & (1u << (cell % 8u))) == 0u)
                    {
                        continue;
                    }
                    const uint32_t i = (cell / kCellSide) * kSide + cell % kCellSide;
                    if (!std::isfinite(block.Heights[i]) || !std::isfinite(block.Heights[i + 1u]) ||
                        !std::isfinite(block.Heights[i + kSide]) || !std::isfinite(block.Heights[i + kSide + 1u]))
                    {
                        return reject("接地用の高さ格子の信頼の印が NaN の標本を指しています");
                    }
                }
            }
            const auto before = [](const Block &a, const Block &b)
            {
                return a.Z < b.Z || (a.Z == b.Z && a.X < b.X);
            };
            std::sort(ground->m_Blocks.begin(), ground->m_Blocks.end(), before);
            for (size_t i = 1; i < ground->m_Blocks.size(); ++i)
            {
                if (!before(ground->m_Blocks[i - 1], ground->m_Blocks[i]))
                {
                    return reject("接地用の高さ格子の塊が重複しています");
                }
            }
            return ground;
        }
        catch (const std::exception &)
        {
            outError = "接地用の高さ格子を読み込むためのメモリを確保できません";
            return {};
        }
    }

    PlacementGroundSample TerrainPlacementGround::Sample(float x, float z, float &outY) const
    {
        using namespace PlacementGroundFormat;
        if (!std::isfinite(x) || !std::isfinite(z) || x < m_FirstX || x > m_MaxX || z < m_FirstZ || z > m_MaxZ)
        {
            return PlacementGroundSample::NoBlock;
        }
        const double u = (static_cast<double>(x) - m_FirstX) / kBlockMeters;
        const double v = (static_cast<double>(z) - m_FirstZ) / kBlockMeters;
        const int32_t bx = static_cast<int32_t>(std::floor(u));
        const int32_t bz = static_cast<int32_t>(std::floor(v));
        // 境の上では西・北の塊もその点を含む。片側が疎な格子から落ちていても、もう片側を引ける。
        for (int dz = 0; dz <= (v == bz && bz > 0 ? 1 : 0); ++dz)
        {
            for (int dx = 0; dx <= (u == bx && bx > 0 ? 1 : 0); ++dx)
            {
                const int32_t targetX = bx - dx;
                const int32_t targetZ = bz - dz;
                const auto found =
                    std::lower_bound(m_Blocks.begin(), m_Blocks.end(), 0,
                                     [&](const Block &block, int)
                                     {
                                         return block.Z < targetZ || (block.Z == targetZ && block.X < targetX);
                                     });
                if (found == m_Blocks.end() || found->X != targetX || found->Z != targetZ)
                {
                    continue;
                }
                const double sx = std::clamp((u - targetX) * (kSide - 1u), 0.0, static_cast<double>(kSide - 1u));
                const double sz = std::clamp((v - targetZ) * (kSide - 1u), 0.0, static_cast<double>(kSide - 1u));
                const uint32_t ix = std::min(static_cast<uint32_t>(sx), kSide - 2u);
                const uint32_t iz = std::min(static_cast<uint32_t>(sz), kSide - 2u);
                const uint32_t cell = iz * kCellSide + ix;
                if ((found->ReliableCells[cell / 8u] & (1u << (cell % 8u))) == 0u)
                {
                    return PlacementGroundSample::Unreliable;
                }
                const float a = found->Heights[iz * kSide + ix];
                const float b = found->Heights[iz * kSide + ix + 1u];
                const float c = found->Heights[(iz + 1u) * kSide + ix];
                const float d = found->Heights[(iz + 1u) * kSide + ix + 1u];
                if (!std::isfinite(a) || !std::isfinite(b) || !std::isfinite(c) || !std::isfinite(d))
                {
                    return PlacementGroundSample::Unreliable;
                }
                const double tx = sx - ix;
                const double tz = sz - iz;
                outY = static_cast<float>((1.0 - tz) * ((1.0 - tx) * a + tx * b) + tz * ((1.0 - tx) * c + tx * d));
                return PlacementGroundSample::Baked;
            }
        }
        return PlacementGroundSample::NoBlock;
    }

    bool TerrainPlacementGround::SampleGround(const TerrainHeightField &field, float x, float z, float &outY) const
    {
        switch (Sample(x, z, outY))
        {
        case PlacementGroundSample::Baked:
            return true;
        case PlacementGroundSample::Unreliable:
            return false;
        case PlacementGroundSample::NoBlock:
            outY = field.SampleHeight(x, z);
            return true;
        }
        return false;
    }

    bool TerrainPlacementGround::IsCurrent(Container::Span<const TerrainPlacementGroundSource> sources) const
    {
        if (sources.size() != m_Sources.size())
        {
            return false;
        }
        for (size_t i = 0; i < sources.size(); ++i)
        {
            for (size_t j = 0; j < i; ++j)
            {
                if (sources[j].LogicalPath == sources[i].LogicalPath)
                {
                    return false;
                }
            }
            const auto found = std::find_if(m_Sources.begin(), m_Sources.end(),
                                            [&](const auto &stored)
                                            {
                                                return stored.LogicalPath == sources[i].LogicalPath &&
                                                       stored.CookedHash == sources[i].CookedHash;
                                            });
            if (found == m_Sources.end())
            {
                return false;
            }
        }
        return true;
    }
} // namespace NorvesLib::Core::Rendering::Terrain
