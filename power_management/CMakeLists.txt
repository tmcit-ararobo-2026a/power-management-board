cmake_minimum_required(VERSION 3.22)

add_library(power_management STATIC
    src/app.cpp
)

target_include_directories(power_management PUBLIC
    include
)
target_compile_features(power_management PUBLIC cxx_std_17)