#pragma once

// ---------------------------------------------------------------------------
// 地形の高さ場（seed と関数だけで決まる、手続き生成の高さ）
//
// クッカー（AssetCook の --generate terrain-tile:<x>,<z>）が地形のメッシュを焼くときと、Game が任意の (x,z) の
// 高さ・法線を引くとき（岩や建物を地面に据える・草を撒く）が、同じ関数を通る。どちらかだけ値を変えると、
// 焼いた地形と Game が引く高さが食い違う。
//
// 座標は m、+Y が上。高さ 0 が中央の盆地の床。形は中央の平らな盆地、その周りの砂丘となだらかな起伏、
// 輪の上に並ぶメサ（平らな頂と切り立った崖の台地）、外周の稜線の 4 つを重ねて作る。
// ---------------------------------------------------------------------------

#include <cstdint>

namespace NorvesLib::Core::Rendering::Terrain
{

    /// 高さ場の形を決める値。同じ値なら、どの実行・どのプロセスでも同じ高さになる
    struct TerrainHeightFieldParams
    {
        /// 乱数の種（メサの配置・ノイズの位相・砂丘の向き）
        uint32_t Seed = 1u;
        /// 中央の平らな盆地の半径[m]（この内側は高さ 0）
        float BasinRadius = 100.0f;
        /// 盆地の縁から周りの起伏へ移る幅[m]
        float BasinBlend = 90.0f;
        /// 盆地の縁から稜線の麓まで、外へ向かってゆるく上がる高さ[m]（盆地をくぼみにする）
        float BowlHeight = 14.0f;
        /// なだらかな起伏（数百 m の波長）の振幅[m]
        float RollingAmplitude = 10.0f;
        /// 砂丘の波長[m]と振幅[m]
        float DuneWavelength = 70.0f;
        float DuneAmplitude = 5.0f;
        /// メサを並べる輪の内側・外側の半径[m]と、メサの数
        float MesaRingInner = 330.0f;
        float MesaRingOuter = 680.0f;
        uint32_t MesaCount = 7u;
        /// 外周の稜線が立ち上がり始める距離・頂に達する距離[m]（中心から。角は正方形に寄せて測る）
        float RidgeStart = 700.0f;
        float RidgeCrest = 920.0f;
        /// 稜線の高さ[m]（基準の高さと、尾根の凹凸の振幅）
        float RidgeHeight = 110.0f;
        float RidgeRoughness = 70.0f;
    };

    /// 高さ場の法線（単位ベクトル、+Y が上）
    struct TerrainNormal
    {
        float X = 0.0f;
        float Y = 1.0f;
        float Z = 0.0f;
    };

    /// 高さ場。作った後は読み取りだけで、複数のスレッドから同時に引いてよい
    class TerrainHeightField
    {
    public:
        static constexpr uint32_t kMaxMesas = 16u;

        explicit TerrainHeightField(const TerrainHeightFieldParams &params);

        /// (x,z) の高さ[m]
        float SampleHeight(float x, float z) const;

        /// (x,z) の法線。高さの中心差分（間隔 sampleSpacing[m]）から求める
        TerrainNormal SampleNormal(float x, float z, float sampleSpacing = 0.5f) const;

        const TerrainHeightFieldParams &GetParams() const
        {
            return m_Params;
        }

    private:
        struct Mesa
        {
            float CenterX = 0.0f;
            float CenterZ = 0.0f;
            /// 頂の半径[m]と、楕円の縦横比・向き[rad]
            float Radius = 0.0f;
            float Aspect = 1.0f;
            float Angle = 0.0f;
            /// 頂の高さ[m]（周りの地面からの高さ）
            float Height = 0.0f;
        };

        float SampleMesas(float x, float z) const;

        TerrainHeightFieldParams m_Params;
        Mesa m_Mesas[kMaxMesas];
        uint32_t m_MesaCount = 0;
        /// 砂丘の進む向き（単位ベクトル）
        float m_DuneDirX = 1.0f;
        float m_DuneDirZ = 0.0f;
    };

    /// 荒野のシーン（wasteland）の地形の値。クッカーと Game が同じ値を使う
    namespace WastelandTerrain
    {
        /// 地形の一辺の半分[m]（原点が中心の 2 km 四方）
        inline constexpr float kHalfExtent = 1000.0f;
        /// メッシュの格子の間隔[m]
        inline constexpr float kGridSpacing = 1.0f;
        /// 区画の一辺の格子の数（区画ごとに 1 つのメッシュに焼く）
        inline constexpr uint32_t kTileCells = 500u;
        /// 区画の並び（一辺あたり）。kTileCells × kTilesPerSide × kGridSpacing = 2 × kHalfExtent
        inline constexpr uint32_t kTilesPerSide = 4u;
        /// テクスチャの 1 枚が覆う広さ[m]（UV はワールドの XZ をこの値で割る）
        inline constexpr float kTextureTileMeters = 4.0f;
        /// 乱数の種
        inline constexpr uint32_t kSeed = 20261014u;

        /// 区画 (tileX, tileZ) のクック済みメッシュの論理パスの接頭辞。後ろに "<tileX>_<tileZ>.generated" を付ける
        inline constexpr const char *kCookedMeshLogicalPathPrefix = "Assets/Models/WastelandTerrain/WastelandTerrain_";

        /// 荒野の地形の高さ場の値
        TerrainHeightFieldParams MakeParams();
    } // namespace WastelandTerrain

} // namespace NorvesLib::Core::Rendering::Terrain
