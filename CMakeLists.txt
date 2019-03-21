cmake_minimum_required(VERSION 3.8)
project(observer)
##
## TESTS
## create and configure the unit test target
##
enable_testing()
set_property(GLOBAL PROPERTY USE_FOLDERS ON)
add_subdirectory(observer)
add_subdirectory(test)
