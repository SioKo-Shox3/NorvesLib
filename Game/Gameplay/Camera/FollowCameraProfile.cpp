#include "Gameplay/Camera/FollowCameraProfile.h"
#include "Text/JsonDocument.h"
#include <cmath>
namespace Game::Gameplay
{
    bool ParseFollowCameraProfile(NorvesLib::Core::Container::Span<const uint8_t> bytes, FollowCameraProfile& out)
    {
        using namespace NorvesLib::Core;
        if (bytes.empty() || bytes.size() > 65536)
            return false;
        JsonDocument document;
        if (!JsonDocument::TryParseUtf8(bytes, document))
            return false;
        const auto root = document.GetRoot();
        if (!root.IsObject() || root.FindMember("version").AsNumber() != 1)
            return false;
        FollowCameraProfile next;
        auto number = [](JsonValue object, const char* key, float& value, double min, double max) {
            if (!object.IsValid())
                return true;
            if (!object.IsObject())
                return false;
            const auto item = object.FindMember(key);
            if (!item.IsValid())
                return true;
            const double n = item.AsNumber();
            if (!item.IsNumber() || !std::isfinite(n) || n < min || n > max)
                return false;
            value = float(n);
            return true;
        };
        const auto half = root.FindMember("halfLives"), speed = root.FindMember("speedEffects");
        const auto collision = root.FindMember("collision"), fade = root.FindMember("fade");
        const auto trauma = root.FindMember("trauma"), lock = root.FindMember("lockOn");
        const auto focus = root.FindMember("focus");
        if (!number(half, "xz", next.PositionHalfLives.x, 0, 10) ||
            !number(half, "y", next.PositionHalfLives.y, 0, 10) || !number(half, "yaw", next.YawHalfLife, 0, 10) ||
            !number(half, "fov", next.FovHalfLife, 0, 10) || !number(half, "focus", next.FocusHalfLife, 0, 10) ||
            !number(root, "yawReturnDelay", next.YawReturnDelay, 0, 60) ||
            !number(root, "minimumYawSpeed", next.MinimumYawSpeed, 0, 100) ||
            !number(root, "maximumArmLength", next.MaximumArmLength, .1, 100) ||
            !number(speed, "vRef", next.SpeedReference, .01, 1000) || !number(speed, "armGain", next.ArmGain, 0, 100) ||
            !number(speed, "fovGain", next.FovGain, 0, 90) || !number(speed, "baseFov", next.BaseFov, 10, 120) ||
            !number(speed, "strength", next.SpeedEffectsStrength, 0, 1) ||
            !number(collision, "probeRadius", next.Collision.ProbeRadius, .001, 10) ||
            !number(collision, "margin", next.Collision.Margin, 0, 10) ||
            !number(collision, "extendHalfLife", next.Collision.ExtendHalfLife, 0, 10) ||
            !number(collision, "maximumExtendSpeed", next.Collision.MaximumExtendSpeed, 0, 1000) ||
            !number(fade, "start", next.FadeStart, 0, 100) || !number(fade, "end", next.FadeEnd, 0, 100) ||
            !number(trauma, "decay", next.TraumaDecay, 0, 100) ||
            !number(trauma, "strength", next.ShakeStrength, 0, 1) ||
            !number(trauma, "yaw", next.ShakeDegrees.x, 0, 15) ||
            !number(trauma, "pitch", next.ShakeDegrees.y, 0, 15) ||
            !number(trauma, "roll", next.ShakeDegrees.z, 0, 15) || !number(lock, "maxBias", next.LockBias, 0, .5) ||
            !number(lock, "breakThreshold", next.LockBreakDegrees, 0, 90) ||
            !number(lock, "armGain", next.LockArmGain, 0, 2) ||
            !number(focus, "maximumDistance", next.MaximumFocusDistance, .1, 1000))
            return false;
        if (focus.IsValid())
        {
            if (!focus.IsObject())
                return false;
            const auto enabled = focus.FindMember("enabled");
            if (enabled.IsValid())
            {
                if (!enabled.IsBoolean())
                    return false;
                next.bAutoFocus = enabled.AsBool();
            }
        }
        next.PositionHalfLives.z = next.PositionHalfLives.x;
        if (next.FadeEnd <= next.FadeStart || next.BaseFov + next.FovGain > 150)
            return false;
        out = next;
        return true;
    }
} // namespace Game::Gameplay
