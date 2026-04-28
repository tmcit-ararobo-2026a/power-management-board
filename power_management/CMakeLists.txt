cmake_minimum_required(VERSION 3.22)

add_library(power_management STATIC
    src/app.cpp
)

target_include_directories(power_management PUBLIC
    include
)

target_compile_features(power_management PUBLIC cxx_std_17)

target_link_libraries(power_management PRIVATE gn10_can)
target_link_libraries(power_management PRIVATE stm32cubemx)