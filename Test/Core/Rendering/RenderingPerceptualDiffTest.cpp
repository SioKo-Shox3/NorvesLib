#include "RenderingValidation/RenderingPerceptualDiff.h"

#include <cmath>
#include <cstdlib>
#include <cstring>
#include <iomanip>
#include <iostream>

namespace
{
    using namespace NorvesLib;
    using namespace NorvesLib::Test::RenderingValidation;

    enum class CommandMode
    {
        UnitTests,
        MeasureArtificial,
        CompareRebaselineReview,
        ValidateArtificialBaselines
    };

    struct CommandLine
    {
        CommandMode Mode = CommandMode::UnitTests;
        SceneKind Scene = SceneKind::Indoor;
        bool bHasScene = false;
        uint32_t PatchSize = 0;
        bool bHasPatchSize = false;
        uint8_t ChannelDelta = 0;
        bool bHasChannelDelta = false;
    };

    bool Require(bool condition, const char* message)
    {
        if (!condition)
        {
            std::cerr << "RenderingPerceptualDiffTest failed: " << message << std::endl;
            return false;
        }
        return true;
    }

    const char* SceneName(SceneKind scene)
    {
        return scene == SceneKind::Indoor ? "indoor" : "outdoor";
    }

    const TCHAR* BaselineFileName(SceneKind scene)
    {
        return scene == SceneKind::Indoor ? TEXT("Indoor.png") : TEXT("Outdoor.png");
    }

    bool IsAllowedPatchSize(int value)
    {
        return value == 1 || value == 2 || value == 4 || value == 8 || value == 16;
    }

    bool IsAllowedChannelDelta(int value)
    {
        return value == 1 || value == 2 || value == 4 || value == 8;
    }

    bool ParseCommandLine(int argc, char** argv, CommandLine& out)
    {
        if (argc == 1)
        {
            return true;
        }
        for (int index = 1; index < argc; ++index)
        {
            const char* argument = argv[index];
            if (std::strcmp(argument, "--measure-artificial") == 0)
            {
                if (out.Mode != CommandMode::UnitTests)
                {
                    return false;
                }
                out.Mode = CommandMode::MeasureArtificial;
            }
            else if (std::strcmp(argument, "--compare-rebaseline-review") == 0)
            {
                if (out.Mode != CommandMode::UnitTests)
                {
                    return false;
                }
                out.Mode = CommandMode::CompareRebaselineReview;
            }
            else if (std::strcmp(argument, "--validate-artificial-baselines") == 0)
            {
                if (out.Mode != CommandMode::UnitTests)
                {
                    return false;
                }
                out.Mode = CommandMode::ValidateArtificialBaselines;
            }
            else if (std::strncmp(argument, "--scene=", 8) == 0)
            {
                if (out.bHasScene)
                {
                    return false;
                }
                const char* value = argument + 8;
                if (std::strcmp(value, "indoor") == 0)
                {
                    out.Scene = SceneKind::Indoor;
                }
                else if (std::strcmp(value, "outdoor") == 0)
                {
                    out.Scene = SceneKind::Outdoor;
                }
                else
                {
                    return false;
                }
                out.bHasScene = true;
            }
            else if (std::strncmp(argument, "--patch-size=", 13) == 0)
            {
                if (out.bHasPatchSize)
                {
                    return false;
                }
                const int value = std::atoi(argument + 13);
                if (!IsAllowedPatchSize(value))
                {
                    return false;
                }
                out.PatchSize = static_cast<uint32_t>(value);
                out.bHasPatchSize = true;
            }
            else if (std::strncmp(argument, "--channel-delta=", 16) == 0)
            {
                if (out.bHasChannelDelta)
                {
                    return false;
                }
                const int value = std::atoi(argument + 16);
                if (!IsAllowedChannelDelta(value))
                {
                    return false;
                }
                out.ChannelDelta = static_cast<uint8_t>(value);
                out.bHasChannelDelta = true;
            }
            else
            {
                return false;
            }
        }
        if (out.Mode == CommandMode::ValidateArtificialBaselines)
        {
            return !out.bHasScene && !out.bHasPatchSize && !out.bHasChannelDelta;
        }
        if (!out.bHasScene || out.Mode == CommandMode::UnitTests)
        {
            return false;
        }
        if (out.Mode == CommandMode::MeasureArtificial)
        {
            return out.bHasPatchSize && out.bHasChannelDelta;
        }
        return !out.bHasPatchSize && !out.bHasChannelDelta;
    }

