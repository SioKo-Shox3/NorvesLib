#include "Animation/ClipBankRestComparison.h"
#include <algorithm>
#include <cmath>
namespace NorvesLib::Core::Skeletal::Detail
{
    namespace
    {
        double RotationDistance(const SkeletalValue& a, const SkeletalValue& b)
        {
            double x[4] = {a.X, a.Y, a.Z, a.W}, y[4] = {b.X, b.Y, b.Z, b.W};
            double nx = 0, ny = 0;
            for (size_t i = 0; i < 4; ++i)
            {
                nx += x[i] * x[i];
                ny += y[i] * y[i];
            }
            nx = std::sqrt(nx);
            ny = std::sqrt(ny);
            double dot = 0;
            for (size_t i = 0; i < 4; ++i)
            {
                x[i] /= nx;
                y[i] /= ny;
                dot += x[i] * y[i];
            }
            double minus = 0, plus = 0;
            for (size_t i = 0; i < 4; ++i)
            {
                if (dot < 0)
                {
                    y[i] = -y[i];
                }
                const double d = x[i] - y[i], s = x[i] + y[i];
                minus += d * d;
                plus += s * s;
            }
            return 4 * std::atan2(std::sqrt(minus), std::sqrt(plus));
        }
    } // namespace
    bool CompareClipBankRest(const ClipBankV1Data& bank, Container::Span<const SkeletalRestTransform> targetCanonical,
                             Container::Span<const uint32_t> targetIndices, double targetScale,
                             const RigBindingPolicy& policy, RigV1Report& report)
    {
        if (targetCanonical.size() != bank.Topology.Joints.size() || targetIndices.size() != targetCanonical.size() ||
            !IsValidRigBindingPolicy(policy))
        {
            report.Status = RigV1Status::InvalidInput;
            return false;
        }
        const auto* source = &bank;
        const auto targetHash = RigRestHash({targetCanonical.data(), targetCanonical.size()});
        bool mismatch = false;
        report.Snapshots.reserve(source->Snapshots.size());
        report.Differences.reserve(source->Snapshots.size() * targetCanonical.size());
        for (size_t n = 0; n < source->Snapshots.size(); ++n)
        {
            const auto& snapshot = source->Snapshots[n];
            RigSnapshotComparison comparison;
            comparison.AuthorLabel = snapshot.Label;
            comparison.AuthorRestHash = snapshot.RestHash;
            comparison.TargetRestHash = targetHash;
            comparison.AuthorImportScale = snapshot.ResolvedImportScale;
            comparison.TargetImportScale = targetScale;
            for (size_t i = 0; i < targetCanonical.size(); ++i)
            {
                const auto& a = snapshot.Rest[i];
                const auto& b = targetCanonical[i];
                RigJointDifference difference;
                difference.SnapshotIndex = uint32_t(n);
                difference.CanonicalIndex = uint32_t(i);
                difference.TargetIndex = targetIndices[i];
                difference.Name = source->Topology.Joints[i].Name;
                difference.TranslationMeters =
                    std::hypot(double(b.Translation.X) - a.Translation.X, double(b.Translation.Y) - a.Translation.Y,
                               double(b.Translation.Z) - a.Translation.Z);
                difference.RotationRadians = RotationDistance(a.Rotation, b.Rotation);
                const double av[] = {a.Scale.X, a.Scale.Y, a.Scale.Z}, bv[] = {b.Scale.X, b.Scale.Y, b.Scale.Z};
                for (size_t axis = 0; axis < 3; ++axis)
                {
                    difference.ScaleRatios[axis] = bv[axis] / av[axis];
                    difference.MaximumLogScale =
                        std::max(difference.MaximumLogScale, std::abs(std::log(difference.ScaleRatios[axis])));
                }
                difference.bExceedsTolerance = difference.TranslationMeters > policy.Tolerance.TranslationMeters ||
                                               difference.RotationRadians > policy.Tolerance.RotationRadians ||
                                               difference.MaximumLogScale > policy.Tolerance.LogScale;
                if (difference.bExceedsTolerance)
                {
                    ++comparison.ExceededJoints;
                    mismatch = true;
                }
                comparison.MaximumTranslationMeters =
                    std::max(comparison.MaximumTranslationMeters, difference.TranslationMeters);
                comparison.MaximumRotationRadians =
                    std::max(comparison.MaximumRotationRadians, difference.RotationRadians);
                comparison.MaximumLogScale = std::max(comparison.MaximumLogScale, difference.MaximumLogScale);
                report.Differences.push_back(std::move(difference));
            }
            report.Snapshots.push_back(std::move(comparison));
        }
        report.bComparisonComplete = true;
        if (mismatch && !policy.bAllowRestMismatch)
        {
            report.Status = RigV1Status::RestMismatch;
            return false;
        }
        report.bOverrideUsed = mismatch && policy.bAllowRestMismatch;
        report.Status = RigV1Status::Success;
        return true;
    }
} // namespace NorvesLib::Core::Skeletal::Detail
