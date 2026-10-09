#pragma once

#include "Math/Vector3.h"

#include <cstddef>

namespace NorvesLib::Core::Input
{

    /**
     * @brief カメラパスの 1 つのキー（その時刻のカメラの位置と注視点）
     */
    struct CameraPathKey
    {
        /// 時刻（秒）
        float Time;
        /// カメラの位置[m]
        float Position[3];
        /// 注視点[m]。カメラはこの点を向く
        float Target[3];
    };

    /**
     * @brief 位置と注視点のキーを滑らかにつなぐカメラパス
     *
     * キー列は呼び出し側が持つ静的な配列（所有しない）。位置と注視点をそれぞれ、キーの時刻に合わせた
     * Catmull-Rom 形式のエルミート補間でつなぐ。隣り合うキーの間隔が違っても速度がつながるよう、
     * 各キーの接線は両隣のキーの差を時刻の差で割って求める。
     * - 一周する(bLoop): 最後のキーから最初のキーへ戻る区間を持つ。最初のキーの時刻は 0、周期は最後のキーの時刻より大きい
     * - 一周しない: 最初のキーより前は最初のキー、最後のキーより後は最後のキーに留まる。端の接線は片側の差分
     */
    class CameraPath
    {
    public:
        /**
         * @brief キー列を設定する
         * @param keys 時刻が昇順のキー（所有しない。呼ぶ側が寿命を保つ）
         * @param count キーの数（2 以上）
         * @param bLoop 一周して繰り返すか
         * @param loopSeconds bLoop のときの周期（秒）。最後のキーの時刻より大きい値。bLoop でなければ無視
         * @return 設定できたら true。不正なキー列（数が足りない・時刻が昇順でない・周期が足りない）は false で何も変えない
         */
        bool Set(const CameraPathKey *keys, std::size_t count, bool bLoop, float loopSeconds);

        /// キー列を外す
        void Clear();

        bool IsValid() const
        {
            return m_Keys != nullptr;
        }

        bool IsLoop() const
        {
            return m_bLoop;
        }

        /// パスの長さ（秒）。一周するパスは周期、そうでなければ最後のキーの時刻。無効なパスは 0
        float GetDuration() const
        {
            return m_Duration;
        }

        /// 再生時刻を有効な範囲へ収める。一周するパスは [0, 周期) へ巻き戻し、そうでなければ [0, 長さ] に収める
        float WrapTime(float time) const;

        /**
         * @brief ある時刻の位置と注視点を求める
         * @param time 時刻（秒）。WrapTime と同じ規則で範囲へ収めてから評価する
         * @return 評価できたら true。無効なパスは false
         */
        bool Evaluate(float time, Math::Vector3 &outPosition, Math::Vector3 &outTarget) const;

        /**
         * @brief 位置から注視点を向く Yaw/Pitch（度）を求める。FreeFlyCameraController の角度の約束と同じ
         * @return 位置と注視点がほぼ重なっていて向きが決まらないときは false（出力は変えない）
         */
        static bool LookAngles(const Math::Vector3 &position, const Math::Vector3 &target, float &outYawDegrees,
                               float &outPitchDegrees);

    private:
        /// 仮想の添字 index（範囲外は一周なら巻き戻し、そうでなければ端）のキーの時刻・位置・注視点
        void KeyAt(long index, float &outTime, Math::Vector3 &outPosition, Math::Vector3 &outTarget) const;

        const CameraPathKey *m_Keys = nullptr;
        std::size_t m_Count = 0;
        bool m_bLoop = false;
        float m_Duration = 0.0f;
    };

} // namespace NorvesLib::Core::Input
