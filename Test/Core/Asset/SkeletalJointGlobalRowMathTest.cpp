// targetの行ベクトルFKをliteral、親順、scratch契約で反証する。
#include "Animation/SkeletalJointGlobalRowMath.h"
#include <bit>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <limits>
#include <initializer_list>
#define CHECK(value)                                                                                                   \
    do                                                                                                                 \
    {                                                                                                                  \
        if (!(value))                                                                                                  \
        {                                                                                                              \
            std::fprintf(stderr, "Joint FK check failed: %s:%d %s\n", __FILE__, __LINE__, #value);                     \
            std::abort();                                                                                              \
        }                                                                                                              \
    } while (false)
namespace M = NorvesLib::Math;
namespace C = NorvesLib::Core::Container;
namespace D = NorvesLib::Core::Animation::Detail;
namespace
{
    void Near(const M::Matrix4x4& actual, const M::Matrix4x4& expected)
    {
        for (size_t i = 0; i < 16; ++i)
        {
            CHECK(std::isfinite(actual.values[i]));
            CHECK(std::abs(actual.values[i] - expected.values[i]) <= 1e-5f);
        }
    }
    void Exact(const M::Matrix4x4& actual, const M::Matrix4x4& expected)
    {
        for (size_t i = 0; i < 16; ++i)
        {
            CHECK(std::bit_cast<uint32_t>(actual.values[i]) == std::bit_cast<uint32_t>(expected.values[i]));
        }
    }
    template <size_t N>
    bool Evaluate(uint32_t index, const int32_t (&parents)[N], const M::Matrix4x4 (&local)[N],
                  M::Matrix4x4 (&global)[N], uint8_t (&state)[N])
    {
        const auto getter = [&parents](uint32_t i) noexcept -> int32_t
        {
            return parents[i];
        };
        return D::BuildJointGlobalRow(index, getter, {local, N}, {global, N}, {state, N});
    }
    M::Matrix4x4 Translation(float x, float y, float z)
    {
        auto value = M::Matrix4x4::Identity;
        value.m30 = x;
        value.m31 = y;
        value.m32 = z;
        return value;
    }
    void TestRootAndShapes()
    {
        int32_t parents[1] = {-1};
        M::Matrix4x4 local[1] = {Translation(3, 4, 5)};
        local[0].m01 = -0.0f;
        local[0].m20 = -0.0f;
        M::Matrix4x4 global[1] = {M::Matrix4x4::Zero};
        uint8_t state[1]{};
        CHECK(Evaluate(0, parents, local, global, state));
        Exact(global[0], local[0]);
        CHECK(state[0] == 2);
        uint32_t calls = 0;
        const auto getter = [&calls](uint32_t) noexcept -> int32_t
        {
            ++calls;
            return -1;
        };
        CHECK(!D::BuildJointGlobalRow(0, getter, {}, {}, {}));
        CHECK(!D::BuildJointGlobalRow(1, getter, {local, 1}, {global, 1}, {state, 1}));
        CHECK(!D::BuildJointGlobalRow(0, getter, {local, 1}, {global, 0}, {state, 1}));
        CHECK(!D::BuildJointGlobalRow(0, getter, {local, 1}, {global, 1}, {state, 0}));
        CHECK(!D::BuildJointGlobalRow(0, getter, {nullptr, 1}, {global, 1}, {state, 1}));
        CHECK(!D::BuildJointGlobalRow(0, getter, {local, 1}, {nullptr, 1}, {state, 1}));
        CHECK(!D::BuildJointGlobalRow(0, getter, {local, 1}, {global, 1}, {nullptr, 1}));
        state[0] = 3;
        CHECK(!D::BuildJointGlobalRow(0, getter, {local, 1}, {global, 1}, {state, 1}));
        CHECK(calls == 0);
    }
    void TestBranchesAndOrder()
    {
        const int32_t parents[4] = {2, 2, -1, -1};
        M::Matrix4x4 local[4] = {Translation(2, 0, 0), Translation(0, 3, 0),
                                 M::Matrix4x4(0, 1, 0, 0, -1, 0, 0, 0, 0, 0, 1, 0, 10, 20, 30, 1),
                                 Translation(7, 8, 9)};
        local[0].m00 = 2;
        M::Matrix4x4 global[4];
        uint8_t state[4]{};
        uint32_t calls[4]{};
        const auto getter = [&parents, &calls](uint32_t i) noexcept -> int32_t
        {
            ++calls[i];
            return parents[i];
        };
        for (uint32_t i = 0; i < 4; ++i)
        {
            CHECK(D::BuildJointGlobalRow(i, getter, {local, 4}, {global, 4}, {state, 4}));
        }
        Near(global[0], M::Matrix4x4(0, 2, 0, 0, -1, 0, 0, 0, 0, 0, 1, 0, 10, 22, 30, 1));
        Near(global[1], M::Matrix4x4(0, 1, 0, 0, -1, 0, 0, 0, 0, 0, 1, 0, 7, 20, 30, 1));
        Exact(global[2], local[2]);
        Exact(global[3], local[3]);
        for (uint32_t i = 0; i < 4; ++i)
        {
            CHECK(calls[i] == 1 && state[i] == 2);
            CHECK(D::BuildJointGlobalRow(i, getter, {local, 4}, {global, 4}, {state, 4}));
            CHECK(calls[i] == 1);
        }
        const int32_t reorderedParents[4] = {-1, 0, 0, -1};
        const uint32_t oldIndex[4] = {2, 0, 1, 3};
        M::Matrix4x4 reorderedLocal[4] = {local[2], local[0], local[1], local[3]};
        M::Matrix4x4 reorderedGlobal[4];
        uint8_t reorderedState[4]{};
        for (uint32_t i = 0; i < 4; ++i)
        {
            CHECK(Evaluate(i, reorderedParents, reorderedLocal, reorderedGlobal, reorderedState));
            Exact(reorderedGlobal[i], global[oldIndex[i]]);
        }
        const int32_t chain[3] = {1, 2, -1};
        M::Matrix4x4 chainLocal[3] = {Translation(1, 0, 0), Translation(0, 2, 0),
                                      M::Matrix4x4(0, 1, 0, 0, -1, 0, 0, 0, 0, 0, 1, 0, 3, 4, 5, 1)};
        M::Matrix4x4 chainGlobal[3];
        uint8_t chainState[3]{};
        CHECK(Evaluate(0, chain, chainLocal, chainGlobal, chainState));
        Near(chainGlobal[0], M::Matrix4x4(0, 1, 0, 0, -1, 0, 0, 0, 0, 0, 1, 0, 1, 5, 5, 1));
    }
    void TestFailuresAndRestart()
    {
        for (int32_t parent : {0, 1, -2})
        {
            int32_t parents[1] = {parent};
            M::Matrix4x4 local[1] = {M::Matrix4x4::Identity};
            M::Matrix4x4 global[1];
            uint8_t state[1]{};
            CHECK(!Evaluate(0, parents, local, global, state));
            CHECK(state[0] == 1);
            parents[0] = -1;
            state[0] = 0;
            CHECK(Evaluate(0, parents, local, global, state));
        }
        int32_t parents[2] = {1, 0};
        M::Matrix4x4 local[2] = {M::Matrix4x4::Identity, M::Matrix4x4::Identity};
        M::Matrix4x4 global[2];
        uint8_t state[2]{};
        CHECK(!Evaluate(0, parents, local, global, state));
        CHECK(state[0] == 1 && state[1] == 1);
        parents[0] = -1;
        state[0] = state[1] = 0;
        CHECK(Evaluate(1, parents, local, global, state));
        for (float bad : {std::numeric_limits<float>::quiet_NaN(), std::numeric_limits<float>::infinity()})
        {
            state[0] = state[1] = 0;
            local[0].m00 = bad;
            CHECK(!Evaluate(0, parents, local, global, state));
            CHECK(state[0] == 1);
            Exact(global[0], local[0]);
        }
        state[0] = state[1] = 0;
        local[0].m00 = std::numeric_limits<float>::max();
        local[1].m00 = 2;
        CHECK(!Evaluate(1, parents, local, global, state));
        CHECK(state[0] == 2 && state[1] == 1);
        CHECK(global[0].m00 == std::numeric_limits<float>::max() && std::isinf(global[1].m00));
        state[0] = state[1] = 0;
        local[0].m00 = 1;
        CHECK(Evaluate(1, parents, local, global, state));
        CHECK(global[1].m00 == 2 && state[0] == 2 && state[1] == 2);
    }
    void TestFiniteNonRigidInputs()
    {
        const int32_t parents[3] = {-1, 0, 1};
        M::Matrix4x4 local[3] = {M::Matrix4x4::Identity, M::Matrix4x4::Identity, M::Matrix4x4::Identity};
        local[0].m00 = 0;
        local[1].m01 = 0.25f;
        local[2].m11 = -1;
        M::Matrix4x4 global[3];
        uint8_t state[3]{};
        CHECK(Evaluate(2, parents, local, global, state));
        for (size_t i = 0; i < 3; ++i)
        {
            CHECK(D::IsFiniteMatrix(global[i]) && state[i] == 2);
        }
        CHECK(global[2].m00 == 0);
    }
} // namespace
int main()
{
    TestRootAndShapes();
    TestBranchesAndOrder();
    TestFailuresAndRestart();
    TestFiniteNonRigidInputs();
    std::puts(
        "SKELETAL_JOINT_GLOBAL_ROW_MATH result=pass row_fk_parent_order_literal_cache_cycle_finite_partial_scratch_no_allocation");
    return 0;
}
