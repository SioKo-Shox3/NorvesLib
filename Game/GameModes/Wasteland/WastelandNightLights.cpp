#include "GameModes/Wasteland/WastelandNightLights.h"

#include "Core/Public/Component/LightIntensityUnit.h"
#include "Core/Public/Component/MeshComponent.h"
#include "Core/Public/Component/PointLightComponent.h"
#include "Core/Public/GameMode/GameModeContext.h"
#include "Core/Public/GameMode/GameModeScope.h"
#include "Core/Public/Logging/LogMacros.h"
#include "Core/Public/Math/Quaternion.h"
#include "Core/Public/Math/Vector3.h"
#include "Core/Public/Object/Entity.h"
#include "Core/Public/Object/World.h"
#include "Core/Public/Rendering/MaterialTypes.h"
#include "Core/Public/Rendering/ProceduralMeshGenerator.h"
#include "Core/Public/Rendering/RenderResources.h"

#include <cmath>
#include <cstdio>

using namespace NorvesLib::Core;
using namespace NorvesLib::Core::Container;
using namespace NorvesLib::Core::GameMode;
using namespace NorvesLib::Core::Rendering;
namespace Layout = Game::GameModes::WastelandLayout;

namespace Game::GameModes::WastelandNightLights
{
    namespace
    {
        constexpr float kTwoPi = 6.28318530717958647692f;

        // 発光体のメッシュの ID（ほかのシーンの 100〜、200〜、900、910、911、920、荒野の灰色の板 930 と重ならない値）
        constexpr uint32_t kLanternGlassMeshHandleId = 940;
        constexpr uint32_t kFlameMeshHandleId = 941;
        constexpr uint32_t kDoorwayGlowMeshHandleId = 942;

        // 灯りの色（リニアの sRGB）。街灯は古い白熱灯・ガス灯の 2700 K 前後、松明と焚き火は炎の 1800 K 前後、戸口の奥は
        // 油の灯の 2000 K 前後
        constexpr float kLampColor[3] = {1.0f, 0.56f, 0.24f};
        constexpr float kFireColor[3] = {1.0f, 0.36f, 0.07f};
        constexpr float kRoomColor[3] = {1.0f, 0.45f, 0.14f};

        // 点光源の光束[lm]と届く範囲[m]。街灯は 75 W 形の白熱灯ほど、松明は火かごの炎、焚き火は 0.6 m ほどの炎。
        // 範囲は光束の逆 2 乗の照度が月明かり（0.25 lx）の 2 割ほどに落ちる距離まで
        constexpr float kLampLumens = 1000.0f;
        constexpr float kLampRange = 30.0f;
        constexpr float kTorchLumens = 1500.0f;
        constexpr float kTorchRange = 30.0f;
        constexpr float kCampfireLumens = 5000.0f;
        constexpr float kCampfireRange = 40.0f;

        // 発光体の輝度[nits]。ランタンのガラスは光束を覆いの面（約 0.35 m²）で割った完全拡散の値（約 900）。炎は芯の
        // 黄の明るさより低くし、ブルームのにじみを炎の周りに留める。戸口の奥は、数十 lx に照らされた室内の壁
        constexpr float kLanternGlassNits = 800.0f;
        constexpr float kFlameNits = 600.0f;
        constexpr float kRoomNits = 25.0f;
        // 発光体の GBuffer のアルベド（照明を受けた分が発光に足されないよう暗くする。CustomData の 0 は 1 として扱われる）
        constexpr float kEmitterAlbedo = 0.02f;

        // 揺らぎの相対の振れ幅（焚き火は大きく、松明は少し小さく揺らす）。揺らすのは点光源の光束だけで、炎の発光体の
        // 大きさは変えない（物体の変換が毎フレーム変わるとシーンの revision が進み、RTGI の履歴が毎フレーム捨てられる）
        constexpr float kCampfireFlicker = 0.25f;
        constexpr float kTorchFlicker = 0.18f;
        // 揺らぎの光束をログへ書く間隔[秒]
        constexpr float kFlickerLogIntervalSeconds = 4.0f;

