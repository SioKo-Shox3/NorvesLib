#include "Resource/ImportSettings.h"
#ifdef NDEBUG
#undef NDEBUG
#endif
#include <cassert>
#include <cstring>
#include <iostream>
#include <limits>

using namespace NorvesLib::Core::AssetImport;
namespace
{
    bool Same(const CanonicalSettings& a, const CanonicalSettings& b)
    {
        return a.Size == b.Size && std::memcmp(a.Bytes, b.Bytes, a.Size) == 0;
    }
}

int main()
{
    ImportSettings settings;
    assert(ValidateSettings(settings) == SettingsResult::Success);
    const auto identity = EncodeCanonicalSettings(settings);
    const uint8_t expected[] = {
        1,0,0,0, 0,0,0,0,0,0,240,63, 0, 0,0,0,0,0,0,240,63,
        2,4,0,0, 0,0,0,0,0,0,0,0, 0,0,0,0,0,0,0,0, 0,0,0,0,0,0,0,0, 0,0,0
    };
    static_assert(sizeof(expected) == CanonicalSettingsSize);
    assert(identity.Size == sizeof(expected) && std::memcmp(identity.Bytes, expected, sizeof(expected)) == 0);
    for (uint8_t up = 0; up < 6; ++up)
    {
        for (uint8_t forward = 0; forward < 6; ++forward)
        {
            settings = {};
            settings.Up = static_cast<SignedAxis>(up);
            settings.Forward = static_cast<SignedAxis>(forward);
            const bool bValid = up / 2 != forward / 2;
            assert((ValidateSettings(settings) == SettingsResult::Success) == bValid);
            assert((EncodeCanonicalSettings(settings).Size != 0) == bValid);
        }
    }
    for (const double bad : {0.0, -1.0, std::numeric_limits<double>::infinity(),
        -std::numeric_limits<double>::infinity(), std::numeric_limits<double>::quiet_NaN()})
    {
        settings = {}; settings.Scale = bad;
        assert(ValidateSettings(settings) == SettingsResult::InvalidValue);
        assert(EncodeCanonicalSettings(settings).Size == 0);
        settings = {}; settings.Fit = FitAxis::Up; settings.FitMeters = bad;
        assert(ValidateSettings(settings) == SettingsResult::InvalidValue);
    }
    settings = {}; settings.Up = static_cast<SignedAxis>(255);
    assert(ValidateSettings(settings) == SettingsResult::InvalidValue);
    settings = {}; settings.Forward = static_cast<SignedAxis>(255);
    assert(ValidateSettings(settings) == SettingsResult::InvalidValue);
    settings = {}; settings.Fit = static_cast<FitAxis>(255);
    assert(ValidateSettings(settings) == SettingsResult::InvalidValue);
    settings = {}; settings.Origin = static_cast<OriginMode>(255);
    assert(ValidateSettings(settings) == SettingsResult::InvalidValue);
    settings = {}; settings.Winding = static_cast<WindingMode>(255);
    assert(ValidateSettings(settings) == SettingsResult::InvalidValue);
    settings = {}; settings.FitMeters = 2;
    assert(ValidateSettings(settings) == SettingsResult::InvalidValue);
    settings = {}; settings.Fit = FitAxis::Up; settings.Scale = 2;
    assert(ValidateSettings(settings) == SettingsResult::InvalidValue);
    settings = {}; settings.OriginOffset[0] = 1;
    assert(ValidateSettings(settings) == SettingsResult::InvalidValue);
    settings = {}; settings.Origin = OriginMode::Custom; settings.OriginOffset[1] = std::numeric_limits<double>::quiet_NaN();
    assert(ValidateSettings(settings) == SettingsResult::InvalidValue);
    settings = {}; settings.Origin = OriginMode::SurfaceCentroid;
    assert(ValidateSettings(settings) == SettingsResult::UnsupportedFeature);
    assert(EncodeCanonicalSettings(settings).Size == 0);
    settings = {}; settings.OriginOffset[0] = -0.0;
    assert(Same(identity, EncodeCanonicalSettings(settings)));

    // 有効な各fieldの変更は正規化bytesへ現れる。
    for (int field = 0; field < 12; ++field)
    {
        settings = {};
        switch (field)
        {
        case 0: settings.Scale = 2; break;
        case 1: settings.Fit = FitAxis::Up; break;
        case 2: settings.Fit = FitAxis::Up; settings.FitMeters = 0.6; break;
        case 3: settings.Up = SignedAxis::NegativeY; break;
        case 4: settings.Forward = SignedAxis::NegativeZ; break;
        case 5: settings.bMirrorX = true; break;
        case 6: settings.Origin = OriginMode::BoundsCenter; break;
        case 7: settings.Origin = OriginMode::Custom; settings.OriginOffset[0] = 2; break;
        case 8: settings.Winding = WindingMode::Flip; break;
        case 9: settings.bFlipU = true; break;
        case 10: settings.bFlipV = true; break;
        case 11: settings.Scale = std::numeric_limits<double>::denorm_min(); break;
        }
        assert(ValidateSettings(settings) == SettingsResult::Success);
        assert(!Same(identity, EncodeCanonicalSettings(settings)));
    }
    settings = {};
    settings.Scale = 2.0;
    const auto scale2 = EncodeCanonicalSettings(settings);
    assert(scale2.Bytes[10] == 0 && scale2.Bytes[11] == 64);
    std::cout << "ImportSettingsValueTest PASS: axes_values_canonical_layout_zero_bits\n";
    return 0;
}
