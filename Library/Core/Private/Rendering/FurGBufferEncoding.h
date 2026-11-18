// 毛の GBuffer の P1（Emissive.a）に流れの角を保存し、照明用の接線を復元する参照実装。
// 半精度の Emissive.a の角の分解能は 1/2048 ≒ 0.18 度。
// Common/FurShading.glsl と同じ式に保ち、式を変えるときは両方を同時に直す。
#pragma once

#include "Math/Vector3.h"

namespace NorvesLib::Core::Rendering
{
    /** @brief 流れの角の符号化と復号に使う 2π。 */
    inline constexpr float FurFlowTwoPi = 6.283185307179586f;

    /** @brief 正規化された法線から Duff ら（2017）の式で正規直交基底を作る。 */
    void FurFlowBasis(const Math::Vector3& n, Math::Vector3& outB1, Math::Vector3& outB2);

    /** @brief 正規化された法線の面へ流れを射影し、半精度の P1 に保存する角を返す。 */
    float FurEncodeFlowAngle(const Math::Vector3& n, const Math::Vector3& t);

    /** @brief 書き込みと同じ正規化された法線を使い、P1 の角から照明用の接線を復元する。 */
    Math::Vector3 FurDecodeFlowTangent(const Math::Vector3& n, float p1);
} // namespace NorvesLib::Core::Rendering
