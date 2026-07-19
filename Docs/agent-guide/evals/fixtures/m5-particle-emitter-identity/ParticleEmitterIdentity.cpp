#include <cstdint>

std::uint64_t MakeSyntheticEmitterId(std::uint32_t index, std::uint32_t generation)
{
    return (std::uint64_t{1} << 63u) | static_cast<std::uint64_t>(index + 1u);
}
