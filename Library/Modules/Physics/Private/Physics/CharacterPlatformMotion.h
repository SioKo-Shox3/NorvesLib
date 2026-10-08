#pragma once
#include "Math/Transform.h"
namespace NorvesLib::Modules::Physics
{
    // localはscale1のcollider姿勢。ownerの非一様scaleを逆回転の後に除算する。
    // yawは回転quaternionの方位で、scaleの鏡映を回頭に変えない。TRS逆行列の近似は使わない。
    bool CaptureCharacterAnchor(const Math::Transform& owner, const Math::Transform& local, const Math::Vector3& world,
                                Math::Vector3& anchor, float& yaw);
    bool ResolveCharacterAnchor(const Math::Transform& owner, const Math::Transform& local, const Math::Vector3& anchor,
                                Math::Vector3& world, float& yaw);
} // namespace NorvesLib::Modules::Physics
