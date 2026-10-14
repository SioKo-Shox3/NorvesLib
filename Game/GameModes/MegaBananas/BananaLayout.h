#pragma once

// ---------------------------------------------------------------------------
// バナナの配置（mega-bananas と、mega-stress の混在の負荷が共有する）
//
// 手前に山（塚）を 3 つ積み、その周りに「バナナの海」を敷く。海は中心からの距離で密度を落とす円盤
// （近 50/m²・中 10/m²・遠 2/m²。縁は低周波のノイズでゆらした位置で 0 へ落とす）で、半径は本数から決める
// （密度の形は本数によらず同じで、本数が少なければ同じ形の小さな円盤になる）。
// 円盤を一辺 kSeaTileMeters の区画に分け、中心に近い区画から順に、区画の分の本数を密度に比例した位置へ置く。
// 重なりは、バナナの芯に沿った球の列（BananaShape::SpineSpheres）で隣の区画の分まで調べ、重なる位置では
// 下のバナナに初めて触れる高さへ載せる（密な所は 2 層ほどに積もる）。床全体の格子は持たず、処理中の区画の格子だけを持つ。
// 山は物理を使わず、置いたバナナの形の上に初めて触れる高さへ載せる。配置は本数だけで決まり、実行ごとに同じ
// （予算での区切り方にも依らない。マスクを渡したときは、マスクにも依る）。
// 敷き詰めの位置は、マスク（IBananaFieldMask）が塞いだ所を飛ばし、置けなかった分は次の区画へ回す。
// ---------------------------------------------------------------------------

#include "Core/Public/Asset/CookedMeshFormat.h"
#include "Core/Public/Component/MegaGeometryScatterComponent.h"
#include "Core/Public/Container/Containers.h"
#include "Core/Public/Container/PointerTypes.h"
#include "Core/Public/Math/Vector3.h"

#include <cstdint>

namespace Game::GameModes
{

    /// バナナ（Scripts/GenerateSceneModels.py が作り、Assets/AssetSets/MegaBananasModels.json が焼く）
    inline constexpr const char *kBananaMeshPath = "Assets/Models/Generated/banana/banana.glb";
    inline constexpr const char *kBananaFetchHint = "python Scripts/GenerateSceneModels.py --ids banana";
    /// 生成した GLB の内蔵画像の順（GLB の textures と同じ並び）: 0 = ベースカラー、1 = AO・粗さ・金属（ORM）、2 = 法線
    inline constexpr const char *kBananaAlbedoSuffix = ".img0.png";
    inline constexpr const char *kBananaOrmSuffix = ".img1.png";
    inline constexpr const char *kBananaNormalSuffix = ".img2.png";

    /// 配置に使うバナナの形（クック済みメッシュの頂点から作る）
    struct BananaShape
    {
        /// ローカル X（ローカル Y を長手とするバナナの太さの向き）の最小・最大。床に横たえた高さを求める
        float ThicknessMin = 0.0f;
        float ThicknessMax = 0.0f;
        /// 山の高さを調べるための、頂点を間引いた点（バナナのローカル座標・倍率 1）
        NorvesLib::Core::Container::VariableArray<NorvesLib::Math::Vector3> SamplePoints;
        /// 同じ用途の、より密な点（置く高さの確定と格子への書き込み用）
        NorvesLib::Core::Container::VariableArray<NorvesLib::Math::Vector3> DensePoints;
        /// 長手（ローカル Y）に沿って区切った頂点を包む球の列（中心と半径。倍率 1）。海のバナナどうしの重なりを調べる
        NorvesLib::Core::Container::VariableArray<NorvesLib::Math::Vector3> SpineCenters;
        NorvesLib::Core::Container::VariableArray<float> SpineRadii;
        /// 原点から球の列の最も遠い所までの距離（倍率 1。向きによらない包む半径）
        float SpineBoundRadius = 0.0f;
    };

    class IBananaFieldMask;

    /// 海の密度[本/m²]（近・中・遠の平らな所の値）
    inline constexpr float kSeaDensityNear = 50.0f;
    inline constexpr float kSeaDensityMid = 10.0f;
    inline constexpr float kSeaDensityFar = 2.0f;

