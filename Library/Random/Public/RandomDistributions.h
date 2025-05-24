#pragma once

#include "Random.h"
#include <random>

namespace NorvesLib::Random
{

/**
 * @brief 正規分布（ガウス分布）による乱数生成
 */
class NormalDistribution
{
public:
    /**
     * @brief コンストラクタ
     * @param mean 平均値
     * @param standardDeviation 標準偏差
     */
    NormalDistribution(float mean = 0.0f, float standardDeviation = 1.0f);

    /**
     * @brief 分布からの値を取得
     * @return 正規分布に基づく乱数
     */
    float GetValue();

    /**
     * @brief 平均値を設定
     * @param mean 新しい平均値
     */
    void SetMean(float mean);

    /**
     * @brief 標準偏差を設定
     * @param standardDeviation 新しい標準偏差
     */
    void SetStandardDeviation(float standardDeviation);

    /**
     * @brief パラメータを設定
     * @param mean 平均値
     * @param standardDeviation 標準偏差
     */
    void SetParameters(float mean, float standardDeviation);

private:
    float m_mean;
    float m_standardDeviation;
    Generator& m_generator;
};

/**
 * @brief 指数分布による乱数生成
 */
class ExponentialDistribution
{
public:
    /**
     * @brief コンストラクタ
     * @param lambda 確率密度関数のパラメータλ（レート）
     */
    ExponentialDistribution(float lambda = 1.0f);

    /**
     * @brief 分布からの値を取得
     * @return 指数分布に基づく乱数
     */
    float GetValue();

    /**
     * @brief λ（レート）を設定
     * @param lambda 新しいレート値
     */
    void SetLambda(float lambda);

private:
    float m_lambda;
    Generator& m_generator;
};

/**
 * @brief ポアソン分布による乱数生成
 */
class PoissonDistribution
{
public:
    /**
     * @brief コンストラクタ
     * @param mean イベントの平均発生回数
     */
    PoissonDistribution(float mean = 1.0f);

    /**
     * @brief 分布からの値を取得
     * @return ポアソン分布に基づく乱数（整数）
     */
    int GetValue();

    /**
     * @brief 平均値を設定
     * @param mean 新しい平均値
     */
    void SetMean(float mean);

private:
    float m_mean;
    Generator& m_generator;
};

// グローバル関数 - 特定の分布に従う乱数を簡単に取得するためのユーティリティ

/**
 * @brief 正規分布（ガウス分布）に従う乱数を取得
 * @param mean 平均値
 * @param standardDeviation 標準偏差
 * @return 正規分布に基づく乱数
 */
float GetRandomNormal(float mean = 0.0f, float standardDeviation = 1.0f);

/**
 * @brief 指数分布に従う乱数を取得
 * @param lambda 確率密度関数のパラメータλ（レート）
 * @return 指数分布に基づく乱数
 */
float GetRandomExponential(float lambda = 1.0f);

/**
 * @brief ポアソン分布に従う乱数を取得
 * @param mean イベントの平均発生回数
 * @return ポアソン分布に基づく乱数（整数）
 */
int GetRandomPoisson(float mean = 1.0f);

} // namespace NorvesLib::Random