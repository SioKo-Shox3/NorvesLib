// 列ベクトルの解析値で回転順と位置規約を反証する。既存Samplerやretargetは通さない。
#include "Resource/BvhEvaluate.h"
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <limits>
#include <utility>
#define CHECK(value)                                                                                                   \
    do                                                                                                                 \
    {                                                                                                                  \
        if (!(value))                                                                                                  \
        {                                                                                                              \
            std::fprintf(stderr, "BVH FK line %d: %s\n", __LINE__, #value);                                            \
            std::abort();                                                                                              \
        }                                                                                                              \
    } while (false)
namespace
{
    namespace B = NorvesLib::Core::Bvh;
    namespace C = NorvesLib::Core::Container;
    using Convention = B::TranslationConvention;
    using Status = B::BvhEvaluateStatus;
    using Buffer = C::VariableArray<uint8_t>;
    bool Near(double value, double expected)
    {
        return std::isfinite(value) && std::fabs(value - expected) <= 1e-9;
    }
    void Vector(const B::Vector3d& actual, double x, double y, double z)
    {
        CHECK(Near(actual.X, x) && Near(actual.Y, y) && Near(actual.Z, z));
    }
    void Matrix(const B::Matrix3d& actual, const double (&expected)[9])
    {
        for (size_t i = 0; i < 9; ++i)
        {
            CHECK(Near(actual.Values[i], expected[i]));
        }
    }
    void Identity(const B::Matrix3d& actual)
    {
        const double identity[] = {1, 0, 0, 0, 1, 0, 0, 0, 1};
        Matrix(actual, identity);
    }
    B::BvhDocument Decode(const char* text)
    {
        B::BvhDocument document;
        CHECK(B::DecodeBvh({reinterpret_cast<const uint8_t*>(text), std::strlen(text)}, {}, document).Succeeded());
        return document;
    }
    B::BvhDocument Source()
    {
        return Decode(
            "HIERARCHY ROOT R { OFFSET 10 20 30 CHANNELS 6 Xposition Yposition Zposition Zrotation Yrotation Xrotation\n"
            "JOINT C { OFFSET 2 0 0 CHANNELS 6 Xposition Yposition Zposition Xrotation Yrotation Zrotation\n"
            "End Site { OFFSET 0 1 0 } }\nJOINT S { OFFSET 0 0 5 CHANNELS 0 } }\n"
            "MOTION\nFrames: 2\nFrame Time: .25\n"
            "1 2 3 90 0 0 0 3 0 90 0 0\n4 5 6 0 0 0 0 3 0 0 0 0");
    }
    B::BvhPose Evaluate(const B::BvhDocument& document, uint32_t frame, Convention convention)
    {
        B::BvhPose pose;
        const auto result = B::EvaluateBvhFrame(document, frame, convention, pose);
        if (!result.Succeeded())
        {
            std::fprintf(stderr, "FK status=%u joint=%zu\n", static_cast<unsigned>(result.Status), result.JointIndex);
        }
        CHECK(result.Succeeded() && pose.FrameIndex == frame && pose.Convention == convention);
        return pose;
    }
    void Reject(const B::BvhDocument& document, Status status, uint32_t frame = 0,
                Convention convention = Convention::OffsetPlusChannels)
    {
        auto held = Evaluate(Source(), 1, Convention::AbsoluteLocalChannels);
        const auto outcome = B::EvaluateBvhFrame(document, frame, convention, held);
        CHECK(outcome.Status == status && !outcome.Succeeded());
        CHECK(held.FrameIndex == 1 && held.Convention == Convention::AbsoluteLocalChannels && held.Joints.size() == 3);
        Vector(held.Joints[0].World.Translation, 4, 5, 6);
        Vector(held.Joints[1].EndSiteWorld, 4, 9, 6);
    }
    void ExplicitConventions()
    {
        auto source = Source();
        auto added = Evaluate(source, 0, Convention::OffsetPlusChannels);
        auto absolute = Evaluate(source, 0, Convention::AbsoluteLocalChannels);
        CHECK(added.Joints.size() == 3 && absolute.Joints.size() == 3);
        Vector(added.Joints[0].Local.Translation, 11, 22, 33);
        Vector(added.Joints[0].World.Translation, 11, 22, 33);
        Vector(added.Joints[1].Local.Translation, 2, 3, 0);
        Vector(added.Joints[1].World.Translation, 8, 24, 33);
        Vector(added.Joints[1].EndSiteWorld, 8, 24, 34);
        Vector(absolute.Joints[0].Local.Translation, 1, 2, 3);
        Vector(absolute.Joints[0].World.Translation, 1, 2, 3);
        Vector(absolute.Joints[1].Local.Translation, 0, 3, 0);
        Vector(absolute.Joints[1].World.Translation, -2, 2, 3);
        Vector(absolute.Joints[1].EndSiteWorld, -2, 2, 4);
        Vector(added.Joints[2].World.Translation, 11, 22, 38);
        Vector(absolute.Joints[2].World.Translation, 1, 2, 8);
        CHECK(!added.Joints[0].bHasEndSite && added.Joints[1].bHasEndSite && !added.Joints[2].bHasEndSite);
        Identity(added.Joints[2].Local.Rotation);
        const double rootRotation[] = {0, -1, 0, 1, 0, 0, 0, 0, 1};
        const double childRotation[] = {0, 0, 1, 1, 0, 0, 0, 1, 0};
        Matrix(added.Joints[0].World.Rotation, rootRotation);
        Matrix(added.Joints[1].World.Rotation, childRotation);
        Matrix(absolute.Joints[1].World.Rotation, childRotation);
        const auto next = Evaluate(source, 1, Convention::OffsetPlusChannels);
        Vector(next.Joints[0].World.Translation, 14, 25, 36);
        Vector(next.Joints[1].World.Translation, 16, 28, 36);
        Vector(next.Joints[1].EndSiteWorld, 16, 29, 36);
        Identity(next.Joints[1].World.Rotation);
        CHECK(source.Values[0] == 1 && source.Joints[1].Offset.X == 2);
        source = {};
        const auto copied = added;
        added = {};
        Vector(copied.Joints[1].EndSiteWorld, 8, 24, 34);
        auto moved = std::move(absolute);
        Vector(moved.Joints[1].EndSiteWorld, -2, 2, 4);
    }
    B::BvhDocument Rotations()
    {
        return Decode(
            "HIERARCHY ROOT R { OFFSET 2 3 4 CHANNELS 3 Xrotation Yrotation Zrotation }\nMOTION\nFrames: 1\nFrame Time: 1\n90 90 90");
    }
    void RotationOrders()
    {
        const unsigned orders[][3] = {{3, 4, 5}, {3, 5, 4}, {4, 3, 5}, {4, 5, 3}, {5, 3, 4}, {5, 4, 3}};
        // それぞれXYZ/XZY/YXZ/YZX/ZXY/ZYX。正の90度の軸回転を手計算した整数行列。
        const double expected[][9] = {{0, 0, 1, 0, -1, 0, 1, 0, 0}, {0, -1, 0, 1, 0, 0, 0, 0, 1},
                                      {1, 0, 0, 0, 0, -1, 0, 1, 0}, {0, 1, 0, 1, 0, 0, 0, 0, -1},
                                      {-1, 0, 0, 0, 0, 1, 0, 1, 0}, {0, 0, 1, 0, 1, 0, -1, 0, 0}};
        for (size_t order = 0; order < 6; ++order)
        {
            auto source = Rotations();
            for (size_t i = 0; i < 3; ++i)
            {
                source.Channels[i] = static_cast<B::Channel>(orders[order][i]);
            }
            const auto added = Evaluate(source, 0, Convention::OffsetPlusChannels);
            const auto absolute = Evaluate(source, 0, Convention::AbsoluteLocalChannels);
            Matrix(added.Joints[0].Local.Rotation, expected[order]);
            Matrix(absolute.Joints[0].World.Rotation, expected[order]);
            Vector(absolute.Joints[0].World.Translation, 2, 3, 4);
        }
        const double axes[][9] = {
            {1, 0, 0, 0, 0, -1, 0, 1, 0}, {0, 0, 1, 0, 1, 0, -1, 0, 0}, {0, -1, 0, 1, 0, 0, 0, 0, 1}};
        for (size_t axis = 0; axis < 3; ++axis)
        {
            auto source = Rotations();
            source.Values = {0, 0, 0};
            source.Values[axis] = 90;
            const auto pose = Evaluate(source, 0, Convention::OffsetPlusChannels);
            Matrix(pose.Joints[0].Local.Rotation, axes[axis]);
            source.Values[axis] = 450;
            Matrix(Evaluate(source, 0, Convention::OffsetPlusChannels).Joints[0].Local.Rotation, axes[axis]);
            source.Values[axis] = -90;
            const auto negative = Evaluate(source, 0, Convention::OffsetPlusChannels);
            for (size_t row = 0; row < 3; ++row)
            {
                for (size_t column = 0; column < 3; ++column)
                {
                    CHECK(
                        Near(negative.Joints[0].Local.Rotation.Values[row * 3 + column], axes[axis][column * 3 + row]));
                }
            }
        }
        auto source = Rotations();
        source.Values = {0, 0, 45};
        const double diagonal[] = {
            .7071067811865475244, -.7071067811865475244, 0, .7071067811865475244, .7071067811865475244, 0, 0, 0, 1};
        Matrix(Evaluate(source, 0, Convention::AbsoluteLocalChannels).Joints[0].World.Rotation, diagonal);
        source.Values = {std::numeric_limits<double>::max(), 0, 0};
        const auto large = Evaluate(source, 0, Convention::OffsetPlusChannels);
        for (size_t row = 0; row < 3; ++row)
        {
            for (size_t other = 0; other < 3; ++other)
            {
                double dot = 0;
                for (size_t column = 0; column < 3; ++column)
                {
                    dot += large.Joints[0].Local.Rotation.Values[row * 3 + column] *
                           large.Joints[0].Local.Rotation.Values[other * 3 + column];
                }
                CHECK(Near(dot, row == other ? 1 : 0));
            }
        }
    }
    void RejectsAndFiniteBoundaries()
    {
        Reject(Source(), Status::InvalidConvention, 0, Convention{});
        Reject(Source(), Status::InvalidConvention, 0, static_cast<Convention>(255));
        Reject(Source(), Status::FrameOutOfRange, 2);
        Reject({}, Status::InvalidDocument);
        auto bad = Source();
        bad.FrameCount = 0;
        Reject(bad, Status::InvalidDocument);
        bad = Source();
        bad.FrameTimeSeconds = 0;
        Reject(bad, Status::InvalidDocument);
        bad = Source();
        bad.Values.pop_back();
        Reject(bad, Status::InvalidDocument);
        bad = Source();
        bad.Joints[0].Parent = 0;
        Reject(bad, Status::InvalidDocument);
        bad = Source();
        bad.Joints[1].Parent = 1;
        Reject(bad, Status::InvalidDocument);
        bad = Source();
        bad.Joints[1].Parent = 2;
        Reject(bad, Status::InvalidDocument);
        bad = Source();
        bad.Joints[1].ChannelOffset = 5;
        Reject(bad, Status::InvalidDocument);
        bad = Source();
        bad.Joints[1].ChannelOffset = 7;
        Reject(bad, Status::InvalidDocument);
        bad = Source();
        bad.Joints[1].ChannelCount = 7;
        Reject(bad, Status::InvalidDocument);
        bad = Source();
        bad.Channels[0] = static_cast<B::Channel>(255);
        Reject(bad, Status::InvalidDocument);
        bad = Source();
        bad.Channels[1] = bad.Channels[0];
        Reject(bad, Status::InvalidDocument);
        const double nan = std::numeric_limits<double>::quiet_NaN();
        bad = Source();
        bad.FrameTimeSeconds = nan;
        Reject(bad, Status::InvalidDocument);
        bad = Source();
        bad.FrameTimeSeconds = std::numeric_limits<double>::infinity();
        Reject(bad, Status::InvalidDocument);
        bad = Source();
        bad.FrameTimeSeconds = -1;
        Reject(bad, Status::InvalidDocument);
        bad = Source();
        bad.FrameCount = 3;
        bad.Values.resize(36);
        bad.FrameTimeSeconds = std::numeric_limits<double>::max();
        Reject(bad, Status::InvalidDocument);
        auto unnamed = Source();
        for (auto& joint : unnamed.Joints)
        {
            joint.Name.clear();
        }
        Vector(Evaluate(unnamed, 0, Convention::OffsetPlusChannels).Joints[1].World.Translation, 8, 24, 33);
        bad = Source();
        bad.Joints[0].Offset.X = nan;
        Reject(bad, Status::InvalidDocument);
        bad = Source();
        bad.Joints[1].EndSiteOffset.Y = nan;
        Reject(bad, Status::InvalidDocument);
        bad = Source();
        bad.Values[0] = nan;
        Reject(bad, Status::InvalidDocument);
        bad = Source();
        bad.Values[23] = nan;
        Vector(Evaluate(bad, 0, Convention::OffsetPlusChannels).Joints[0].World.Translation, 11, 22, 33);
        Reject(bad, Status::InvalidDocument, 1);
        // raw解析が成功しても評価profileには含めない組合せを黙って落とさない。
        Reject(Decode("HIERARCHY ROOT R { OFFSET 0 0 0 CHANNELS 1 Xrotation }\nMOTION\nFrames: 1\nFrame Time: 1\n1"),
               Status::UnsupportedChannels);
        Reject(
            Decode(
                "HIERARCHY ROOT R { OFFSET 0 0 0 CHANNELS 2 Xposition Yposition }\nMOTION\nFrames: 1\nFrame Time: 1\n1 2"),
            Status::UnsupportedChannels);
        Reject(
            Decode(
                "HIERARCHY ROOT R { OFFSET 0 0 0 CHANNELS 6 Xposition Xrotation Yposition Yrotation Zposition Zrotation }\nMOTION\nFrames: 1\nFrame Time: 1\n1 2 3 4 5 6"),
            Status::UnsupportedChannels);
        Reject(
            Decode(
                "HIERARCHY ROOT R { OFFSET 0 0 0 CHANNELS 6 Xrotation Yrotation Zrotation Xposition Yposition Zposition }\nMOTION\nFrames: 1\nFrame Time: 1\n1 2 3 4 5 6"),
            Status::UnsupportedChannels);
        const double maximum = std::numeric_limits<double>::max();
        auto position = Decode(
            "HIERARCHY ROOT R { OFFSET 0 0 0 CHANNELS 3 Zposition Xposition Yposition }\nMOTION\nFrames: 1\nFrame Time: 1\n3 1 2");
        Vector(Evaluate(position, 0, Convention::AbsoluteLocalChannels).Joints[0].World.Translation, 1, 2, 3);
        position.Joints[0].Offset.X = maximum;
        position.Values[1] = maximum;
        Reject(position, Status::NonFiniteResult);
        CHECK(Evaluate(position, 0, Convention::AbsoluteLocalChannels).Joints[0].World.Translation.X == maximum);
        auto world = Rotations();
        world.Values = {0, 0, 0};
        world.Joints[0].Offset = {maximum, 0, 0};
        B::Joint child;
        child.Parent = 0;
        child.ChannelOffset = 3;
        child.Offset = {maximum, 0, 0};
        world.Joints.push_back(child);
        Reject(world, Status::NonFiniteResult);
        auto end = Rotations();
        end.Values = {0, 0, 0};
        end.Joints[0].Offset = {maximum, 0, 0};
        end.Joints[0].bHasEndSite = true;
        end.Joints[0].EndSiteOffset = {maximum, 0, 0};
        Reject(end, Status::NonFiniteResult);
    }
} // namespace
int main()
{
    ExplicitConventions();
    RotationOrders();
    RejectsAndFiniteBoundaries();
    std::puts(
        "BVH_EVALUATE result=pass explicit_translation_conventions_six_rotation_orders_fk_end_sites_profile_atomic_no_retarget");
    return 0;
}