        // ---- 形（生成した GLB の頂点から測ったローカルの座標[m]。小物の原点はメッシュの範囲の中心） ----
        // 街灯（street_lamp）: ランタンのガラスは y 1.06〜1.30 の逆さの角錐台（半幅 0.11〜0.16）。その外側を覆う
        constexpr float kLanternBottomY = 1.05f;
        constexpr float kLanternTopY = 1.29f;
        constexpr float kLanternBottomHalf = 0.122f;
        constexpr float kLanternTopHalf = 0.172f;
        // 点光源はランタンの中心
        constexpr float kLampLightY = 1.17f;
        // 松明の柱（torch_post）: 火かごの縁は y 0.84、炭の山の頂は y 1.07。炎は炭の上へ立ち、点光源は炭の山の少し上
        constexpr float kTorchFlameCenterY = 1.1f;
        constexpr float kTorchFlameRadii[3] = {0.07f, 0.17f, 0.07f};
        constexpr float kTorchLightY = 1.2f;
        // 焚き火（campfire_ring）: 石の輪の上端は y −0.04、薪の円錐の頂は y 0.32。炎は薪の中に立ち、点光源は薪の頂の少し上
        constexpr float kCampfireFlameCenterY = 0.16f;
        constexpr float kCampfireFlameRadii[3] = {0.12f, 0.3f, 0.12f};
        constexpr float kCampfireLightY = 0.42f;
        // 日干し煉瓦の家（adobe_ruin）: 表（+Z）の壁は外の面 z 2.3・内の面 z 1.8、戸口は x −0.85〜0.05・y −1.55〜0.4、
        // 室内は床 y −1.55・天井 y 1.0、横の壁の内の面は x −1.9・2.1 前後。戸口の奥の室内の灯りは、表の壁の内の面の
        // すぐ奥に立てた板（戸口からだけ見える）
        constexpr float kDoorwayGlowZ = 1.65f;
        constexpr float kDoorwayGlowMinX = -1.5f;
        constexpr float kDoorwayGlowMaxX = 0.7f;
        constexpr float kDoorwayGlowMinY = -1.5f;
        constexpr float kDoorwayGlowMaxY = 0.95f;

        /// 小物のローカルの点をワールドへ写す（WastelandLayout.cpp の配置と同じ: Y 軸まわりの角 a で、ローカルの (x, z) を
        /// (x cos a − z sin a, z cos a + x sin a) へ写し、拡大率を掛けて位置を足す）
        NorvesLib::Math::Vector3 ToWorld(const Layout::PropPlacement &placement, float x, float y, float z)
        {
            const float c = std::cos(placement.Yaw);
            const float s = std::sin(placement.Yaw);
            return NorvesLib::Math::Vector3(placement.X + (x * c - z * s) * placement.Scale, placement.Y + y * placement.Scale,
                                            placement.Z + (z * c + x * s) * placement.Scale);
        }

        void AppendQuad(VariableArray<Mesh3DVertex> &vertices, VariableArray<uint32_t> &indices,
                        const float (&corners)[4][3], const float (&normal)[3])
        {
            const uint32_t base = static_cast<uint32_t>(vertices.size());
            constexpr float kUV[4][2] = {{0.0f, 0.0f}, {1.0f, 0.0f}, {1.0f, 1.0f}, {0.0f, 1.0f}};
            for (uint32_t corner = 0; corner < 4; ++corner)
            {
                Mesh3DVertex vertex;
                for (uint32_t axis = 0; axis < 3; ++axis)
                {
                    vertex.Position[axis] = corners[corner][axis];
                    vertex.Normal[axis] = normal[axis];
                }
                vertex.TexCoord[0] = kUV[corner][0];
                vertex.TexCoord[1] = kUV[corner][1];
                vertices.push_back(vertex);
            }
            // 角は法線の向きから見て反時計回りに並ぶので、表（時計回り。ProceduralMeshGenerator の平面と同じ）になるよう逆に結ぶ
            const uint32_t order[6] = {0, 2, 1, 0, 3, 2};
            for (uint32_t index : order)
            {
                indices.push_back(base + index);
            }
        }

