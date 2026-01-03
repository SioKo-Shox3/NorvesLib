#pragma once

#include "Random.h"
#include "Math/Vector2.h"
#include "Math/Vector3.h"
#include <cmath>

namespace NorvesLib::Random
{

// Mathモジュールの型を使用するためのエイリアス
using Vector2 = NorvesLib::Math::Vector2;
using Vector3 = NorvesLib::Math::Vector3;

/**
 * @brief 指定された範囲内のランダムな2Dベクトルを生成
 * 
 * @param minX X座標の最小値
 * @param maxX X座標の最大値
 * @param minY Y座標の最小値
 * @param maxY Y座標の最大値
 * @return 指定範囲内のランダムなVector2
 */
Vector2 GetRandomVector2(float minX, float maxX, float minY, float maxY);

/**
 * @brief 指定された範囲内のランダムな3Dベクトルを生成
 * 
 * @param minX X座標の最小値
 * @param maxX X座標の最大値
 * @param minY Y座標の最小値
 * @param maxY Y座標の最大値
 * @param minZ Z座標の最小値
 * @param maxZ Z座標の最大値
 * @return 指定範囲内のランダムなVector3
 */
Vector3 GetRandomVector3(float minX, float maxX, float minY, float maxY, float minZ, float maxZ);

/**
 * @brief 単位円上のランダムな2Dベクトルを生成
 * 
 * @return 単位円上のランダムなVector2
 */
Vector2 GetRandomUnitVector2();

/**
 * @brief 単位球上のランダムな3Dベクトルを生成
 * 
 * @return 単位球上のランダムなVector3
 */
Vector3 GetRandomUnitVector3();

/**
 * @brief ランダムな角度（ラジアン）を生成
 * 
 * @param min 最小角度（ラジアン）
 * @param max 最大角度（ラジアン）
 * @return ランダムな角度（ラジアン）
 */
float GetRandomAngle(float min = 0.0f, float max = 2.0f * 3.14159265358979323846f);

/**
 * @brief ランダムな要素を配列から選択
 * 
 * @tparam T 配列の要素の型
 * @param array 配列
 * @param size 配列のサイズ
 * @return 配列からランダムに選択した要素
 */
template<typename T>
T& GetRandomElement(T* array, size_t size) 
{
    int32_t index = GetRandomInt(0, static_cast<int32_t>(size - 1));
    return array[index];
}

/**
 * @brief ランダムなチャンス判定（確率に基づく試行）
 * 
 * @param probability 成功確率 (0.0～1.0)
 * @return 成功したかどうか
 */
inline bool RandomChance(float probability = 0.5f) 
{
    return GetRandomBool(probability);
}

} // namespace NorvesLib::Random