// 薄レンズの錯乱円とFramePacket由来の時間・並進補間を反証する。
#include "Rendering/PathTracingCamera.h"

#include <algorithm>
#include <cassert>
#include <cmath>
#include <iostream>

using namespace NorvesLib::Core::Rendering;

int main()
{
    CameraProxy current;
    current.CameraId = 7u;
    current.PositionX = 2.0f;
    current.FieldOfView = 60.0f;
    current.Aperture = 0.7f;
    current.FocusDistance = 0.3f;
    current.ShutterSpeed = 1.0f / 60.0f;
    CameraProxy previous = current;
    previous.PositionX = 0.0f;

    const PathTracingCameraSample first = SamplePathTracingCamera(
        current, &previous, 1.0f / 60.0f, 0u);
    const PathTracingCameraSample repeat = SamplePathTracingCamera(
        current, &previous, 1.0f / 60.0f, 0u);
    assert(first.ShutterTime == repeat.ShutterTime);
    assert(std::abs(first.ShutterTime - 0.2f) < 1.0e-6f);
    assert(std::abs(first.Camera.PositionX - 0.4f) < 1.0e-6f);
    assert(first.bThinLens);
    for (uint32_t axis = 0u; axis < 3u; ++axis)
    {
        assert(first.LensOffset[axis] == repeat.LensOffset[axis]);
    }
    const PathTracingCameraSample halfShutter = SamplePathTracingCamera(
        current, &previous, 1.0f / 30.0f, 0u);
    assert(std::abs(halfShutter.ShutterTime - 0.6f) < 1.0e-6f);
    assert(std::abs(halfShutter.Camera.PositionX - 1.2f) < 1.0e-6f);
    previous.CameraId = 8u;
    const PathTracingCameraSample mismatched = SamplePathTracingCamera(
        current, &previous, 1.0f / 60.0f, 0u);
    assert(mismatched.Camera.PositionX == current.PositionX);

    constexpr uint32_t imageHeight = 1080u;
    const double focalLength = 0.024 /
        (2.0 * std::tan(60.0 * 3.14159265358979323846 / 360.0));
    const double expected = (focalLength / 0.7) * focalLength *
        std::abs(0.6 - 0.3) / (0.6 * (0.3 - focalLength)) *
        imageHeight / 0.024;
    const float actual = ComputePathTracingCocPixels(current, 0.6f,
                                                      imageHeight);
    assert(std::abs(static_cast<double>(actual) - expected) < 1.0e-4);
    const double imageDistance = focalLength * 0.3 / (0.3 - focalLength);
    double sampledRadius = 0.0;
    for (uint32_t sample = 0u; sample < 1024u; ++sample)
    {
        const PathTracingCameraSample lens = SamplePathTracingCamera(
            current, nullptr, 0.0f, sample);
        const double lensRadius = std::hypot(lens.LensOffset[0],
                                              lens.LensOffset[1]);
        sampledRadius = std::max(sampledRadius,
            lensRadius * std::abs(1.0 - 0.6 / 0.3));
    }
    const double sampledCoc = 2.0 * sampledRadius * imageDistance /
        0.6 * imageHeight / 0.024;
    assert(std::abs(sampledCoc - expected) / expected < 0.001);
    assert(ComputePathTracingCocPixels(current, 0.3f, imageHeight) == 0.0f);
    current.FocusDistance = 0.0f;
    assert(ComputePathTracingCocPixels(current, 0.6f, imageHeight) == 0.0f);
    assert(!SamplePathTracingCamera(current, nullptr, 0.0f, 0u).bThinLens);

    const float oldTransform[12] = {
        1.0f, 0.0f, 0.0f, -2.0f,
        0.0f, 1.0f, 0.0f, 0.0f,
        0.0f, 0.0f, 1.0f, 0.0f};
    const float newTransform[12] = {
        1.0f, 0.0f, 0.0f, 2.0f,
        0.0f, 1.0f, 0.0f, 0.0f,
        0.0f, 0.0f, 1.0f, 0.0f};
    float result[12] = {};
    assert(InterpolatePathTracingTransform(oldTransform, newTransform,
                                           0.25f, result));
    assert(result[3] == -1.0f);
    float scaled[12] = {};
    for (uint32_t index = 0u; index < 12u; ++index)
    {
        scaled[index] = newTransform[index];
    }
    scaled[0] = 2.0f;
    assert(!InterpolatePathTracingTransform(oldTransform, scaled,
                                            0.25f, result));
    std::cout << "coc_pixels=" << actual
              << " sampled_coc_pixels=" << sampledCoc
              << " shutter_first=" << first.ShutterTime
              << " translation_only=true\n";
    return 0;
}