        /// ランタンのガラスを覆う逆さの角錐台（横の 4 面と底）
        void BuildLanternGlass(VariableArray<Mesh3DVertex> &vertices, VariableArray<uint32_t> &indices)
        {
            const float b = kLanternBottomHalf;
            const float t = kLanternTopHalf;
            const float y0 = kLanternBottomY;
            const float y1 = kLanternTopY;
            // 横の面の外向きの法線（上へ広がる分だけ下を向く）
            const float flare = (t - b) / (y1 - y0);
            const float length = std::sqrt(1.0f + flare * flare);
            const float nh = 1.0f / length;
            const float ny = -flare / length;
            const float sides[4][4][3] = {
                {{-b, y0, b}, {b, y0, b}, {t, y1, t}, {-t, y1, t}},     // +Z
                {{b, y0, -b}, {-b, y0, -b}, {-t, y1, -t}, {t, y1, -t}}, // -Z
                {{b, y0, b}, {b, y0, -b}, {t, y1, -t}, {t, y1, t}},     // +X
                {{-b, y0, -b}, {-b, y0, b}, {-t, y1, t}, {-t, y1, -t}}, // -X
            };
            const float normals[4][3] = {{0.0f, ny, nh}, {0.0f, ny, -nh}, {nh, ny, 0.0f}, {-nh, ny, 0.0f}};
            for (uint32_t side = 0; side < 4; ++side)
            {
                AppendQuad(vertices, indices, sides[side], normals[side]);
            }
            const float bottom[4][3] = {{-b, y0, -b}, {b, y0, -b}, {b, y0, b}, {-b, y0, b}};
            const float down[3] = {0.0f, -1.0f, 0.0f};
            AppendQuad(vertices, indices, bottom, down);
        }

        /// 戸口の奥の室内の灯りの板（表の壁の内の面のすぐ奥に、表 +Z へ向けて立てる）
        void BuildDoorwayGlow(VariableArray<Mesh3DVertex> &vertices, VariableArray<uint32_t> &indices)
        {
            const float corners[4][3] = {{kDoorwayGlowMinX, kDoorwayGlowMinY, kDoorwayGlowZ},
                                         {kDoorwayGlowMaxX, kDoorwayGlowMinY, kDoorwayGlowZ},
                                         {kDoorwayGlowMaxX, kDoorwayGlowMaxY, kDoorwayGlowZ},
                                         {kDoorwayGlowMinX, kDoorwayGlowMaxY, kDoorwayGlowZ}};
            const float front[3] = {0.0f, 0.0f, 1.0f};
            AppendQuad(vertices, indices, corners, front);
        }

        bool RegisterMesh(GameModeContext &ctx, uint32_t id, const VariableArray<Mesh3DVertex> &vertices,
                          const VariableArray<uint32_t> &indices)
        {
            const MeshDataHandle mesh{id};
            if (!ctx.RenderResourcesRef.Meshes().Register(mesh, vertices.data(),
                                                          static_cast<uint32_t>(vertices.size() * sizeof(Mesh3DVertex)),
                                                          indices.data(), static_cast<uint32_t>(indices.size())))
            {
                return false;
            }
            ctx.ScopeRef.TrackMesh(mesh);
            return true;
        }