    Core::Container::String BaselinePath(SceneKind scene)
    {
        Core::Container::String path(NORVES_SOURCE_ROOT);
        path += TEXT("/Test/Core/Rendering/Baselines/RenderingValidation/");
        path += BaselineFileName(scene);
        return path;
    }

    Core::Container::String RebaselineBeforePath(SceneKind scene)
    {
        Core::Container::String path(NORVES_BINARY_ROOT);
        path += TEXT("/RenderingValidation/RebaselineReview/");
        path += scene == SceneKind::Indoor ? TEXT("Indoor.before.png") : TEXT("Outdoor.before.png");
        return path;
    }

    Core::Container::String ThresholdPath()
    {
        Core::Container::String path(NORVES_SOURCE_ROOT);
        path += TEXT("/Test/Core/Rendering/Baselines/RenderingValidation/VisualThresholds.tsv");
        return path;
    }

    bool LoadImage(const Core::Container::String& path, Rgba8Image& outImage)
    {
        return Require(LoadPng(path, outImage) == GoldenImageStatus::Success,
                       "fixed PNG input must load");
    }

    bool PrintMetrics(const char* prefix,
                      SceneKind scene,
                      const PerceptualDifferenceMetrics& metrics)
    {
        if (!Require(std::isfinite(metrics.MeanFlipError) && std::isfinite(metrics.MaxFlipError),
                     "reported FLIP metrics must be finite"))
        {
            return false;
        }
        std::cout << std::fixed << std::setprecision(9) << prefix
                  << " scene=" << SceneName(scene)
                  << " mean_flip=" << metrics.MeanFlipError
                  << " max_flip=" << metrics.MaxFlipError
                  << " raw_max=" << static_cast<unsigned int>(metrics.Raw.MaxChannelDelta)
                  << std::endl;
        return true;
    }

    bool MeasureArtificial(const CommandLine& command)
    {
        Rgba8Image reference;
        if (!LoadImage(BaselinePath(command.Scene), reference))
        {
            return false;
        }
        Rgba8Image candidate = reference;
        ApplyArtificialDifference(candidate, {command.PatchSize, command.ChannelDelta});
        PerceptualDifferenceMetrics metrics;
        if (!Require(CompareLdrFlip(reference, candidate, metrics) == PerceptualDiffStatus::Success,
                     "artificial comparison must succeed") ||
            !Require(metrics.Raw.MaxChannelDelta == command.ChannelDelta,
                     "artificial raw max must equal requested delta"))
        {
            return false;
        }
        std::cout << std::fixed << std::setprecision(9)
                  << "NORVESLIB_ARTIFICIAL_METRICS scene=" << SceneName(command.Scene)
                  << " patch_size=" << command.PatchSize
                  << " channel_delta=" << static_cast<unsigned int>(command.ChannelDelta)
                  << " mean_flip=" << metrics.MeanFlipError
                  << " max_flip=" << metrics.MaxFlipError
                  << " raw_max=" << static_cast<unsigned int>(metrics.Raw.MaxChannelDelta)
                  << std::endl;
        return true;
    }

