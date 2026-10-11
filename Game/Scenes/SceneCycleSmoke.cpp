#include "Scenes/SceneCycleSmoke.h"

#include "Core/Public/Engine/Engine.h"
#include "Core/Public/GameMode/IGameModeController.h"
#include "Core/Public/Logging/LogMacros.h"
#include "Core/Public/Object/Entity.h"
#include "Core/Public/Object/World.h"
#include "Core/Public/Rendering/RenderResources.h"
#include "Core/Public/Rendering/RenderWorld.h"
#include "GameModes/GameModeIds.h"

#include <chrono>
#include <cstdio>
#include <cstring>

using namespace NorvesLib::Core::Container;

namespace Game::Scenes
{
    namespace
    {
        /// 値が続けて同じであるべきフレーム数（読み込みと遅延解放が落ち着いたとみなす）
        constexpr uint32_t kStableFrames = 90;
        /// ランチャーに最低限とどまるフレーム数（Leave 要求が適用されるまでの余裕）
        constexpr uint32_t kMinHomeFrames = 60;
        /// シーンに最低限とどまるフレーム数（組み立てを始めさせる）
        constexpr uint32_t kMinDwellFrames = 240;
        /// 落ち着くのを待つ上限（描画フレーム数）。超えたら落ち着かないまま測る
        constexpr uint32_t kTimeoutFrames = 4000;
        /// 落ち着くのを待つ上限（秒）。シェーダーのコンパイルなどで描画が進まない間も数える
        constexpr std::chrono::seconds kTimeoutSeconds(300);

        /// Snapshot::Values の並びと同じ順の名前
        constexpr const char *kFieldNames[SceneCycleSmoke::kFieldCount] = {
            "materials", "textures", "meshes", "models", "entities",
            "buffers", "texture_bytes", "buffer_bytes", "sparse_used_bytes",
            "geometry_pool_capacity_bytes", "geometry_pool_used_bytes", "mesh_buffer_bytes",
        };

        std::size_t CountEntities(const NorvesLib::Core::Entity *entity)
        {
            std::size_t count = 1;
            for (const NorvesLib::Core::Entity *child : entity->GetChildEntities())
            {
                count += CountEntities(child);
            }
            return count;
        }

        unsigned long long ToULL(uint64_t value)
        {
            return static_cast<unsigned long long>(value);
        }
    } // namespace

    bool SceneCycleSmoke::Snapshot::operator==(const Snapshot &other) const
    {
        for (std::size_t index = 0; index < kFieldCount; ++index)
        {
            if (Values[index] != other.Values[index])
            {
                return false;
            }
        }
        return true;
    }

    bool SceneCycleSmoke::ParseScenes(const char *sceneList)
    {
        m_Scenes.clear();
        m_bEnabled = false;
        if (sceneList == nullptr || sceneList[0] == '\0')
        {
            LOG_ERROR("--scene-cycle-smoke にはシーンの id をカンマ区切りで指定してください");
            return false;
        }

        const char *cursor = sceneList;
        while (true)
        {
            const char *end = std::strchr(cursor, ',');
            const std::size_t length = end != nullptr ? static_cast<std::size_t>(end - cursor) : std::strlen(cursor);
            char token[64] = {};
            if (length == 0 || length >= sizeof(token))
            {
                LOG_ERROR("--scene-cycle-smoke のシーンの id が空か長すぎます");
                return false;
            }
            std::memcpy(token, cursor, length);

            const SceneDescriptor *scene = FindScene(token);
            if (scene == nullptr)
            {
                LOG_ERROR_F("SCENE_UNKNOWN id=\"%s\" --scene-cycle-smoke のシーンが一覧にありません", token);
                return false;
            }
            for (const SceneDescriptor *existing : m_Scenes)
            {
                if (existing == scene)
                {
                    LOG_ERROR_F("--scene-cycle-smoke のシーンが重複しています: %s", token);
                    return false;
                }
            }
            m_Scenes.push_back(scene);

            if (end == nullptr)
            {
                break;
            }
            cursor = end + 1;
        }

        m_bEnabled = true;
        return true;
    }

    void SceneCycleSmoke::SetCycleCount(uint32_t cycleCount)
    {
        m_CycleCount = cycleCount;
    }

    void SceneCycleSmoke::BeginVisit()
    {
        const SceneDescriptor *scene = m_Scenes[m_SceneIndex];
        LOG_INFO_F("SCENE_CYCLE_ENTER cycle=%u scene=%s", m_Cycle + 1, scene->Id);
        m_pController->RequestChange(scene->Mode, m_ModeParams);
        EnterPhase(Phase::DwellScene);
    }