        /// 影を落とさない両面の発光の材質。不透明度 1 の半透明の経路（前方の描画）で描き、レイトレーシングのシーンに入れない。
        /// 灯りの光は点光源が出すので、発光体を RTGI の光源の標本として二重に数えない（数えると、小さく明るい面の標本が
        /// 壁や地面の照り返しに粒を出す）
        MaterialHandle CreateEmitterMaterial(GameModeContext &ctx, const char *name, const float (&color)[3], float nits)
        {
            MaterialCreateData material;
            material.DebugName = name;
            for (uint32_t channel = 0; channel < 3; ++channel)
            {
                material.EmissiveColor[channel] = color[channel];
                material.BaseColor[channel] = kEmitterAlbedo;
            }
            material.BaseColor[3] = 1.0f;
            material.Blend = BlendMode::Translucent;
            material.EmissiveLuminanceNits = nits;
            material.Metallic = 0.0f;
            material.Roughness = 1.0f;
            material.HeightScale = 0.0f;
            material.bTwoSided = true;
            material.bCastShadows = false;
            const MaterialHandle handle = ctx.RenderResourcesRef.Materials().Create(material);
            if (handle.IsValid())
            {
                ctx.ScopeRef.TrackMaterial(handle);
            }
            return handle;
        }

        /// 小物と同じ位置・向き・拡大率の発光体の Entity を置く（炎は拡大率に楕円体の半径を掛ける）
        Entity *SpawnEmitter(GameModeContext &ctx, const Layout::PropPlacement &placement, uint32_t meshId,
                             MaterialHandle material, const NorvesLib::Math::Vector3 &position, const float (&scale)[3])
        {
            Entity *object = ctx.ScopeRef.SpawnObject<Entity>();
            auto *mesh = object != nullptr ? ctx.WorldRef.CreateComponent<Component::MeshComponent>(object) : nullptr;
            if (mesh == nullptr)
            {
                return nullptr;
            }
            object->SetPosition(position.x, position.y, position.z);
            object->SetScale(scale[0], scale[1], scale[2]);
            object->SetRotation(NorvesLib::Math::Quaternion(NorvesLib::Math::Vector3(0.0f, 1.0f, 0.0f), placement.Yaw));
            mesh->SetMeshHandle(MeshDataHandle{meshId});
            mesh->SetMaterial(0, material);
            mesh->SetCastShadow(false);
            for (uint32_t channel = 0; channel < 3; ++channel)
            {
                mesh->SetCustomData(channel, kEmitterAlbedo);
            }
            mesh->SetCustomData(3, 1.0f);
            return object;
        }

        Component::PointLightComponent *SpawnPointLight(GameModeContext &ctx, const NorvesLib::Math::Vector3 &position,
                                                        const float *pColor, float lumens, float range, bool bCastShadows)
        {
            Entity *owner = ctx.ScopeRef.SpawnObject<Entity>();
            auto *light = owner != nullptr ? ctx.WorldRef.CreateComponent<Component::PointLightComponent>(owner) : nullptr;
            if (light == nullptr)
            {
                return nullptr;
            }
            owner->SetPosition(position.x, position.y, position.z);
            light->SetLightColor(pColor[0], pColor[1], pColor[2]);
            light->SetIntensityUnit(Component::LightIntensityUnit::Lumen);
            light->SetIntensity(lumens);
            light->SetRange(range);
            light->SetCastShadows(bCastShadows);
            light->SetLightVisible(true);
            return light;
        }

        /// 揺らぎの倍率（周波数の違う 3 つの正弦の和。1 を中心に ±amount ほど）
        float FlickerFactor(float timeSeconds, float phase, float amount)
        {
            const float wave = 0.55f * std::sin(kTwoPi * 1.3f * timeSeconds + phase) +
                               0.3f * std::sin(kTwoPi * 3.1f * timeSeconds + 1.7f * phase) +
                               0.15f * std::sin(kTwoPi * 7.9f * timeSeconds + 2.3f * phase);
            return 1.0f + amount * wave;
        }
    } // namespace

    bool CastsShadow(Layout::PropKind kind)
    {
        return kind != Layout::PropKind::StreetLamp;
    }

