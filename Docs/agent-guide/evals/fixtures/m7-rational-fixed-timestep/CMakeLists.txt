cmake_minimum_required(VERSION 3.23)

project(RationalFixedTimestep LANGUAGES CXX)

enable_testing()

add_executable(RationalFixedTimestepTest
    FixedStepScheduler.cpp
    FixedStepSchedulerTest.cpp
)
target_compile_features(RationalFixedTimestepTest PRIVATE cxx_std_23)

add_test(NAME RationalFixedTimestepTest COMMAND RationalFixedTimestepTest)