    void SceneCycleSmoke::EnterPhase(Phase phase)
    {
        m_Phase = phase;
        m_PhaseFrames = 0;
        m_StableFrames = 0;
        m_PhaseStart = std::chrono::steady_clock::now();
    }

    void SceneCycleSmoke::RecordSample(bool bSettled)
    {
        Sample sample;
        sample.Cycle = m_Cycle;
        sample.SceneIndex = m_SceneIndex;
        sample.bSettled = bSettled;
        sample.Values = m_Last;
        m_Samples.push_back(sample);

        char line[512];
        int length = std::snprintf(line, sizeof(line), "SCENE_CYCLE_SAMPLE cycle=%u scene=%s settled=%d frames=%u",
                                   m_Cycle + 1, m_Scenes[m_SceneIndex]->Id, bSettled ? 1 : 0, m_PhaseFrames);
        for (std::size_t index = 0; index < kFieldCount && length > 0 && static_cast<std::size_t>(length) < sizeof(line);
             ++index)
        {
            length += std::snprintf(line + length, sizeof(line) - static_cast<std::size_t>(length), " %s=%llu",
                                    kFieldNames[index], ToULL(sample.Values.Values[index]));
        }
        LOG_INFO_F("%s", line);
    }

    void SceneCycleSmoke::Finish(NorvesLib::Core::Engine::Engine &engine)
    {
        // 基準の周回より後を、同じシーンの基準の周回と比べる。値が違えば（増えても減っても）失敗。
        // 落ち着かないまま測った値は比べられないので、それも失敗にする。
        // シーンが 2 つ以上のときは、1周目を基準にしない。後のシーンが読むテクスチャのキャッシュ（別のシーンが先に読んだもの）が
        // 1周目の時点ではまだ無く、2周目で増えるため。全シーンを 1 回ずつ訪れた後の 2周目を基準にする。
        const std::size_t sceneCount = m_Scenes.size();
        const uint32_t baselineCycle = sceneCount >= 2 ? 1 : 0;
        const uint32_t requiredCycles = baselineCycle + 2;
        bool bChanged[kFieldCount] = {};
        bool bAnyChanged = false;
        bool bUnsettled = false;
        for (const Sample &sample : m_Samples)
        {
            if (!sample.bSettled)
            {
                bUnsettled = true;
                LOG_ERROR_F("SCENE_CYCLE_UNSETTLED cycle=%u scene=%s 値が落ち着く前に測りました", sample.Cycle + 1,
                            m_Scenes[sample.SceneIndex]->Id);
            }
            if (sample.Cycle <= baselineCycle)
            {
                continue;
            }
            const Sample &base = m_Samples[baselineCycle * sceneCount + sample.SceneIndex];
            for (std::size_t index = 0; index < kFieldCount; ++index)
            {
                const uint64_t baseValue = base.Values.Values[index];
                const uint64_t nowValue = sample.Values.Values[index];
                if (baseValue == nowValue)
                {
                    continue;
                }
                LOG_ERROR_F("SCENE_CYCLE_DIFF cycle=%u scene=%s field=%s base=%llu now=%llu change=%s", sample.Cycle + 1,
                            m_Scenes[sample.SceneIndex]->Id, kFieldNames[index], ToULL(baseValue), ToULL(nowValue),
                            nowValue > baseValue ? "grew" : "shrank");
                bChanged[index] = true;
                bAnyChanged = true;
            }
        }

        char changedKinds[256] = "none";
        if (bAnyChanged)
        {
            changedKinds[0] = '\0';
            for (std::size_t index = 0; index < kFieldCount; ++index)
            {
                if (bChanged[index])
                {
                    std::snprintf(changedKinds + std::strlen(changedKinds), sizeof(changedKinds) - std::strlen(changedKinds),
                                  "%s%s", changedKinds[0] != '\0' ? "," : "", kFieldNames[index]);
                }
            }
        }

        const bool bEnoughCycles = m_CycleCount >= requiredCycles;
        if (!bEnoughCycles)
        {
            LOG_ERROR_F("SCENE_CYCLE_TOO_FEW cycles=%u scenes=%u 基準の周回の後に比べる周回が無いため比べられません。%u 以上を指定してください",
                        m_CycleCount, static_cast<unsigned>(sceneCount), requiredCycles);
        }
        else
        {
            LOG_INFO_F("SCENE_CYCLE_BASELINE cycle=%u scenes=%u", baselineCycle + 1, static_cast<unsigned>(sceneCount));
        }
        const bool bPass = !bAnyChanged && !bUnsettled && bEnoughCycles && m_Samples.size() == m_CycleCount * sceneCount;
        LOG_INFO_F("SCENE_CYCLE_SMOKE result=%s cycles=%u scenes=%u changed=%s unsettled=%d", bPass ? "pass" : "fail",
                   m_CycleCount, static_cast<unsigned>(sceneCount), changedKinds, bUnsettled ? 1 : 0);
        m_Phase = Phase::Done;
        engine.RequestExit(bPass ? 0 : 1);
    }