    void Place(GameModeContext &ctx, const VariableArray<Layout::PropPlacement> &placed, State &state)
    {
        state = State{};
        VariableArray<Mesh3DVertex> vertices;
        VariableArray<uint32_t> indices;
        BuildLanternGlass(vertices, indices);
        const bool bLanternMesh = RegisterMesh(ctx, kLanternGlassMeshHandleId, vertices, indices);
        ProceduralMeshGenerator::GenerateUVSphere(1.0f, 16, 8, vertices, indices);
        const bool bFlameMesh = RegisterMesh(ctx, kFlameMeshHandleId, vertices, indices);
        vertices.clear();
        indices.clear();
        BuildDoorwayGlow(vertices, indices);
        const bool bDoorwayMesh = RegisterMesh(ctx, kDoorwayGlowMeshHandleId, vertices, indices);
        if (!bLanternMesh || !bFlameMesh || !bDoorwayMesh)
        {
            LOG_ERROR("night-village: 灯りの発光体のメッシュを登録できませんでした");
        }
        const MaterialHandle lanternMaterial = bLanternMesh
                                                   ? CreateEmitterMaterial(ctx, "NightLanternGlass", kLampColor, kLanternGlassNits)
                                                   : MaterialHandle::Invalid();
        const MaterialHandle flameMaterial =
            bFlameMesh ? CreateEmitterMaterial(ctx, "NightFlame", kFireColor, kFlameNits) : MaterialHandle::Invalid();
        const MaterialHandle doorwayMaterial =
            bDoorwayMesh ? CreateEmitterMaterial(ctx, "NightDoorwayGlow", kRoomColor, kRoomNits) : MaterialHandle::Invalid();

        uint32_t lamps = 0;
        uint32_t torches = 0;
        uint32_t campfires = 0;
        uint32_t doorways = 0;
        constexpr float kUnitScale[3] = {1.0f, 1.0f, 1.0f};
        for (const Layout::PropPlacement &placement : placed)
        {
            FlickerLight flicker;
            NorvesLib::Math::Vector3 lightPosition;
            float range = 0.0f;
            const float *pColor = kFireColor;
            bool bCastShadows = true;
            switch (placement.Kind)
            {
            case Layout::PropKind::StreetLamp:
            {
                if (lanternMaterial.IsValid())
                {
                    const float scale[3] = {placement.Scale, placement.Scale, placement.Scale};
                    SpawnEmitter(ctx, placement, kLanternGlassMeshHandleId, lanternMaterial,
                                 NorvesLib::Math::Vector3(placement.X, placement.Y, placement.Z), scale);
                }
                lightPosition = ToWorld(placement, 0.0f, kLampLightY, 0.0f);
                flicker.BaseLumens = kLampLumens;
                range = kLampRange;
                pColor = kLampColor;
                ++lamps;
                break;
            }
            case Layout::PropKind::TorchPost:
            case Layout::PropKind::CampfireRing:
            {
                const bool bCampfire = placement.Kind == Layout::PropKind::CampfireRing;
                const float *pRadii = bCampfire ? kCampfireFlameRadii : kTorchFlameRadii;
                const float flameScale[3] = {pRadii[0] * placement.Scale, pRadii[1] * placement.Scale,
                                             pRadii[2] * placement.Scale};
                if (flameMaterial.IsValid())
                {
                    SpawnEmitter(ctx, placement, kFlameMeshHandleId, flameMaterial,
                                 ToWorld(placement, 0.0f, bCampfire ? kCampfireFlameCenterY : kTorchFlameCenterY, 0.0f),
                                 flameScale);
                }
                lightPosition = ToWorld(placement, 0.0f, bCampfire ? kCampfireLightY : kTorchLightY, 0.0f);
                flicker.BaseLumens = bCampfire ? kCampfireLumens : kTorchLumens;
                flicker.FlickerAmount = bCampfire ? kCampfireFlicker : kTorchFlicker;
                range = bCampfire ? kCampfireRange : kTorchRange;
                // 松明の灯は影無し。火かごの上の 1 点から影を描くと、火かごの縁が足元に半径 2 m ほどの硬い円盤の影を
                // 落とす（実際の炎は火かごより上へ広がる大きな光源で、足元の影は淡くぼける）
                bCastShadows = bCampfire;
                if (bCampfire)
                {
                    ++campfires;
                }
                else
                {
                    ++torches;
                }
                break;
            }
            case Layout::PropKind::AdobeRuin:
            {
                if (doorwayMaterial.IsValid())
                {
                    SpawnEmitter(ctx, placement, kDoorwayGlowMeshHandleId, doorwayMaterial,
                                 NorvesLib::Math::Vector3(placement.X, placement.Y, placement.Z), kUnitScale);
                    ++doorways;
                }
                continue;
            }
            default:
                continue;
            }
            flicker.pLight = SpawnPointLight(ctx, lightPosition, pColor, flicker.BaseLumens, range, bCastShadows);
            if (flicker.pLight == nullptr)
            {
                LOG_ERROR("night-village: 灯りの点光源を作れませんでした");
                continue;
            }
            // 灯りごとに揺らぎの位相をずらす（番号の黄金比の端数）
            const float index = static_cast<float>(state.Lights.size());
            flicker.Phase = kTwoPi * (index * 0.618034f - std::floor(index * 0.618034f));
            LOG_INFO_F("NIGHT_VILLAGE_POINT_LIGHT id=%s x=%.2f y=%.2f z=%.2f lumens=%.0f range=%.1f flicker=%.2f "
                       "cast_shadows=%d",
                       Layout::GetPropKindSpec(placement.Kind).Id, static_cast<double>(lightPosition.x),
                       static_cast<double>(lightPosition.y), static_cast<double>(lightPosition.z),
                       static_cast<double>(flicker.BaseLumens), static_cast<double>(range),
                       static_cast<double>(flicker.FlickerAmount), bCastShadows ? 1 : 0);
            state.Lights.push_back(flicker);
        }
        LOG_INFO_F("NIGHT_VILLAGE_LIGHTS point_lights=%zu lamps=%u torches=%u campfires=%u doorways=%u shadowed_max=4",
                   state.Lights.size(), lamps, torches, campfires, doorways);
    }

