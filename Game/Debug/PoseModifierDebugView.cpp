#if defined(NORVES_ENABLE_IMGUI)
#include "PoseModifierDebugView.h"
#include "Core/Public/Animation/SkeletonResource.h"
#include "Core/Public/Component/SkinnedMeshComponent.h"
#include "Core/Public/Engine/NorvesEngine.h"
#include "Core/Public/Math/MatrixUtils.h"
#include "Core/Public/Object/World.h"
#include "Core/Public/Text/JsonWriter.h"
#include <algorithm>
#include <cmath>
#include "imgui.h"

namespace Game::Debug
{
    using namespace NorvesLib::Core;
    namespace A = NorvesLib::Core::Animation;
    namespace C = NorvesLib::Core::Container;
    namespace P = NorvesLib::Core::Animation::Procedural;
    namespace M = NorvesLib::Math;
    namespace
    {
        constexpr size_t kMaxProbes = 32;
        C::String MemberPath(const C::String& base, const C::String& member)
        {
            return base.empty() ? member : base + "." + member;
        }
        C::String ElementPath(const C::String& base, size_t index)
        {
            auto result = base + "[";
            char digits[3 * sizeof(size_t)];
            size_t count = 0;
            do
            {
                digits[count++] = char('0' + index % 10);
                index /= 10;
            } while (index);
            while (count)
            {
                result.push_back(digits[--count]);
            }
            return result + "]";
        }
        // 編集した数値か有効フラグだけを差し替え、ほかの値と配列順を保って複写する。
        void CopyJson(JsonWriter& writer, JsonValue value, const char* key, const C::String& path,
                      const C::String& editPath, double number, bool bBoolean = false)
        {
            if (path == editPath)
            {
                if (bBoolean)
                {
                    writer.WriteBool(key, number != 0);
                }
                else if (key)
                {
                    writer.WriteNumber(key, number);
                }
                else
                {
                    writer.WriteNumberElement(number);
                }
                return;
            }
            if (value.IsObject())
            {
                if (key)
                {
                    writer.BeginObject(key);
                }
                else
                {
                    writer.BeginObject();
                }
                for (size_t i = 0; i < value.GetObjectSize(); ++i)
                {
                    const auto& name = value.GetMemberName(i);
                    CopyJson(writer, value.GetMemberValue(i), name.c_str(), MemberPath(path, name),
                             editPath, number, bBoolean);
                }
                // weight は省略できるため、固定重みの初回操作では追加する。
                if (!value.HasMember("weight") && MemberPath(path, "weight") == editPath)
                {
                    writer.WriteNumber("weight", number);
                }
                writer.EndObject();
            }
            else if (value.IsArray())
            {
                if (key)
                {
                    writer.BeginArray(key);
                }
                else
                {
                    writer.BeginArray();
                }
                for (size_t i = 0; i < value.GetArraySize(); ++i)
                {
                    CopyJson(writer, value.GetArrayElement(i), nullptr, ElementPath(path, i),
                             editPath, number, bBoolean);
                }
                writer.EndArray();
            }
            else if (value.IsString())
            {
                if (key)
                {
                    writer.WriteString(key, value.AsString());
                }
                else
                {
                    writer.WriteStringElement(value.AsString());
                }
            }
            else if (value.IsNumber())
            {
                if (key)
                {
                    writer.WriteNumber(key, value.AsNumber());
                }
                else
                {
                    writer.WriteNumberElement(value.AsNumber());
                }
            }
            else if (value.IsBoolean())
            {
                if (key)
                {
                    writer.WriteBool(key, value.AsBool());
                }
                else
                {
                    writer.WriteBoolElement(value.AsBool());
                }
            }
            else if (key)
            {
                writer.WriteNull(key);
            }
            else
            {
                writer.WriteNullElement();
            }
        }
        template <typename Fields>
        void CollectNumbers(JsonValue value, const C::String& path, Fields& fields)
        {
            if (value.IsNumber())
            {
                fields.push_back({path, value.AsNumber(), value.AsNumber()});
            }
            else if (value.IsObject())
            {
                for (size_t i = 0; i < value.GetObjectSize(); ++i)
                {
                    CollectNumbers(value.GetMemberValue(i), MemberPath(path, value.GetMemberName(i)), fields);
                }
            }
            else if (value.IsArray())
            {
                for (size_t i = 0; i < value.GetArraySize(); ++i)
                {
                    CollectNumbers(value.GetArrayElement(i), ElementPath(path, i), fields);
                }
            }
        }
        bool JointPosition(Component::SkinnedMeshComponent& mesh, int32_t joint, M::Vector3& out)
        {
            M::Matrix4x4 matrix;
            if (joint < 0 || !mesh.TryGetJointWorldMatrix(uint32_t(joint), matrix))
            {
                return false;
            }
            out = matrix.GetTranslationRow();
            return true;
        }
        const char* ErrorText(P::PoseModifierConfigError error)
        {
            switch (error)
            {
            case P::PoseModifierConfigError::None: return "なし";
            case P::PoseModifierConfigError::InvalidJson: return "JSON の構文が不正です";
            case P::PoseModifierConfigError::UnsupportedVersion: return "未対応の版です";
            case P::PoseModifierConfigError::MissingKey: return "必要なキーがありません";
            case P::PoseModifierConfigError::UnknownKey: return "未知のキーです";
            case P::PoseModifierConfigError::DuplicateKey: return "キーが重複しています";
            case P::PoseModifierConfigError::TypeMismatch: return "値の型が違います";
            case P::PoseModifierConfigError::OutOfRange: return "値が範囲外です";
            case P::PoseModifierConfigError::UnknownType: return "未知の補正型です";
            case P::PoseModifierConfigError::UnknownJoint: return "関節が見つかりません";
            case P::PoseModifierConfigError::UnknownParam: return "浮動小数のパラメーターが見つかりません";
            case P::PoseModifierConfigError::InvalidCurve: return "重みの曲線が不正です";
            case P::PoseModifierConfigError::NoSkeleton: return "骨格がありません";
            case P::PoseModifierConfigError::Busy: return "補正を適用中です";
            case P::PoseModifierConfigError::FileReadFailed: return "ファイルを読めません";
            default: return "補正を生成できません";
            }
        }
        const char* GroundStatusText(A::EGroundQueryStatus status)
        {
            switch (status)
            {
            case A::EGroundQueryStatus::Hit:
                return "当たり";
            case A::EGroundQueryStatus::NoHit:
                return "外れ";
            case A::EGroundQueryStatus::TooSteep:
                return "急斜面";
            case A::EGroundQueryStatus::NotReady:
                return "準備中";
            case A::EGroundQueryStatus::WrongThread:
                return "呼び出しスレッドが不正";
            default:
                return "取得不可";
            }
        }
    } // namespace

