cmake_minimum_required(VERSION 3.23)

project(ParticleEmitterIdentity LANGUAGES CXX)

enable_testing()

add_executable(ParticleEmitterIdentityTest
    ParticleEmitterIdentity.cpp
    ParticleEmitterIdentityTest.cpp
)
target_compile_features(ParticleEmitterIdentityTest PRIVATE cxx_std_23)

add_test(NAME ParticleEmitterIdentityTest COMMAND ParticleEmitterIdentityTest)