    void Tick(State &state, float deltaTime)
    {
        if (state.Lights.empty())
        {
            return;
        }
        state.TimeSeconds += deltaTime;
        // 揺らいでいることを確かめられるよう、数秒おきに揺らす灯の今の光束を 1 行に並べて出す
        const bool bLog = state.TimeSeconds >= state.NextLogSeconds;
        char text[256] = {};
        size_t length = 0;
        for (FlickerLight &flicker : state.Lights)
        {
            if (flicker.FlickerAmount <= 0.0f || flicker.pLight == nullptr)
            {
                continue;
            }
            const float lumens =
                flicker.BaseLumens * FlickerFactor(state.TimeSeconds, flicker.Phase, flicker.FlickerAmount);
            flicker.pLight->SetIntensity(lumens);
            if (bLog && length < sizeof(text))
            {
                const int written = std::snprintf(text + length, sizeof(text) - length, "%s%.0f", length > 0 ? "," : "",
                                                  static_cast<double>(lumens));
                length += written > 0 ? static_cast<size_t>(written) : 0;
            }
        }
        if (bLog)
        {
            state.NextLogSeconds = state.TimeSeconds + kFlickerLogIntervalSeconds;
            LOG_INFO_F("NIGHT_VILLAGE_FLICKER t=%.2f lumens=%s", static_cast<double>(state.TimeSeconds), text);
        }
    }

    void Leave(State &state)
    {
        state = State{};
    }

} // namespace Game::GameModes::WastelandNightLights
