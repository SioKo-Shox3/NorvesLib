// version 1 の profile の出力が GR85 の変更で変わらないことをハッシュで固定する。
#include "ClipBankV1Fixture.h"
#include "Tools/AssetCook/RigRetargetCook.h"
#include "Asset/AssetPackageFormat.h"

namespace F = NorvesLib::Tests::RigV1Fixture;
namespace Cook = NorvesLib::Tools::AssetCook;

namespace
{
    bool V1ProfileOutputIsPinned()
    {
        F::Fixture fixture;
        const auto write = [](const std::filesystem::path& path, const F::Text& text)
        {
            F::WriteBytes(path, F::Bytes(text.begin(), text.end()));
        };
        write("RigV1Fixture/target.gltf", fixture.Json);
        const auto motion = F::Replace(
            F::RootTrs(fixture.Json, R"("rotation":[0,0.7071067811865475,0,0.7071067811865475],)"),
            R"(,{"sampler":1,"target":{"node":0,"path":"rotation"}})", "");
        write("RigV1Fixture/motion.gltf", motion);
        write("RigV1Fixture/run.bvh", R"bvh(HIERARCHY
ROOT Root { OFFSET 0 0 0 CHANNELS 6 Xposition Yposition Zposition Zrotation Xrotation Yrotation
 JOINT Child { OFFSET 0 1 0 CHANNELS 6 Xposition Yposition Zposition Zrotation Xrotation Yrotation End Site { OFFSET 0 1 0 } }
}
MOTION
Frames: 3
Frame Time: 0.5
0 0 0 0 0 45 10 0 0 0 0 0
1 .25 0 0 0 60 20 0 0 30 0 0
2 0 0 0 0 75 30 0 0 0 0 0
)bvh");
        write("RigV1Fixture/roles.json",
              R"json({"version":1,"vocabulary":"quadruped_v1","axes":{"up":"+Y","forward":"+Z","handedness":"right"},"units":{"position_scale":1},"position_convention":"additive","time":{"mode":"header_frame_time"},"source_roles":{"root":["Root"],"spine":["Child"]},"target_roles":{"root":[{"joint":"Root"}],"spine":[{"joint":"Child"}]},"rest_pose":{"mode":"match"},"processing":{"loop_mode":"none","output_fps":30}})json");

        struct PinnedOutput
        {
            const char* Input;
            const char* ClipName;
            uint64_t Hash;
            size_t Size;
        };
        constexpr PinnedOutput expected[] = {
            {"RigV1Fixture/run.bvh", "Run", 9676747322439301739ull, 5384},
            {"RigV1Fixture/motion.gltf", "", 2385898557403942170ull, 9464},
        };
        bool bMatches = true;
        for (const auto& pin : expected)
        {
            Cook::SingleAssetCookRequest request;
            request.InputPath = pin.Input;
            request.RetargetSkeletonPath = "RigV1Fixture/target.gltf";
            request.RetargetProfilePath = "RigV1Fixture/roles.json";
            request.RetargetClipName = pin.ClipName;
            request.LogicalPath = "Animations/Run";
            request.Kind = "animation";
            request.Format = "nvskel.v1.clips";
            Cook::CookDependencySnapshot dependencies;
            F::Text error;
            F::Bytes payload;
            const auto source = F::ReadBytes(request.InputPath);
            if (!Cook::CaptureCookDependencySnapshot(request, 1, dependencies, error) ||
                !Cook::Detail::CookRigRetargetBank(request, F::View(source), dependencies, payload, error))
            {
                std::fprintf(stderr, "リターゲットの cook に失敗: %s (%s)\n", pin.Input, error.c_str());
                return false;
            }
            const auto hash = NorvesLib::Core::Asset::ComputeAssetPackagePayloadHash(payload.data(), payload.size());
            if (hash != pin.Hash || payload.size() != pin.Size)
            {
                std::fprintf(stderr,
                             "v1 profile の出力が不一致: %s 期待 hash=%llu bytes=%zu 実際 hash=%llu bytes=%zu\n",
                             pin.Input, static_cast<unsigned long long>(pin.Hash), pin.Size,
                             static_cast<unsigned long long>(hash), payload.size());
                bMatches = false;
            }
        }
        if (bMatches)
        {
            std::puts("RETARGET_COOK_CASE v1_profile_output_pinned result=pass");
        }
        return bMatches;
    }
} // namespace

int main()
{
    return V1ProfileOutputIsPinned() ? 0 : 1;
}