    void SceneCycleSmoke::Update(NorvesLib::Core::Engine::Engine &engine,
                                 NorvesLib::Core::GameMode::IGameModeController &controller)
    {
        if (!m_bEnabled || m_Phase == Phase::Done || m_Scenes.empty())
        {
            return;
        }
        m_pController = &controller;

        // 描画が進んだフレームだけ数える。GameThread は RenderThread のシェーダーのコンパイル中も回り続けるので、
        // 更新の回数で待つと、描画が始まる前に待ち時間が尽きる。
        const uint64_t renderedFrame = engine.GetRenderWorld().GetRenderedFrameCount();
        const bool bRenderedNewFrame = renderedFrame != m_LastRenderedFrame;
        m_LastRenderedFrame = renderedFrame;
        if (m_PhaseStart == std::chrono::steady_clock::time_point())
        {
            m_PhaseStart = std::chrono::steady_clock::now();
        }
        if (bRenderedNewFrame)
        {
            ++m_PhaseFrames;
        }
        const bool bTimedOut = m_PhaseFrames >= kTimeoutFrames ||
                               std::chrono::steady_clock::now() - m_PhaseStart >= kTimeoutSeconds;

        // いまの値を測る。前のフレームと同じ値が続いたフレーム数を数え、読み込み中・遅延解放待ちの間は 0 に戻す。
        NorvesLib::Core::Rendering::RenderResources &resources = engine.GetRenderResources();
        const NorvesLib::Core::Rendering::ResourceStats stats = resources.GetResourceStats();
        std::size_t entityCount = 0;
        for (const NorvesLib::Core::Entity *root : engine.GetWorld().GetRootEntities())
        {
            entityCount += CountEntities(root);
        }
        Snapshot now;
        now.Values[0] = resources.Materials().GetCount();
        now.Values[1] = stats.TextureCount;
        now.Values[2] = resources.Meshes().GetCount();
        now.Values[3] = resources.MegaGeometry().GetModelCount();
        now.Values[4] = entityCount;
        now.Values[5] = stats.BufferCount;
        now.Values[6] = stats.TextureBytes;
        now.Values[7] = stats.TotalBufferMemory;
        now.Values[8] = stats.SparsePoolUsedBytes;
        now.Values[9] = stats.GeometryPoolCapacityBytes;
        now.Values[10] = stats.GeometryPoolUsedBytes;
        now.Values[11] = stats.MeshBufferBytes;

        const bool bQuiet = !engine.GetRenderWorld().HasPendingAsyncAssets() && resources.GetPendingRetireCount() == 0;
        if (m_bHasLast && now == m_Last && bQuiet)
        {
            if (bRenderedNewFrame)
            {
                ++m_StableFrames;
            }
        }
        else
        {
            m_StableFrames = 0;
        }
        m_Last = now;
        m_bHasLast = true;

        const bool bStable = m_StableFrames >= kStableFrames;
        switch (m_Phase)
        {
        case Phase::WaitHome:
            if ((bStable && m_PhaseFrames >= kMinHomeFrames) || bTimedOut)
            {
                BeginVisit();
            }
            break;
        case Phase::DwellScene:
            if ((bStable && m_PhaseFrames >= kMinDwellFrames) || bTimedOut)
            {
                LOG_INFO_F("SCENE_CYCLE_LEAVE cycle=%u scene=%s settled=%d frames=%u", m_Cycle + 1,
                           m_Scenes[m_SceneIndex]->Id, bStable ? 1 : 0, m_PhaseFrames);
                controller.RequestChange(Game::GameModes::Launcher);
                EnterPhase(Phase::SettleHome);
            }
            break;
        case Phase::SettleHome:
            if ((bStable && m_PhaseFrames >= kMinHomeFrames) || bTimedOut)
            {
                RecordSample(bStable);
                ++m_SceneIndex;
                if (m_SceneIndex >= m_Scenes.size())
                {
                    m_SceneIndex = 0;
                    ++m_Cycle;
                }
                if (m_Cycle >= m_CycleCount)
                {
                    Finish(engine);
                }
                else
                {
                    BeginVisit();
                }
            }
            break;
        case Phase::Done:
            break;
        }
    }

} // namespace Game::Scenes
