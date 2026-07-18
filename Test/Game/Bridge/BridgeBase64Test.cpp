#include "Bridge/BridgeBase64.h"

#include <cassert>
#include <cstdint>
#include <iostream>
#include <string>

namespace
{
    void AssertEncoded(const uint8_t* bytes, std::size_t byteCount, const std::string& expected)
    {
        const std::string encoded = Game::Bridge::EncodeBridgeBase64(bytes, byteCount);
        assert(encoded == expected);
    }

    void TestBase64()
    {
        AssertEncoded(nullptr, 0, "");

        const uint8_t m[] = { 'M' };
        AssertEncoded(m, sizeof(m), "TQ==");

        const uint8_t ma[] = { 'M', 'a' };
        AssertEncoded(ma, sizeof(ma), "TWE=");

        const uint8_t man[] = { 'M', 'a', 'n' };
        AssertEncoded(man, sizeof(man), "TWFu");

        const uint8_t binary[] = { 0u, 1u, 2u, 253u, 254u, 255u };
        AssertEncoded(binary, sizeof(binary), "AAEC/f7/");
    }
} // namespace

int main()
{
    TestBase64();

    std::cout << "BridgeBase64Test passed\n";
    return 0;
}
