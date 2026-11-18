// 配置編集の命令をゲームモードの更新で実行し、結果を ImGui に表示する。
#if defined(NORVES_ENABLE_IMGUI)

#include "Gameplay/Level/LevelEditView.h"

#include "Container/PointerTypes.h"
#include "Engine/Engine.h"
#include "Engine/NorvesEngine.h"
#include "GameMode/GameModeScope.h"
#include "GameMode/IGameModeController.h"
#include "GameMode/ISubRoutine.h"
#include "Gameplay/Level/LevelMarkerComponent.h"
#include "Logging/LogMacros.h"
#include "Math/MathTypes.h"
#include "Math/Vector4.h"
#include "Object/Entity.h"
#include "Object/World.h"
#include "Rendering/CameraPicking.h"
#include "Rendering/DebugDrawQueue.h"
#include "Rendering/RenderWorld.h"
#include "Rendering/Terrain/TerrainHeightField.h"
#include "Rendering/Terrain/TerrainSystem.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <utility>

#include "imgui.h"

namespace Game::LevelEdit
{
    namespace Core = NorvesLib::Core;
    namespace Level = NorvesLib::Modules::Gameplay::Level;
    namespace Math = NorvesLib::Math;
    using Core::Container::String;
    using Core::Container::VariableArray;

    namespace
    {
        const char* const kKindLabels[] =
        {
            "持ち場", "寝床", "ファストトラベル点", "見どころ", "動物の出現点", "痕跡", "痕跡の点"
        };
        static_assert(sizeof(kKindLabels) / sizeof(kKindLabels[0]) == Level::kLevelMarkerKindCount);
        const char* const kPaintLayerLabels[] = {"草の密度", "枯れ"};
        const char* const kPaintOperationLabels[] = {"塗る", "消す"};
        static_assert(sizeof(kPaintLayerLabels) / sizeof(kPaintLayerLabels[0]) == Level::kLevelPaintLayerCount);

        const char* GetKindLabel(Level::ELevelMarkerKind kind)
        {
            const auto index = static_cast<uint32_t>(kind);
            return index < Level::kLevelMarkerKindCount ? kKindLabels[index] : "不明";
        }

        String CountText(size_t count)
        {
            char buffer[32];
            std::snprintf(buffer, sizeof(buffer), "%zu", count);
            return String(buffer);
        }

        String GetMarkerKey(const Core::Entity& entity)
        {
            const auto* marker = entity.GetComponent<Level::LevelMarkerComponent>();
            return marker != nullptr ? *marker->getKey() : String();
        }

        Math::Vector3 BrushCirclePoint(const Core::Rendering::Terrain::TerrainSystem& terrains,
                                       const Math::Vector3& center, float radius, float angle)
        {
            Math::Vector3 point(center.x + radius * std::cos(angle), center.y, center.z + radius * std::sin(angle));
            const auto field = terrains.FindTerrain(point.x, point.z);
            if (field)
            {
                const float height = field->SampleHeight(point.x, point.z);
                if (std::isfinite(height))
                {
                    point.y = height;
                }
            }
            point.y += 0.1f;
            return point;
        }

        void DrawBrushCircle(Core::Rendering::DebugDrawQueue& queue,
                             const Core::Rendering::Terrain::TerrainSystem& terrains,
                             const Math::Vector3& center, float radius, const Math::Vector4& color)
        {
            constexpr uint32_t kSegments = 48;
            Math::Vector3 previous = BrushCirclePoint(terrains, center, radius, 0.0f);
            for (uint32_t index = 1; index <= kSegments; ++index)
            {
                const float angle = Math::Constants::TWO_PI * static_cast<float>(index) / static_cast<float>(kSegments);
                const Math::Vector3 next = BrushCirclePoint(terrains, center, radius, angle);
                queue.AddLine(previous, next, color);
                previous = next;
            }
        }
    } // namespace

    /// モードが所有し、画面を借用する。画面の終了時は借用を明示的に外す。
    class LevelEditSubRoutine final : public Core::GameMode::ISubRoutine
    {
    public:
        explicit LevelEditSubRoutine(LevelEditView* view) : m_pView(view)
        {
        }
        ~LevelEditSubRoutine() override
        {
            if (m_pView != nullptr)
            {
                m_pView->OnSubRoutineDestroyed(*this);
            }
        }

