#include <cstdint>
#include <exception>
#include <iostream>
#include <stdexcept>

std::uint64_t MakeSyntheticEmitterId(std::uint32_t index, std::uint32_t generation);

namespace
{
    constexpr std::uint64_t SyntheticMarker = std::uint64_t{1} << 63u;
    constexpr std::uint64_t GenerationMask = 0x7FFFFFFFu;

    void Require(bool condition, const char* message)
    {
        if (!condition)
        {
            throw std::runtime_error(message);
        }
    }
} // namespace

int main()
{
    try
    {
        const std::uint64_t first = MakeSyntheticEmitterId(0u, 1u);
        Require((first & SyntheticMarker) != 0u, "missing synthetic marker");
        Require((first & 0xFFFFFFFFu) == 1u, "low field must contain index + 1");
        Require(((first >> 32u) & GenerationMask) == 1u, "generation field mismatch");
        Require(first == 0x8000000100000001ull, "exact value mismatch for (0, 1)");

        const std::uint64_t recycled = MakeSyntheticEmitterId(41u, 7u);
        Require(recycled == 0x800000070000002aull, "exact value mismatch for (41, 7)");
        Require(recycled == MakeSyntheticEmitterId(41u, 7u), "identity must be stable");
        Require(recycled != MakeSyntheticEmitterId(41u, 8u), "generation must affect identity");
        Require(recycled != MakeSyntheticEmitterId(42u, 7u), "index must affect identity");

        const std::uint64_t maximumGeneration = MakeSyntheticEmitterId(0u, 0x7FFFFFFFu);
        Require(((maximumGeneration >> 32u) & GenerationMask) == 0x7FFFFFFFu,
                "maximum generation field mismatch");
    }
    catch (const std::exception& exception)
    {
        std::cerr << exception.what() << '\n';
        return 1;
    }

    return 0;
}