    bool CompareRebaselineReview(const CommandLine& command)
    {
        Rgba8Image reference;
        Rgba8Image candidate;
        PerceptualDifferenceMetrics metrics;
        return LoadImage(RebaselineBeforePath(command.Scene), reference) &&
               LoadImage(BaselinePath(command.Scene), candidate) &&
               Require(CompareLdrFlip(reference, candidate, metrics) == PerceptualDiffStatus::Success,
                       "rebaseline review comparison must succeed") &&
               PrintMetrics("NORVESLIB_REBASELINE_DIFF", command.Scene, metrics);
    }

    bool ValidateArtificialBaseline(SceneKind scene)
    {
        VisualGoldenThresholds thresholds;
        ArtificialDifferenceSpec difference;
        if (!Require(LoadVisualGoldenThresholds(ThresholdPath(), scene, thresholds, difference),
                     "approved visual threshold row must load"))
        {
            return false;
        }
        Rgba8Image reference;
        if (!LoadImage(BaselinePath(scene), reference))
        {
            return false;
        }
        Rgba8Image altered = reference;
        ApplyArtificialDifference(altered, difference);
        PerceptualDifferenceMetrics metrics;
        if (!Require(CompareLdrFlip(reference, altered, metrics) == PerceptualDiffStatus::Success,
                     "approved artificial comparison must succeed") ||
            !Require(metrics.Raw.MaxChannelDelta <= RenderingValidationHardMaxChannelDelta,
                     "approved artificial difference must remain within raw hard max") ||
            !Require(metrics.MeanFlipError > thresholds.MaximumMeanFlipError,
                     "approved artificial difference must exceed mean FLIP threshold") ||
            !Require(!MeetsVisualGoldenThresholds(metrics, thresholds),
                     "combined evaluator must reject approved artificial difference"))
        {
            return false;
        }
        std::cout << std::fixed << std::setprecision(9)
                  << "artificial_baseline scene=" << SceneName(scene)
                  << " mean_flip=" << metrics.MeanFlipError
                  << " limit=" << thresholds.MaximumMeanFlipError
                  << " raw_max=" << static_cast<unsigned int>(metrics.Raw.MaxChannelDelta)
                  << std::endl;
        return true;
    }

    bool ValidateArtificialBaselines()
    {
        return ValidateArtificialBaseline(SceneKind::Indoor) &&
               ValidateArtificialBaseline(SceneKind::Outdoor);
    }

    Rgba8Image MakeReferenceImage()
    {
        constexpr uint32_t Width = 32u;
        constexpr uint32_t Height = 32u;
        constexpr uint32_t ChannelCount = 4u;
        Rgba8Image image;
        image.Width = Width;
        image.Height = Height;
        image.RowPitchBytes = Width * ChannelCount;
        image.Pixels.resize(static_cast<size_t>(image.RowPitchBytes) * Height);
        for (uint32_t y = 0; y < Height; ++y)
        {
            for (uint32_t x = 0; x < Width; ++x)
            {
                const size_t offset = static_cast<size_t>(y) * image.RowPitchBytes +
                                      static_cast<size_t>(x) * ChannelCount;
                image.Pixels[offset + 0u] = 48u;
                image.Pixels[offset + 1u] = 96u;
                image.Pixels[offset + 2u] = 160u;
                image.Pixels[offset + 3u] = 37u;
            }
        }
        return image;
    }

    bool TestSrgbTransferContract()
    {
        return Require(std::fabs(DecodeSrgbToLinear(0.0f) - 0.0f) <= 1.0e-7f,
                       "sRGB zero must decode to linear zero") &&
               Require(std::fabs(DecodeSrgbToLinear(0.04045f) - 0.00313080495f) <= 1.0e-7f,
                       "sRGB branch boundary must match IEC EOTF") &&
               Require(std::fabs(DecodeSrgbToLinear(0.5f) - 0.21404114f) <= 1.0e-7f,
                       "sRGB midpoint must decode to the hand-derived linear value") &&
               Require(std::fabs(DecodeSrgbToLinear(1.0f) - 1.0f) <= 1.0e-7f,
                       "sRGB one must decode to linear one");
    }