        void Enter(Core::GameMode::GameModeContext& ctx) override
        {
            if (m_pView != nullptr)
            {
                m_pView->AttachToMode(*this, ctx);
            }
        }

        void Tick(Core::GameMode::GameModeContext& ctx, float deltaTime) override
        {
            (void)deltaTime;
            if (m_pView != nullptr)
            {
                m_pView->TickFromMode(*this, ctx);
            }
        }

        void Leave(Core::GameMode::GameModeContext& ctx) override
        {
            (void)ctx;
            if (m_pView != nullptr)
            {
                m_pView->DetachFromMode(*this);
            }
        }

        void ClearView()
        {
            m_pView = nullptr;
        }
        const char* DebugName() const override
        {
            return "LevelEdit";
        }

    private:
        LevelEditView* m_pView;
    };

    LevelEditView::LevelEditView()
    {
        m_Operations.SetEntityOwner(this);
    }

    LevelEditView::~LevelEditView()
    {
        Shutdown();
    }

    void LevelEditView::SetGameModeController(Core::GameMode::IGameModeController* controller)
    {
        if (m_pController != controller)
        {
            Shutdown();
            m_pController = controller;
        }
    }

    void LevelEditView::Shutdown()
    {
        if (m_pSubRoutine != nullptr)
        {
            m_pSubRoutine->ClearView();
            m_pSubRoutine = nullptr;
        }
        m_pScope = nullptr;
        m_bAttached = false;
        m_pController = nullptr;
        ResetModeState();
    }

    void LevelEditView::ResetModeState()
    {
        m_Operations.ClearSelection();
        m_Commands.clear();
        m_MarkerCount = 0;
        m_bHasSelection = false;
        m_SelectedKey.clear();
        m_SelectedPosition = Math::Vector3();
        m_DrawStats = {};
        m_LastResult.clear();
    }

    void LevelEditView::AttachToMode(LevelEditSubRoutine& sub, Core::GameMode::GameModeContext& ctx)
    {
        if (&sub == m_pSubRoutine)
        {
            m_pScope = &ctx.ScopeRef;
            m_bAttached = true;
            ResetModeState();
        }
    }

    void LevelEditView::DetachFromMode(LevelEditSubRoutine& sub)
    {
        if (&sub == m_pSubRoutine)
        {
            m_pScope = nullptr;
            m_bAttached = false;
            m_pSubRoutine = nullptr;
            ResetModeState();
        }
    }

    void LevelEditView::OnSubRoutineDestroyed(LevelEditSubRoutine& sub)
    {
        DetachFromMode(sub);
    }

    void LevelEditView::OnLevelEditRootSpawned(Core::Entity& root)
    {
        if (m_pScope != nullptr)
        {
            m_pScope->TrackObject(&root);
        }
    }

    void LevelEditView::OnLevelEditBeforeDestroy(Core::Entity& entity)
    {
        if (m_pScope != nullptr)
        {
            m_pScope->Untrack(&entity);
        }
    }

    void LevelEditView::Enqueue(ECommandType type, const char* text)
    {
        Command command;
        command.Type = type;
        command.Kind = static_cast<Level::ELevelMarkerKind>(m_MarkerKindIndex);
        command.Text = text;
        command.PaintLayer = static_cast<Level::ELevelPaintLayer>(m_PaintLayerIndex);
        command.Brush = m_PaintBrush;
        command.Brush.Value = static_cast<uint8_t>(std::clamp(m_PaintValue, 0, 255));
        command.MasksDirectory = m_MasksPath;
        command.DrynessSettings = m_DrynessSettings;
        command.bErase = m_PaintOperationIndex == 1;
        command.bFlipZ = m_bPaintFlipZ;
        command.bResetDrynessValues = m_bResetDrynessValues;
        m_Commands.push_back(std::move(command));
    }

    void LevelEditView::SetResult(const String& text, bool bSuccess)
    {
        m_LastResult = text;
        if (bSuccess)
        {
            NORVES_LOG_INFO("LevelEdit", "%s", text.c_str());
        }
        else
        {
            NORVES_LOG_WARNING("LevelEdit", "%s", text.c_str());
        }
    }

