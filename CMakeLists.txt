cmake_minimum_required(VERSION 3.8)
project(observer)
##
## TESTS
## create and configure the unit test target
##
enable_testing()
add_subdirectory(observer)
add_subdirectory(test)