    bool TestSameImageAndArtificialDifference()
    {
        const Rgba8Image reference = MakeReferenceImage();
        PerceptualDifferenceMetrics same;
        if (!Require(CompareLdrFlip(reference, reference, same) == PerceptualDiffStatus::Success,
                     "same image comparison must succeed") ||
            !Require(same.MeanFlipError == 0.0, "same image mean FLIP must be zero") ||
            !Require(same.MaxFlipError == 0.0f, "same image max FLIP must be zero") ||
            !Require(same.Raw.MaxChannelDelta == 0u, "same image raw max must be zero"))
        {
            return false;
        }

        Rgba8Image altered = reference;
        const ArtificialDifferenceSpec difference{16u, 8u};
        ApplyArtificialDifference(altered, difference);
        constexpr uint32_t PatchBegin = 8u;
        constexpr uint32_t PatchEnd = 24u;
        for (uint32_t y = 0; y < altered.Height; ++y)
        {
            for (uint32_t x = 0; x < altered.Width; ++x)
            {
                const size_t offset = static_cast<size_t>(y) * altered.RowPitchBytes +
                                      static_cast<size_t>(x) * 4u;
                const bool bInsidePatch = x >= PatchBegin && x < PatchEnd &&
                                          y >= PatchBegin && y < PatchEnd;
                if (!Require(altered.Pixels[offset + 3u] == reference.Pixels[offset + 3u],
                             "artificial difference must preserve alpha") ||
                    !Require(altered.Pixels[offset] == static_cast<uint8_t>(
                                 reference.Pixels[offset] + (bInsidePatch ? difference.ChannelDelta : 0u)),
                             "artificial difference must change only the central RGB patch"))
                {
                    return false;
                }
            }
        }

        PerceptualDifferenceMetrics changed;
        if (!Require(CompareLdrFlip(reference, altered, changed) == PerceptualDiffStatus::Success,
                     "artificial difference comparison must succeed") ||
            !Require(changed.MeanFlipError > same.MeanFlipError,
                     "artificial difference must increase mean FLIP") ||
            !Require(changed.MaxFlipError > 0.0f, "artificial difference must produce positive max FLIP") ||
            !Require(changed.Raw.MaxChannelDelta == 8u, "artificial raw max must equal delta") ||
            !Require(changed.MaxFlipX >= PatchBegin && changed.MaxFlipX < PatchEnd &&
                         changed.MaxFlipY >= PatchBegin && changed.MaxFlipY < PatchEnd,
                     "max FLIP coordinate must lie inside central patch") ||
            !Require(!MeetsVisualGoldenThresholds(changed, {0.0, 8u}),
                     "pooled FLIP gate must reject artificial difference"))
        {
            return false;
        }
        std::cout << "same_mean=" << same.MeanFlipError << std::endl;
        std::cout << "artificial_difference=rejected max_coordinate=("
                  << changed.MaxFlipX << ',' << changed.MaxFlipY << ')' << std::endl;
        return true;
    }
}

int main(int argc, char** argv)
{
    CommandLine command;
    if (!ParseCommandLine(argc, argv, command))
    {
        std::cerr << "RenderingPerceptualDiffTest failed: invalid command line" << std::endl;
        return 1;
    }
    if (command.Mode == CommandMode::MeasureArtificial)
    {
        return MeasureArtificial(command) ? 0 : 1;
    }
    if (command.Mode == CommandMode::CompareRebaselineReview)
    {
        return CompareRebaselineReview(command) ? 0 : 1;
    }
    if (command.Mode == CommandMode::ValidateArtificialBaselines)
    {
        return ValidateArtificialBaselines() ? 0 : 1;
    }
    if (!TestSrgbTransferContract() || !TestSameImageAndArtificialDifference())
    {
        return 1;
    }
    std::cout << "RenderingPerceptualDiffTest passed" << std::endl;
    return 0;
}