    bool LevelEditView::BindPaintMasks(const Command& command, Core::GameMode::GameModeContext& ctx,
                                       const Math::Vector3& point)
    {
        const auto field = ctx.EngineRef.GetTerrainSystem().FindTerrain(point.x, point.z);
        if (!field || !field->IsGrid())
        {
            SetResult("画面の中心に格子の地形がありません", false);
            return false;
        }

        String error;
        const auto layout = Level::MakeLevelPaintMaskLayout(field->GetGridLayout(), command.bFlipZ);
        if (!m_Operations.BindPaintMasks(layout, command.MasksDirectory, &error))
        {
            SetResult(error, false);
            return false;
        }
        m_bDrynessBaselineNeedsSave = false;
        const bool bResetValues = !m_Operations.WasPaintMaskLoadedFromFile(Level::ELevelPaintLayer::Dryness);
        if (!m_Operations.RegenerateDrynessBaseline(ctx.WorldRef, command.DrynessSettings, bResetValues,
                                                    nullptr, nullptr, &error))
        {
            SetResult("マスクは合わせましたが、" + error, false);
            return false;
        }
        m_bDrynessBaselineNeedsSave = bResetValues;
        SetResult("地形にマスクを合わせました: " + command.MasksDirectory);
        return true;
    }

    void LevelEditView::ExecuteCommand(const Command& command, Core::GameMode::GameModeContext& ctx,
                                       const Math::Ray* ray)
    {
        auto& world = ctx.WorldRef;
        const auto& query = ctx.EngineRef.GetSceneQuery();
        const bool bNeedsRay = command.Type == ECommandType::Place || command.Type == ECommandType::Pick ||
                               command.Type == ECommandType::Move || command.Type == ECommandType::BindPaintMasks ||
                               command.Type == ECommandType::ApplyPaint;
        if (bNeedsRay && ray == nullptr)
        {
            SetResult("カメラがありません", false);
            return;
        }

        Core::Entity* selected = m_Operations.GetSelected(world);
        const bool bNeedsSelection = command.Type == ECommandType::Move || command.Type == ECommandType::Delete ||
                                     command.Type == ECommandType::Rename;
        if (bNeedsSelection && selected == nullptr)
        {
            SetResult("選択なし", false);
            return;
        }

        Math::Vector3 point;
        Math::Vector3 normal;
        if ((command.Type == ECommandType::Place || command.Type == ECommandType::Move ||
             command.Type == ECommandType::BindPaintMasks || command.Type == ECommandType::ApplyPaint) &&
            !m_Operations.RaycastGround(query, *ray, Level::LevelEditOperations::kDefaultRayDistance, point, normal))
        {
            SetResult("画面の中心の先に地面がありません", false);
            return;
        }

        switch (command.Type)
        {
        case ECommandType::Place:
        {
            auto* placed = m_Operations.PlaceMarker(world, query, command.Kind, point, command.Text);
            if (placed != nullptr)
            {
                SetResult("置きました: " + GetMarkerKey(*placed));
            }
            else
            {
                SetResult("置けませんでした(Key が使用中か、痕跡の点は痕跡を選んでから置きます)", false);
            }
            break;
        }
        case ECommandType::Pick:
        {
            auto* picked = m_Operations.PickMarker(world, *ray);
            m_Operations.Select(picked);
            SetResult(picked != nullptr ? "選びました: " + GetMarkerKey(*picked) :
                      String("画面の中心にマーカーがありません"), picked != nullptr);
            break;
        }
        case ECommandType::Move:
        {
            const bool bMoved = m_Operations.MoveMarker(world, query, *selected, point);
            SetResult(bMoved ? "動かしました" : "動かせませんでした", bMoved);
            break;
        }
        case ECommandType::Delete:
        {
            const bool bDeleted = m_Operations.DeleteMarker(world, *selected);
            SetResult(bDeleted ? "消しました" : "消せませんでした", bDeleted);
            break;
        }
        case ECommandType::Rename:
        {
            const bool bRenamed = m_Operations.SetMarkerKey(world, *selected, command.Text);
            SetResult(bRenamed ? "Key を変えました: " + command.Text :
                      String("Key を変えられません(空か使用中)"), bRenamed);
            break;
        }
        case ECommandType::FindByKey:
        {
            auto* found = m_Operations.FindMarkerByKey(world, command.Text);
            m_Operations.Select(found);
            SetResult(found != nullptr ? "選びました: " + GetMarkerKey(*found) :
                      "Key '" + command.Text + "' のマーカーがありません", found != nullptr);
            break;
        }
        case ECommandType::ReSnap:
        {
            const auto stats = m_Operations.ReSnapAll(world, query);
            SetResult("再吸着: 合わせた " + CountText(stats.Snapped) + "・地面なし " + CountText(stats.Missed));
            break;
        }
        case ECommandType::Save:
        {
            size_t count = 0;
            String error;
            if (m_Operations.SaveMarkers(world, command.Text, &count, &error))
            {
                SetResult("保存しました: " + CountText(count) + " 個のルート → " + command.Text);
            }
            else
            {
                SetResult(error, false);
            }
            break;
        }
        case ECommandType::Load:
        {
            VariableArray<Core::Entity*> roots;
            if (m_Operations.LoadMarkers(world, command.Text, &roots))
            {
                SetResult("読み込みました: " + CountText(roots.size()) + " 個のルート");
            }
            else
            {
                SetResult("読み込めませんでした: " + command.Text + "(ログを見てください)", false);
            }
            break;
        }
        case ECommandType::BindPaintMasks:
        {
            BindPaintMasks(command, ctx, point);
            break;
        }
        case ECommandType::ApplyPaint:
        {
            if (!m_Operations.HasPaintMasks() && !BindPaintMasks(command, ctx, point))
            {
                break;
            }
            const uint32_t changed = command.bErase
                ? m_Operations.EraseMask(command.PaintLayer, point, command.Brush)
                : m_Operations.PaintMask(command.PaintLayer, point, command.Brush);
            SetResult("変わった標本: " + CountText(changed));
            break;
        }
        case ECommandType::SavePaintMasks:
        {
            size_t count = 0;
            String error;
            if (m_Operations.SavePaintMasks(command.MasksDirectory, &count, &error))
            {
                m_bDrynessBaselineNeedsSave = false;
                SetResult("マスクを保存しました: " + CountText(count) + " 層");
            }
            else
            {
                SetResult("マスクを保存できませんでした: " + error, false);
            }
            break;
        }
        case ECommandType::LoadPaintMasks:
        {
            String error;
            if (m_Operations.LoadPaintMasks(&error))
            {
                m_bDrynessBaselineNeedsSave = !m_Operations.WasPaintMaskLoadedFromFile(Level::ELevelPaintLayer::Dryness);
                SetResult("マスクを読み直しました: " + m_Operations.GetPaintMaskDirectory());
            }
            else
            {
                SetResult("マスクを読み直せませんでした: " + error, false);
            }
            break;
        }
        case ECommandType::RegenerateDryness:
        {
            size_t territories = 0;
            size_t witherSources = 0;
            String error;
            if (m_Operations.RegenerateDrynessBaseline(world, command.DrynessSettings, command.bResetDrynessValues,
                                                       &territories, &witherSources, &error))
            {
                if (command.bResetDrynessValues)
                {
                    m_bDrynessBaselineNeedsSave = true;
                }
                SetResult("枯れの初期値を作りました: 持ち場 " + CountText(territories) +
                          "・枯れの源 " + CountText(witherSources));
            }
            else
            {
                SetResult(error, false);
            }
            break;
        }
        }
    }