    A::EGroundQueryStatus PoseModifierDebugView::RecordingGroundQuery::Query(
        const M::Vector3& origin, float maxDown, A::GroundHit& out) const
    {
        out = {};
        out.Status = Source ? Source->Query(origin, maxDown, out) : A::EGroundQueryStatus::Unavailable;
        if (Probes.size() < kMaxProbes)
        {
            Probes.push_back({origin, maxDown, out});
        }
        return out.Status;
    }
    void PoseModifierDebugView::RecordingGroundQuery::QueryBatch(C::Span<A::GroundProbe> probes) const
    {
        if (Source)
        {
            Source->QueryBatch(probes);
        }
        for (auto& probe : probes)
        {
            if (!Source)
            {
                probe.Result = {};
                probe.Result.Status = A::EGroundQueryStatus::Unavailable;
            }
            if (Probes.size() < kMaxProbes)
            {
                Probes.push_back(probe);
            }
        }
    }
    PoseModifierDebugView::~PoseModifierDebugView()
    {
        Detach();
    }
    Component::AnimatorComponent* PoseModifierDebugView::FindAnimator() const
    {
        auto* entity = m_World ? m_World->FindEntityByObjectId(m_EntityId) : nullptr;
        auto* animator = entity && !entity->IsPendingDestroy() ? entity->GetComponent<Component::AnimatorComponent>() : nullptr;
        return animator && !animator->IsPendingDestroy() ? animator : nullptr;
    }
    void PoseModifierDebugView::RestoreQueries()
    {
        for (const auto& modifier : m_Modifiers)
        {
            if (auto* foot = A::FindFootIKModifier(modifier.get()))
            {
                foot->SetGroundQuery(m_Ground.Source);
            }
        }
    }
    void PoseModifierDebugView::Attach(World& world, uint64_t id, const C::String& json,
                                       const A::IGroundQuery* ground, bool bEnabled)
    {
        Detach();
        m_World = &world;
        m_EntityId = id;
        m_Ground.Source = ground;
        m_Json = json;
        m_bEnabled = bEnabled;
        if (JsonDocument::TryParse(json, m_Document))
        {
            BuildRows();
            (void)ApplyJson(json, bEnabled);
        }
        else
        {
            m_Report.Error = P::PoseModifierConfigError::InvalidJson;
        }
        NorvesLib::Modules::Gui::RegisterImGuiView(this);
    }
    void PoseModifierDebugView::Detach()
    {
        NorvesLib::Modules::Gui::UnregisterImGuiView(this);
        if (auto* animator = FindAnimator())
        {
            animator->ClearModifiers();
        }
        // 外部に強参照が残っても、終了する場面の問い合わせを借用させない。
        m_Ground.Source = nullptr;
        RestoreQueries();
        m_Modifiers.clear();
        m_Ground.Probes.clear();
        m_World = nullptr;
        m_EntityId = 0;
        m_Json.clear();
        m_Document.Reset();
        m_Rows.clear();
        m_Report = {};
        m_bEnabled = false;
        m_bDraw = true;
    }
    void PoseModifierDebugView::BuildRows()
    {
        m_Rows.clear();
        Component::AnimatorDebugSnapshot snapshot;
        if (auto* animator = FindAnimator())
        {
            animator->BuildDebugSnapshot(snapshot);
        }
        const auto modifiers = m_Document.GetRoot().FindMember("modifiers");
        for (size_t i = 0; i < modifiers.GetArraySize(); ++i)
        {
            const auto config = modifiers.GetArrayElement(i);
            ModifierRow row;
            row.Type = config.FindMember("type").AsString();
            (void)P::TryGetDefaultPoseModifierOrder(Identity(row.Type), row.Order);
            row.Order = int32_t(config.FindMember("order").AsNumber(row.Order));
            row.bEnabled = config.FindMember("enabled").AsBool(true);
            const auto weight = config.FindMember("weight");
            row.Weight = float(weight.AsNumber(1));
            row.Parameter = weight.FindMember("param").AsString();
            for (const auto& parameter : snapshot.Parameters)
            {
                if (!row.Parameter.empty() && parameter.Name == Identity(row.Parameter) && parameter.Type == A::AnimParamType::Float)
                {
                    row.Weight = std::clamp(parameter.Value.Float, 0.f, 1.f);
                }
            }
            CollectNumbers(config.FindMember("settings"), MemberPath(ElementPath("modifiers", i), "settings"), row.Fields);
            m_Rows.push_back(std::move(row));
        }
    }
    bool PoseModifierDebugView::ApplyJson(const C::String& json, bool bInstall)
    {
        auto* animator = FindAnimator();
        auto* entity = m_World ? m_World->FindEntityByObjectId(m_EntityId) : nullptr;
        auto* mesh = entity ? entity->GetComponent<Component::SkinnedMeshComponent>() : nullptr;
        const auto asset = mesh && !mesh->IsPendingDestroy() ? mesh->GetSkeletalAsset() : nullptr;
        const auto skeleton = asset ? asset->GetSkeleton() : nullptr;
        if (!animator || !skeleton)
        {
            m_Report = {P::PoseModifierConfigError::NoSkeleton, {}};
            return false;
        }
        A::AnimParamSet params;
        const auto graph = animator->GetGraph();
        if (graph && graph->GetData() && !params.Initialize(graph->GetData()->Parameters))
        {
            m_Report = {P::PoseModifierConfigError::CreateFailed, {}};
            return false;
        }
        C::VariableArray<C::TSharedPtr<A::IPoseModifier>> candidate;
        JsonDocument document;
        if (!JsonDocument::TryParse(json, document))
        {
            m_Report = {P::PoseModifierConfigError::InvalidJson, {}};
            return false;
        }
        if (!P::ParsePoseModifierConfig(json, *skeleton, graph ? &params : nullptr,
                                        P::GetBuiltinPoseModifierTypes(), candidate, m_Report))
        {
            return false;
        }
        if (bInstall)
        {
            if (!animator->ApplyPoseModifiersJson(json, &m_Report))
            {
                return false;
            }
            // 列挙 API がないため、同じ検証済み設定の群を登録して観測用の強参照を保持する。
            // 同一スレッドの非適用中で、非 null・重複なしの群なので AddModifier は成功する。
            animator->ClearModifiers();
            for (const auto& modifier : candidate)
            {
                if (auto* foot = A::FindFootIKModifier(modifier.get()))
                {
                    foot->SetGroundQuery(&m_Ground);
                }
                if (!animator->AddModifier(modifier))
                {
                    animator->ClearModifiers();
                    for (const auto& previous : m_Modifiers)
                    {
                        (void)animator->AddModifier(previous);
                    }
                    m_Report = {P::PoseModifierConfigError::Busy, {}};
                    return false;
                }
            }
        }
        RestoreQueries();
        m_Modifiers = std::move(candidate);
        m_Json = json;
        m_Document = std::move(document);
        m_Ground.Probes.clear();
        m_Report = {};
        return true;
    }
    bool PoseModifierDebugView::EditNumber(const C::String& path, double value)
    {
        JsonWriter writer(true);
        CopyJson(writer, m_Document.GetRoot(), nullptr, {}, path, value);
        if (!writer.IsComplete())
        {
            m_Report = {P::PoseModifierConfigError::InvalidJson, path};
            return false;
        }
        return ApplyJson(writer.ToString(), m_bEnabled);
    }
    bool PoseModifierDebugView::EnableConfiguredRow(size_t index)
    {
        const auto config = m_Document.GetRoot().FindMember("modifiers").GetArrayElement(index);
        if (config.FindMember("enabled").AsBool(true))
        {
            return true;
        }
        JsonWriter writer(true);
        CopyJson(writer, m_Document.GetRoot(), nullptr, {}, MemberPath(ElementPath("modifiers", index), "enabled"), 1, true);
        return writer.IsComplete() && ApplyJson(writer.ToString(), m_bEnabled);
    }
    void PoseModifierDebugView::UpdateWeight(size_t index)
    {
        auto& row = m_Rows[index];
        if (!row.Parameter.empty())
        {
            if (auto* animator = FindAnimator())
            {
                if (!animator->SetFloat(animator->FindParam(Identity(row.Parameter)), row.bEnabled ? row.Weight : 0))
                {
                    m_Report = {P::PoseModifierConfigError::UnknownParam,
                                MemberPath(ElementPath("modifiers", index), "weight.param")};
                }
            }
        }
        else
        {
            (void)EditNumber(MemberPath(ElementPath("modifiers", index), "weight"), row.bEnabled ? row.Weight : 0);
        }
    }
    void PoseModifierDebugView::OnImGui()
    {
        const bool bVisible = ImGui::Begin("姿勢の後処理");
        if (bVisible && FindAnimator())
        {
            bool bEnabled = m_bEnabled;
            if (ImGui::Checkbox("A: 補正あり / B: 補正なし", &bEnabled))
            {
                if (bEnabled)
                {
                    if (ApplyJson(m_Json, true))
                    {
                        m_bEnabled = true;
                        for (size_t i = 0; i < m_Rows.size(); ++i)
                        {
                            UpdateWeight(i);
                        }
                    }
                }
                else
                {
                    FindAnimator()->ClearModifiers();
                    m_bEnabled = false;
                    RestoreQueries();
                    m_Ground.Probes.clear();
                }
            }
            ImGui::TextUnformatted(m_bEnabled ? "現在: A（補正あり）" : "現在: B（補正なし）");
            ImGui::Checkbox("デバッグ描画", &m_bDraw);
            ImGui::TextUnformatted("地面: 緑=当たり / 赤=外れ・取得不可 / 橙=急斜面");
            ImGui::TextUnformatted("固定点: 水色 / ポールの設定方向・膝: 黄 / 視線: 紫 / ばね先端: 青");
            ImGui::TextUnformatted("ポールは設定方向の参考表示です。実際の解と異なる場合があります。");
            if (ImGui::Button("現在の JSON をコピー"))
            {
                ImGui::SetClipboardText(m_Json.c_str());
            }
            ImGui::TextUnformatted("重みのパラメーター値は実行中の状態です。JSON には設定と結び付けをコピーします。");
            for (size_t i = 0; i < m_Rows.size(); ++i)
            {
                auto& row = m_Rows[i];
                ImGui::PushID(int(i));
                ImGui::Separator();
                const int32_t order = i < m_Modifiers.size() ? m_Modifiers[i]->Order() : row.Order;
                ImGui::Text("%s  順序: %d", row.Type.c_str(), order);
                const bool bPrevious = row.bEnabled;
                bool bChanged = ImGui::Checkbox("有効", &row.bEnabled);
                if (row.bEnabled && !bPrevious && !EnableConfiguredRow(i))
                {
                    row.bEnabled = bPrevious;
                    bChanged = false;
                }
                bChanged = ImGui::SliderFloat("重み", &row.Weight, 0, 1) || bChanged;
                if (bChanged)
                {
                    UpdateWeight(i);
                }
                if (!row.Parameter.empty())
                {
                    ImGui::Text("パラメーター: %s", row.Parameter.c_str());
                }
                if (ImGui::TreeNode("設定の数値"))
                {
                    for (auto& field : row.Fields)
                    {
                        ImGui::DragScalar(field.Path.c_str(), ImGuiDataType_Double, &field.Value, .01f, nullptr, nullptr, "%.6g");
                        if (ImGui::IsItemDeactivatedAfterEdit())
                        {
                            if (EditNumber(field.Path, field.Value))
                            {
                                field.AppliedValue = field.Value;
                            }
                            else
                            {
                                field.Value = field.AppliedValue;
                            }
                        }
                    }
                    ImGui::TreePop();
                }
                if (m_bEnabled && row.bEnabled && i < m_Modifiers.size())
                {
                    if (auto* foot = A::FindFootIKModifier(m_Modifiers[i].get()))
                    {
                        for (size_t leg = 0; leg < foot->GetSettings().Legs.size(); ++leg)
                        {
                            const auto name = foot->GetSettings().Legs[leg].Name.ToString();
                            ImGui::Text("%s: %s / %s", name.c_str(), GroundStatusText(foot->GetLegStatus(leg)),
                                foot->GetLegPlantPhase(leg) == A::EFootPlantPhase::Planted ? "固定中" : "遊脚");
                        }
                    }
                }
                ImGui::PopID();
            }
        }
        else if (bVisible)
        {
            ImGui::TextUnformatted("四足の Animator がありません");
        }
        if (bVisible && m_Report.Error != P::PoseModifierConfigError::None)
        {
            ImGui::Text("適用できません: %s（エラー %u）", ErrorText(m_Report.Error), unsigned(m_Report.Error));
            ImGui::Text("場所: %s", m_Report.Path.empty() ? "ルート" : m_Report.Path.c_str());
            ImGui::TextUnformatted("最後に適用できた設定を保持しています。");
        }
        ImGui::End();
    }
    void PoseModifierDebugView::SubmitDebugDraw()
    {
        auto* animator = FindAnimator();
        auto* entity = animator ? m_World->FindEntityByObjectId(m_EntityId) : nullptr;
        auto* mesh = entity ? entity->GetComponent<Component::SkinnedMeshComponent>() : nullptr;
        if (m_bDraw && mesh && !mesh->IsPendingDestroy())
        {
            auto& draw = GEngine.GetDebugDraw();
            const M::Vector4 green(0, 1, 0, 1), red(1, .1f, .1f, 1), orange(1, .5f, 0, 1);
            const M::Vector4 cyan(0, 1, 1, 1), yellow(1, 1, 0, 1), purple(1, 0, 1, 1), blue(.2f, .5f, 1, 1);
            // 他の view の線を残し、この view の追加は最大 8192 頂点に収める。
            const size_t limit = std::min(draw.GetVertexCount() + 8192,
                                          size_t(Rendering::DebugDrawQueue::kMaxDebugLineVertices));
            auto sphere = [&](const M::Vector3& point, const M::Vector4& color)
            {
                if (draw.GetVertexCount() + 144 <= limit)
                {
                    draw.AddSphere(point, .035f, color);
                }
            };
            auto line = [&](const M::Vector3& from, const M::Vector3& to, const M::Vector4& color)
            {
                if (draw.GetVertexCount() + 2 <= limit)
                {
                    draw.AddLine(from, to, color);
                }
            };
            for (const auto& probe : m_Ground.Probes)
            {
                const auto status = probe.Result.Status;
                const auto& color = status == A::EGroundQueryStatus::Hit ? green :
                                    status == A::EGroundQueryStatus::TooSteep ? orange : red;
                line(probe.OriginAbove, probe.OriginAbove - M::Vector3::UnitY * probe.MaxDown, color);
                if (status == A::EGroundQueryStatus::Hit || status == A::EGroundQueryStatus::TooSteep)
                {
                    sphere(probe.Result.Point, color);
                    line(probe.Result.Point, probe.Result.Point + probe.Result.Normal * .2f, color);
                }
            }
            const auto configs = m_Document.GetRoot().FindMember("modifiers");
            for (size_t i = 0; i < m_Modifiers.size(); ++i)
            {
                if (m_bEnabled && i < m_Rows.size() && m_Rows[i].bEnabled && m_Rows[i].Weight > 0)
                {
                    if (auto* foot = A::FindFootIKModifier(m_Modifiers[i].get()))
                    {
                        const auto& legs = foot->GetSettings().Legs;
                        for (size_t leg = 0; leg < legs.size(); ++leg)
                        {
                            if (foot->GetLegPlantPhase(leg) == A::EFootPlantPhase::Planted)
                            {
                                sphere(foot->GetLegToeTargetWorld(leg), cyan);
                            }
                            M::Vector3 knee;
                            if (JointPosition(*mesh, legs[leg].Joints[1], knee))
                            {
                                sphere(knee, yellow);
                                auto direction = legs[leg].Pole;
                                if (legs[leg].bAutoPole)
                                {
                                    const auto asset = mesh->GetSkeletalAsset();
                                    const auto skeleton = asset ? asset->GetSkeleton() : nullptr;
                                    const int32_t root = legs[leg].Joints[0];
                                    const int32_t parent = skeleton && root >= 0 &&
                                        size_t(root) < skeleton->GetPoseRuntime().Parents.size() ?
                                        skeleton->GetPoseRuntime().Parents[root] : -1;
                                    M::Transform transform;
                                    if (parent >= 0 && mesh->TryGetJointWorldTransform(uint32_t(parent), transform))
                                    {
                                        direction = transform.TransformDirection(legs[leg].PoleInParent);
                                    }
                                    else if (parent == -1)
                                    {
                                        direction = entity->GetWorldTransform().TransformDirection(legs[leg].PoleInParent);
                                    }
                                }
                                else
                                {
                                    direction = entity->GetWorldTransform().TransformDirection(direction);
                                }
                                if (direction.LengthSquared() > 1e-8f)
                                {
                                    line(knee, knee + direction.Normalized() * .25f, yellow);
                                }
                            }
                        }
                    }
                }
                const auto settings = configs.GetArrayElement(i).FindMember("settings");
                if (m_Rows[i].Type == "Look")
                {
                    M::Vector3 target, head;
                    if (animator->TryResolveLookTarget(target))
                    {
                        sphere(target, purple);
                        if (JointPosition(*mesh, mesh->FindJointIndex(Identity(settings.FindMember("head").AsString())), head))
                        {
                            line(head, target, purple);
                        }
                    }
                }
                if (m_Rows[i].Type == "SpringBone")
                {
                    const auto chains = settings.FindMember("chains");
                    for (size_t chain = 0; chain < chains.GetArraySize(); ++chain)
                    {
                        const auto joints = chains.GetArrayElement(chain).FindMember("joints");
                        M::Vector3 tip;
                        if (joints.GetArraySize() && JointPosition(*mesh,
                            mesh->FindJointIndex(Identity(joints.GetArrayElement(joints.GetArraySize() - 1).AsString())), tip))
                        {
                            sphere(tip, blue);
                        }
                    }
                }
            }
        }
        // 描画しない場合も捨て、次の Tick に古い問い合わせを持ち越さない。
        // DebugDrawQueue は RenderingCoordinator が FramePacket へ移した直後に消す。
        m_Ground.Probes.clear();
    }
} // namespace Game::Debug
#endif
