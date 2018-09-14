cmake_minimum_required(VERSION 3.8)

##
## PROJECT
## name and version
##
project(csari_observer VERSION 0.1.0 LANGUAGES CXX)
set_property(GLOBAL PROPERTY USE_FOLDERS ON)
##
## OPTIONS
##
## No options yet

##
## CONFIGURATION
##

##
## TARGET
## create target and add include path
##
add_library(${PROJECT_NAME} INTERFACE)
target_compile_features(${PROJECT_NAME} INTERFACE cxx_std_17)
target_include_directories(${PROJECT_NAME} INTERFACE ./include)

##
## TESTS
## create and configure the unit test target
##
include(CTest)
enable_testing()
add_subdirectory(test)