    void LevelEditView::TickFromMode(LevelEditSubRoutine& sub, Core::GameMode::GameModeContext& ctx)
    {
        if (&sub != m_pSubRoutine || !m_bAttached)
        {
            return;
        }

        const auto camera = ctx.EngineRef.GetRenderWorld().GetRenderingCoordinator().GetMainCamera();
        Math::Ray ray;
        const bool bHasRay = camera.IsValid() && Core::Rendering::BuildPickingRay(
            camera, camera.Viewport.X + camera.Viewport.Width * 0.5f,
            camera.Viewport.Y + camera.Viewport.Height * 0.5f, ray);
        ctx.WorldRef.UpdateWorldTransforms();
        for (const Command& command : m_Commands)
        {
            ExecuteCommand(command, ctx, bHasRay ? &ray : nullptr);
        }
        m_Commands.clear();

        Math::Vector3 brushPoint;
        Math::Vector3 brushNormal;
        const bool bPaintContinuously = m_bContinuousPaint && m_Operations.HasPaintMasks();
        const bool bHasBrushPoint = (bPaintContinuously || m_bDrawBrush) && bHasRay &&
            m_Operations.RaycastGround(ctx.EngineRef.GetSceneQuery(), ray,
                                       Level::LevelEditOperations::kDefaultRayDistance, brushPoint, brushNormal);
        if (bPaintContinuously && bHasBrushPoint)
        {
            const auto layer = static_cast<Level::ELevelPaintLayer>(m_PaintLayerIndex);
            if (m_PaintOperationIndex == 1)
            {
                m_Operations.EraseMask(layer, brushPoint, m_PaintBrush);
            }
            else
            {
                m_Operations.PaintMask(layer, brushPoint, m_PaintBrush);
            }
        }

        // 読み込み直後の子の変換も表示と描画に反映する。
        ctx.WorldRef.UpdateWorldTransforms();
        VariableArray<Core::Entity*> markers;
        m_Operations.CollectMarkers(ctx.WorldRef, markers);
        m_MarkerCount = markers.size();
        const auto* selected = m_Operations.GetSelected(ctx.WorldRef);
        m_bHasSelection = selected != nullptr;
        m_SelectedKey.clear();
        if (selected != nullptr)
        {
            m_SelectedKey = GetMarkerKey(*selected);
            m_SelectedKind = static_cast<Level::ELevelMarkerKind>(Level::kLevelMarkerKindCount);
            Level::TryGetLevelMarkerKind(*selected, m_SelectedKind);
            m_SelectedPosition = selected->GetWorldTransform().position;
        }

        m_DrawStats = {};
        if (m_bDrawMarkers && camera.IsValid())
        {
            auto settings = m_DrawSettings;
            settings.SelectedObjectId = m_Operations.GetSelectedObjectId();
            const Math::Vector3 position(camera.PositionX, camera.PositionY, camera.PositionZ);
            m_DrawStats = DrawLevelMarkers(ctx.WorldRef, position, settings, Core::GEngine.GetDebugDraw());
        }

        if (m_bDrawBrush && bHasBrushPoint)
        {
            auto& queue = Core::GEngine.GetDebugDraw();
            const auto& terrains = ctx.EngineRef.GetTerrainSystem();
            DrawBrushCircle(queue, terrains, brushPoint, m_PaintBrush.Radius, Math::Vector4(0.2f, 1.0f, 0.3f, 1.0f));
            if (m_PaintBrush.Falloff > 0.0f)
            {
                DrawBrushCircle(queue, terrains, brushPoint, m_PaintBrush.Radius * (1.0f - m_PaintBrush.Falloff),
                                Math::Vector4(1.0f, 0.85f, 0.2f, 1.0f));
            }
        }

        m_bPaintMasksBound = m_Operations.HasPaintMasks();
        m_PaintMaskLayout = m_Operations.GetPaintMaskLayout();
        m_BoundMasksDirectory = m_Operations.GetPaintMaskDirectory();
        for (uint32_t index = 0; index < Level::kLevelPaintLayerCount; ++index)
        {
            const auto layer = static_cast<Level::ELevelPaintLayer>(index);
            const auto* mask = m_Operations.GetPaintMask(layer);
            m_bPaintMaskLoaded[index] = m_Operations.WasPaintMaskLoadedFromFile(layer);
            m_bPaintMaskDirty[index] = mask != nullptr &&
                (mask->IsDirty() || (layer == Level::ELevelPaintLayer::Dryness && m_bDrynessBaselineNeedsSave));
        }
    }

