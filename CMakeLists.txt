cmake_minimum_required(VERSION 3.5)
project(network VERSION 0.1.0 LANGUAGES C CXX)

add_executable(ping task1.cpp)
add_executable(tracert task2.cpp)
add_executable(scanner task3.cpp)


