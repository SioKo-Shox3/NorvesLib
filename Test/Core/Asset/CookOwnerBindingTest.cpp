// owner wire固定vectorはPython hashlibで独立算出した値。file I/Oを行わない。
#include "Tools/AssetCook/CookOwnerBinding.h"
#include "Tools/AssetCook/CookOwnedState.h"
#include <cstdio>
#include <cstdlib>
#include <cstring>
#define CHECK(x)                                                                                                       \
    do                                                                                                                 \
    {                                                                                                                  \
        if (!(x))                                                                                                      \
        {                                                                                                              \
            std::fprintf(stderr, "line %d: %s\n", __LINE__, #x);                                                       \
            std::abort();                                                                                              \
        }                                                                                                              \
    } while (false)
namespace OwnerIdTest
{
    using namespace NorvesLib::Tools::AssetCook;
    using OwnerText = NorvesLib::Core::Container::AnsiString;
    void Vector(const CookOwnerIdentity& identity, const char* expected)
    {
        OwnerText id = "held", error;
        CHECK(ComputeCookOwnerId(identity, id, error));
        CHECK(error.empty() && id == expected);
        OwnerText again;
        CHECK(ComputeCookOwnerId(identity, again, error));
        CHECK(id == again);
    }
    void Bad(const CookOwnerIdentity& identity)
    {
        OwnerText id = "held", error;
        CHECK(!ComputeCookOwnerId(identity, id, error));
        CHECK(id == "held" && !error.empty());
    }
} // namespace OwnerIdTest
int main()
{
#if !defined(_WIN32)
    return 125;
#else
    using namespace OwnerIdTest;
    Vector({"C:/Specs/level.json", "C:/Runtime/level", "manifest.json"}, "9e67aea62876c7796c613f3dec2234b2");
    Vector({"a", "bc", "manifest.json"}, "2961e0a985a1be94353c6bc1f3c773f6");
    Vector({"ab", "c", "manifest.json"}, "1355fdd426e3571da689ab40a365f99d");
    Vector({"C:/\xe8\xb3\x87\xe6\x96\x99/\xe7\x8a\xac\xf0\x9f\x90\xba.json", "C:/Runtime/\xe7\x8a\xac\xf0\x9f\x90\xba",
            "manifests/main.json"},
           "47452190880092222e89b32c9659fc03");
    const CookOwnerIdentity original{"C:/Specs/level.json", "C:/Runtime/level", "manifest.json"};
    OwnerText first, error, id;
    CHECK(ComputeCookOwnerId(original, first, error));
    auto changed = original;
    changed.CanonicalSpecLocator = "C:/Specs/other.json";
    CHECK(ComputeCookOwnerId(changed, id, error) && id != first);
    changed = original;
    changed.CanonicalFinalRuntimeRootIdentity = "C:/Runtime/other";
    CHECK(ComputeCookOwnerId(changed, id, error) && id != first);
    changed = original;
    changed.ManifestName = "other.json";
    CHECK(ComputeCookOwnerId(changed, id, error) && id != first);
    changed = original;
    changed.CanonicalSpecLocator = "C:/Specs/LEVEL.json";
    CHECK(ComputeCookOwnerId(changed, id, error) && id != first);
    changed = original;
    changed.CanonicalSpecLocator = "C:/Specs/\xc3\xa9.json";
    OwnerText composed;
    CHECK(ComputeCookOwnerId(changed, composed, error));
    changed.CanonicalSpecLocator = "C:/Specs/e\xcc\x81.json";
    CHECK(ComputeCookOwnerId(changed, id, error) && id != composed);
    for (unsigned field = 0; field < 3; ++field)
    {
        changed = original;
        auto& value = field == 0   ? changed.CanonicalSpecLocator
                      : field == 1 ? changed.CanonicalFinalRuntimeRootIdentity
                                   : changed.ManifestName;
        value.clear();
        Bad(changed);
        value = OwnerText(MaximumCookOwnerIdentityBytes, 'x');
        CHECK(ComputeCookOwnerId(changed, id, error));
        value.push_back('x');
        Bad(changed);
        value = "a";
        value.push_back('\0');
        value.push_back('b');
        Bad(changed);
        value = "\xc0\x80";
        Bad(changed);
        value = "\xed\xa0\x80";
        Bad(changed);
        value = "\xf4\x90\x80\x80";
        Bad(changed);
        value = "\xe2\x82";
        Bad(changed);
    }
    for (const char* name : {"../manifest.json", "/manifest.json", "a//b.json", "a/./b.json", "a\\b.json", "a:stream",
                             "CON.json", "a.", "a ", "Assets/manifest.json"})
    {
        changed = original;
        changed.ManifestName = name;
        Bad(changed);
    }
    changed = original;
    CHECK(!ComputeCookOwnerId(changed, changed.CanonicalSpecLocator, error));
    CHECK(changed.CanonicalSpecLocator == original.CanonicalSpecLocator);
    OwnerText held = "held";
    CHECK(!ComputeCookOwnerId(original, held, held));
    CHECK(held == "held");
    changed = original;
    CHECK(!ComputeCookOwnerId(changed, held, changed.ManifestName));
    CHECK(changed.ManifestName == original.ManifestName && held == "held");
    CookOwnedState state;
    state.Binding = {first, "C:/Runtime/level", "manifest.json"};
    OwnerText json;
    CHECK(SerializeCookOwnedState(state, json, error));
    CookOwnedState parsed;
    CHECK(ParseCookOwnedState({reinterpret_cast<const uint8_t*>(json.data()), json.size()}, state.Binding, parsed,
                              error));
    CHECK(parsed.Binding.OwnerId == first);
    auto other = original;
    other.CanonicalSpecLocator = "C:/Specs/other.json";
    auto expected = state.Binding;
    CHECK(ComputeCookOwnerId(other, expected.OwnerId, error));
    CHECK(!ParseCookOwnedState({reinterpret_cast<const uint8_t*>(json.data()), json.size()}, expected, parsed, error));
    CHECK(parsed.Binding.OwnerId == first);
    std::puts("COOK_OWNER_ID result=pass fixed_sha256_vectors_length_delimited_utf8_bounds_hold_state_binding");
    return 0;
#endif
}
