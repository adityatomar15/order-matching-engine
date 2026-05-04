cmake_minimum_required(VERSION 3.14)
project(OrderMatchingEngine VERSION 1.0.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_FLAGS_RELEASE "-O3 -march=native -Wall -Wextra")

add_executable(engine src/main.cpp)