    /// 配置の内訳（MEGA_BANANAS_LAYOUT に出す）
    struct BananaLayoutStats
    {
        uint32_t Requested = 0;
        uint32_t Placed = 0;
        uint32_t PileCount = 0;
        /// 海の円盤の中心（床の x・z）と半径[m]（縁のゆらぎの中心の値）
        float CenterX = 0.0f;
        float CenterZ = 0.0f;
        float Radius = 0.0f;
        /// 近・中の境の半径[m]（密度が移り変わる区間の中ほど）
        float NearRadius = 0.0f;
        float MidRadius = 0.0f;
        /// 中心からの距離で数えた本数（山を除く）
        uint32_t NearCount = 0;
        uint32_t MidCount = 0;
        uint32_t FarCount = 0;
        /// 床でなく、ほかのバナナの上に載せた本数と、床に置いたときより持ち上げた高さの最大[m]
        uint32_t StackedCount = 0;
        float MaxLift = 0.0f;
        /// 区画の数と、置けずに残った本数（マスクが塞いだ所が多いときだけ 0 でない）
        uint32_t TileCount = 0;
        uint32_t Deficit = 0;
        /// 生成にかかった時間の合計[ms]と、Step の回数
        double BuildMilliseconds = 0.0;
        uint32_t StepCount = 0;
    };

    /**
     * @brief バナナの配置を、予算で区切って少しずつ作る（区画 1 つの途中でも区切れる）
     *
     * Begin で山を置き、Step を呼ぶたびに予算の時間だけ海の区画を進める。置いた入力は区画の順に連続して並ぶ。
     */
    class BananaFieldBuilder
    {
    public:
        BananaFieldBuilder();
        ~BananaFieldBuilder();
        BananaFieldBuilder(const BananaFieldBuilder &) = delete;
        BananaFieldBuilder &operator=(const BananaFieldBuilder &) = delete;

        /**
         * @brief 配置を始める。山（数百本）はここで置く
         * @param pMask 敷き詰めの位置を塞ぐマスク。nullptr なら塞がない。Step を終えるまで生かしておく
         */
        void Begin(const BananaShape &shape, uint32_t count, const IBananaFieldMask *pMask);

        /**
         * @brief budgetSeconds の間、海の区画を進める（0 以下なら最後まで進める）
         * @return 全部置き終えたら true
         */
        bool Step(double budgetSeconds);

        bool IsDone() const;

        /// 置いた入力（山・区画の順）
        const NorvesLib::Core::Container::VariableArray<NorvesLib::Core::Component::MegaScatterInstance> &GetInstances() const;
        NorvesLib::Core::Container::VariableArray<NorvesLib::Core::Component::MegaScatterInstance> &GetInstances();

        /**
         * @brief 置いた入力を、区画の境で maxPerBatch 本以下の連続した組に分ける（区画 1 つが上限を超えるときはその区画だけの組）
         * @param outBatchStarts 各組の先頭の入力の番号。最後に入力の数を足す（組の数 + 1 個）
         */
        void SplitBatches(uint32_t maxPerBatch, NorvesLib::Core::Container::VariableArray<uint32_t> &outBatchStarts) const;

        const BananaLayoutStats &GetStats() const;

        /// 途中の状態（BananaLayout.cpp の中だけで定義し、使う）
        struct State;

    private:
        NorvesLib::Core::Container::TUniquePtr<State> m_pState;
    };

    /// 敷き詰めの位置を塞ぐマスク（床の x・z で問う）
    class IBananaFieldMask
    {
    public:
        virtual ~IBananaFieldMask() = default;

        /// この位置にバナナを置かないなら true
        virtual bool IsBlocked(float x, float z) const = 0;
    };

    /// count 本を置くときの海の半径[m]（形がそろっていれば BananaFieldBuilder::Begin が決める値と同じ）。配置の前に床を作るのに使う
    float ComputeSeaRadius(uint32_t count);

    /// 遠の密度が縁の手前まで平らに続く所の外の端（半径に対する比。これより外は縁のゆらぎと密度の落ち込みの区間）
    float SeaFullDensityEnd();

    /// 縁がいちばん外側へゆれた所（半径に対する比。これより外にバナナは置かない）
    float SeaOuterEdge();

    /// メッシュの頂点から、配置に使う形を作る
    void BuildBananaShape(const NorvesLib::Core::Asset::CookedMeshData &mesh, BananaShape &outShape);

    /**
     * @brief count 本を、手前に山・その周りに海で散布の入力へ足す（BananaFieldBuilder を予算なしで最後まで回す）
     * @param pMask 敷き詰めの位置を塞ぐマスク。nullptr なら塞がない（mega-bananas の配置）
     * @param outPileCount 山に置いた本数
     * @return 足した本数
     */
    uint32_t PlaceBananas(NorvesLib::Core::Container::VariableArray<NorvesLib::Core::Component::MegaScatterInstance> &instances,
                          const BananaShape &shape, uint32_t count, const IBananaFieldMask *pMask, uint32_t &outPileCount);

} // namespace Game::GameModes
