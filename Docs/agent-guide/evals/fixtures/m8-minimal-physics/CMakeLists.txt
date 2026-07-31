cmake_minimum_required(VERSION 3.23)

project(M8MinimalPhysics LANGUAGES CXX)

enable_testing()

add_executable(M8MinimalPhysicsTest
    PhysicsSceneQueryFacadeTest.cpp
)
target_compile_features(M8MinimalPhysicsTest PRIVATE cxx_std_23)

add_test(NAME M8MinimalPhysicsTest COMMAND M8MinimalPhysicsTest)
