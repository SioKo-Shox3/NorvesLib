#pragma once

#include <cstdint>
#include <random>
#include <limits>

namespace NorvesLib::Random
{

// 前方宣言
class NormalDistribution;
class ExponentialDistribution;
class PoissonDistribution;

/**
 * 基本的な乱数生成機能を提供するクラス
 */
class Generator
{
public:
    /**
     * デフォルトコンストラクタ - 現在時刻をシードとして初期化
     */
    Generator();

    /**
     * 指定されたシードで初期化
     * 
     * @param seed 乱数生成のシード値
     */
    explicit Generator(uint32_t seed);

    /**
     * シード値を設定
     * 
     * @param seed 新しいシード値
     */
    void SetSeed(uint32_t seed);

    /**
     * 整数の乱数を生成 [min, max]の範囲
     * 
     * @param min 最小値
     * @param max 最大値
     * @return min以上max以下の整数乱数
     */
    int32_t GetInt(int32_t min = 0, int32_t max = std::numeric_limits<int32_t>::max());

    /**
     * 浮動小数点の乱数を生成 [min, max]の範囲
     * 
     * @param min 最小値
     * @param max 最大値
     * @return min以上max以下の浮動小数点乱数
     */
    float GetFloat(float min = 0.0f, float max = 1.0f);

    /**
     * 倍精度浮動小数点の乱数を生成 [min, max]の範囲
     * 
     * @param min 最小値
     * @param max 最大値
     * @return min以上max以下の倍精度浮動小数点乱数
     */
    double GetDouble(double min = 0.0, double max = 1.0);

    /**
     * 確率に基づいて真偽値を返す
     * 
     * @param probability 真になる確率 (0.0～1.0)
     * @return 確率に基づく真偽値
     */
    bool GetBool(float probability = 0.5f);

private:
    std::mt19937 m_engine;
    
    // 分布クラスを友達として追加
    friend class NormalDistribution;
    friend class ExponentialDistribution;
    friend class PoissonDistribution;
};

/**
 * グローバルな乱数生成器を取得
 * 
 * @return グローバルな乱数生成器への参照
 */
Generator& GetGlobalGenerator();

/**
 * グローバル乱数生成器を使用して整数の乱数を生成
 * 
 * @param min 最小値
 * @param max 最大値
 * @return min以上max以下の整数乱数
 */
int32_t GetRandomInt(int32_t min = 0, int32_t max = std::numeric_limits<int32_t>::max());

/**
 * グローバル乱数生成器を使用して浮動小数点の乱数を生成
 * 
 * @param min 最小値
 * @param max 最大値
 * @return min以上max以下の浮動小数点乱数
 */
float GetRandomFloat(float min = 0.0f, float max = 1.0f);

/**
 * グローバル乱数生成器を使用して確率に基づく真偽値を返す
 * 
 * @param probability 真になる確率 (0.0～1.0)
 * @return 確率に基づく真偽値
 */
bool GetRandomBool(float probability = 0.5f);

} // namespace NorvesLib::Random
