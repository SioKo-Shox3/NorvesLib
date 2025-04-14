#include "RandomDistributions.h"

namespace NorvesLib::Random
{

// NormalDistribution実装
NormalDistribution::NormalDistribution(float mean, float standardDeviation)
    : m_mean(mean)
    , m_standardDeviation(standardDeviation)
    , m_generator(GetGlobalGenerator())
{
}

float NormalDistribution::GetValue()
{
    std::normal_distribution<float> distribution(m_mean, m_standardDeviation);
    return distribution(m_generator.m_engine);
}

void NormalDistribution::SetMean(float mean)
{
    m_mean = mean;
}

void NormalDistribution::SetStandardDeviation(float standardDeviation)
{
    m_standardDeviation = standardDeviation;
}

void NormalDistribution::SetParameters(float mean, float standardDeviation)
{
    m_mean = mean;
    m_standardDeviation = standardDeviation;
}

// ExponentialDistribution実装
ExponentialDistribution::ExponentialDistribution(float lambda)
    : m_lambda(lambda)
    , m_generator(GetGlobalGenerator())
{
}

float ExponentialDistribution::GetValue()
{
    std::exponential_distribution<float> distribution(m_lambda);
    return distribution(m_generator.m_engine);
}

void ExponentialDistribution::SetLambda(float lambda)
{
    m_lambda = lambda;
}

// PoissonDistribution実装
PoissonDistribution::PoissonDistribution(float mean)
    : m_mean(mean)
    , m_generator(GetGlobalGenerator())
{
}

int PoissonDistribution::GetValue()
{
    std::poisson_distribution<int> distribution(m_mean);
    return distribution(m_generator.m_engine);
}

void PoissonDistribution::SetMean(float mean)
{
    m_mean = mean;
}

// グローバルユーティリティ関数
float GetRandomNormal(float mean, float standardDeviation)
{
    NormalDistribution distribution(mean, standardDeviation);
    return distribution.GetValue();
}

float GetRandomExponential(float lambda)
{
    ExponentialDistribution distribution(lambda);
    return distribution.GetValue();
}

int GetRandomPoisson(float mean)
{
    PoissonDistribution distribution(mean);
    return distribution.GetValue();
}

} // namespace NorvesLib::Random