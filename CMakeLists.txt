cmake_minimum_required(VERSION 3.5)

project(ObservableTree LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

include_directories(include .. D:/local/boost_1_72_0)
add_executable(ObservableTree
    test/test.cpp
    ../json11/json11.cpp
    )
