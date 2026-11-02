#pragma once

/**
 * @file DefeatBloodPresenter.h
 * @brief 撃破の血の時間割をワールドの粒とデカールへ反映する
 */

#include "Particle/DecalPool.h"
#include "Particle/DefeatBloodSequence.h"
#include "Particle/VfxEffectSystem.h"

namespace NorvesLib::Core::Particle
{
    struct DefeatBloodPresenterDesc
    {
        DefeatBloodTimeline Timeline;
        float DecalColor[3] = {0.16f, 0.005f, 0.005f}; // 線形の暗い赤
        float DecalDepthMeters = 0.25f;                // 箱の局所 Z の半分の大きさ[m]
        float MinDecalRadiusMeters = 0.02f;            // 滲みの始まりの半径の下限[m]
        uint32_t MaxActive = 16u;                      // 同時に進める撃破の数
    };

    /// 単一のゲームスレッドから使う。参照先の VFX とデカールの置き場は、この型より長く生存させる。
    class DefeatBloodPresenter
    {
    public:
        static constexpr const char* SprayEventName = "DefeatBlood";

        DefeatBloodPresenter(VfxEffectSystem& vfx, DecalPool& decals, const DefeatBloodPresenterDesc& desc = {});
        /// 自分で作った VFX のインスタンスと生存中のデカールを片付ける
        ~DefeatBloodPresenter();
        DefeatBloodPresenter(const DefeatBloodPresenter&) = delete;
        DefeatBloodPresenter& operator=(const DefeatBloodPresenter&) = delete;
        DefeatBloodPresenter(DefeatBloodPresenter&&) = delete;
        DefeatBloodPresenter& operator=(DefeatBloodPresenter&&) = delete;

        /// 撃破の合図。時間割・位置・法線・描画値が不正、または MaxActive に達していれば false
        bool Trigger(const Math::Vector3& position, const Math::Vector3& normal);
        /// 呼び手は毎フレーム Tick(dt) → vfx.Tick(dt) → decals.Tick(dt) の順に同じ dt で呼ぶ
        void Tick(float deltaSeconds);
        uint32_t GetActiveCount() const;
        /// ワールド空間で SpawnOnEvent(EventName=DefeatBlood) により赤い半透明の粒を出す既定のエフェクト
        static VfxEffectAsset MakeDefaultSprayEffect();

    private:
        struct ActiveSlot
        {
            DefeatBloodSequence Sequence;
            DecalHandle Decal;
            double StartVfxTime = 0.0;
            bool bActive = false;
        };

        void WarnOnce(const char* message);

        VfxEffectSystem& m_Vfx;
        DecalPool& m_Decals;
        DefeatBloodPresenterDesc m_Desc;
        Container::VariableArray<ActiveSlot> m_Slots;
        Container::VariableArray<DefeatBloodCue> m_Cues;
        VfxEffectHandle m_SprayInstance;
        uint32_t m_ActiveCount = 0u;
        bool m_bValidDesc = false;
        bool m_bWarned = false;
    };
} // namespace NorvesLib::Core::Particle