    void LevelEditView::OnImGui()
    {
        if (m_pController != nullptr && m_pSubRoutine == nullptr)
        {
            auto sub = Core::Container::MakeUnique<LevelEditSubRoutine>(this);
            m_pSubRoutine = sub.get();
            m_pController->RequestPushSubRoutine(std::move(sub));
        }

        if (!ImGui::Begin("配置の編集"))
        {
            ImGui::End();
            return;
        }
        if (!m_bAttached)
        {
            ImGui::TextUnformatted("ゲームモードへつないでいます");
            ImGui::End();
            return;
        }

        ImGui::Combo("種類", &m_MarkerKindIndex, kKindLabels, static_cast<int>(Level::kLevelMarkerKindCount));
        ImGui::InputText("Key(空なら自動の採番)", m_PlaceKey, sizeof(m_PlaceKey));
        if (ImGui::Button("画面の中心に置く"))
        {
            Enqueue(ECommandType::Place, m_PlaceKey);
        }
        if (ImGui::Button("画面の中心で選ぶ"))
        {
            Enqueue(ECommandType::Pick);
        }
        if (ImGui::Button("選択を画面の中心へ動かす"))
        {
            Enqueue(ECommandType::Move);
        }
        if (ImGui::Button("選択を消す"))
        {
            Enqueue(ECommandType::Delete);
        }

        ImGui::Separator();
        if (m_bHasSelection)
        {
            ImGui::Text("選択中の Key: %s", m_SelectedKey.c_str());
            ImGui::Text("選択中の種類: %s", GetKindLabel(m_SelectedKind));
            ImGui::Text("位置: %.2f, %.2f, %.2f", m_SelectedPosition.x, m_SelectedPosition.y, m_SelectedPosition.z);
        }
        else
        {
            ImGui::TextUnformatted("選択なし");
        }
        ImGui::InputText("新しい Key", m_NewKey, sizeof(m_NewKey));
        if (ImGui::Button("Key を変える"))
        {
            Enqueue(ECommandType::Rename, m_NewKey);
        }
        ImGui::InputText("探す Key", m_FindKey, sizeof(m_FindKey));
        if (ImGui::Button("Key で選ぶ"))
        {
            Enqueue(ECommandType::FindByKey, m_FindKey);
        }
        if (ImGui::Button("全マーカーを再吸着"))
        {
            Enqueue(ECommandType::ReSnap);
        }

        ImGui::Separator();
        ImGui::InputText("保存先", m_Path, sizeof(m_Path));
        if (ImGui::Button("保存"))
        {
            Enqueue(ECommandType::Save, m_Path);
        }
        ImGui::SameLine();
        if (ImGui::Button("読み込み"))
        {
            Enqueue(ECommandType::Load, m_Path);
        }

        ImGui::Separator();
        ImGui::Checkbox("マーカーを描く", &m_bDrawMarkers);
        ImGui::Checkbox("持ち場", &m_DrawSettings.bDrawTerritories);
        ImGui::SameLine();
        ImGui::Checkbox("点", &m_DrawSettings.bDrawPoints);
        ImGui::SameLine();
        ImGui::Checkbox("痕跡", &m_DrawSettings.bDrawTrails);
        ImGui::SliderFloat("描く距離 (m)", &m_DrawSettings.MaxDistance, 10.0f, 2000.0f);
        ImGui::Text("描いた数: %u・距離で外した数: %u・予算で外した数: %u・頂点数: %u",
                    m_DrawStats.Drawn, m_DrawStats.CulledByDistance, m_DrawStats.CulledByBudget, m_DrawStats.VerticesAdded);
        ImGui::Text("マーカーの数: %zu", m_MarkerCount);

        if (ImGui::CollapsingHeader("草の密度・枯れのペイント"))
        {
            ImGui::PushID("PaintMasks");
            if (ImGui::Combo("層", &m_PaintLayerIndex, kPaintLayerLabels, static_cast<int>(Level::kLevelPaintLayerCount)))
            {
                m_PaintValue = m_PaintLayerIndex == 0 ? 0 : 255;
            }
            ImGui::Combo("操作", &m_PaintOperationIndex, kPaintOperationLabels, 2);
            ImGui::SliderFloat("半径 [m]", &m_PaintBrush.Radius, 0.5f, 64.0f);
            ImGui::SliderFloat("強さ", &m_PaintBrush.Strength, 0.0f, 1.0f);
            ImGui::SliderFloat("減衰", &m_PaintBrush.Falloff, 0.0f, 1.0f);
            ImGui::SliderInt("塗る値", &m_PaintValue, 0, 255);
            m_PaintBrush.Radius = std::isfinite(m_PaintBrush.Radius) ? std::clamp(m_PaintBrush.Radius, 0.5f, 64.0f) : 4.0f;
            m_PaintBrush.Strength = std::isfinite(m_PaintBrush.Strength) ? std::clamp(m_PaintBrush.Strength, 0.0f, 1.0f) : 1.0f;
            m_PaintBrush.Falloff = std::isfinite(m_PaintBrush.Falloff) ? std::clamp(m_PaintBrush.Falloff, 0.0f, 1.0f) : 0.5f;
            m_PaintValue = std::clamp(m_PaintValue, 0, 255);
            m_PaintBrush.Value = static_cast<uint8_t>(m_PaintValue);
            ImGui::TextUnformatted("0 = 無し・255 = 最大");
            ImGui::InputText("マスクの置き場", m_MasksPath, sizeof(m_MasksPath));
            ImGui::Checkbox("spec の flip_z と同じにする", &m_bPaintFlipZ);
            ImGui::Checkbox("ブラシの円を描く", &m_bDrawBrush);
            ImGui::Checkbox("連続で塗る（毎更新）", &m_bContinuousPaint);

            ImGui::Separator();
            ImGui::TextUnformatted("枯れの初期値");
            ImGui::SliderFloat("源のない場所の枯れ", &m_DrynessSettings.BaseLevel, 0.0f, 1.0f);
            ImGui::SliderFloat("持ち場の枯れ", &m_DrynessSettings.TerritoryLevel, 0.0f, 1.0f);
            ImGui::InputFloat("持ち場の半径の倍率", &m_DrynessSettings.TerritoryRadiusScale);
            ImGui::InputFloat("持ち場の外側の減衰幅 [m]", &m_DrynessSettings.TerritoryFalloff);
            ImGui::Checkbox("枯れの源を使う", &m_DrynessSettings.bUseWitherSources);
            ImGui::InputFloat("縁の乱れの振幅 [m]", &m_DrynessSettings.EdgeNoise);
            ImGui::InputFloat("雑音の格子の間隔 [m]", &m_DrynessSettings.NoiseScale);
            ImGui::InputScalar("乱数の種", ImGuiDataType_U32, &m_DrynessSettings.Seed);
            ImGui::TextUnformatted("倍率・幅・振幅は 0 以上、振幅が正なら雑音の間隔は正にします");
            ImGui::Checkbox("塗った枯れも初期値へ戻す", &m_bResetDrynessValues);

            if (ImGui::Button("画面の中心の地形に合わせる（未保存の塗りは捨てる）"))
            {
                Enqueue(ECommandType::BindPaintMasks);
            }
            if (ImGui::Button("画面の中心に塗る（1 回）"))
            {
                Enqueue(ECommandType::ApplyPaint);
            }
            if (ImGui::Button("保存"))
            {
                Enqueue(ECommandType::SavePaintMasks);
            }
            ImGui::SameLine();
            if (ImGui::Button("読み直す"))
            {
                Enqueue(ECommandType::LoadPaintMasks);
            }
            if (ImGui::Button("枯れの初期値を作り直す"))
            {
                Enqueue(ECommandType::RegenerateDryness);
            }

            ImGui::Text("地形に合わせた状態: %s", m_bPaintMasksBound ? "合わせ済み" : "未設定");
            if (m_bPaintMasksBound)
            {
                ImGui::Text("格子: %u 列 × %u 行", m_PaintMaskLayout.Columns, m_PaintMaskLayout.Rows);
                ImGui::Text("原点 (FirstX, FirstZ): %.3f, %.3f", m_PaintMaskLayout.FirstX, m_PaintMaskLayout.FirstZ);
                ImGui::Text("間隔: %.3f m・flip_z: %s", m_PaintMaskLayout.Spacing,
                            m_PaintMaskLayout.bFlipZ ? "オン" : "オフ");
                ImGui::TextWrapped("合わせた置き場（読み直し先）: %s", m_BoundMasksDirectory.c_str());
                for (uint32_t index = 0; index < Level::kLevelPaintLayerCount; ++index)
                {
                    ImGui::Text("%s: ファイルから読み込み %s・未保存の変更 %s", kPaintLayerLabels[index],
                                m_bPaintMaskLoaded[index] ? "あり" : "なし", m_bPaintMaskDirty[index] ? "あり" : "なし");
                }
            }
            ImGui::TextWrapped("地形の spec の recipe.paint に grass_density（mode: multiply）と dry_grass（mode: override）でこの PNG を指すと、地形の cook に入ります");
            ImGui::TextWrapped("結果: %s", m_LastResult.c_str());
            ImGui::PopID();
        }
        ImGui::TextWrapped("%s", m_LastResult.c_str());
        ImGui::End();
    }
} // namespace Game::LevelEdit

#endif // NORVES_ENABLE_IMGUI
