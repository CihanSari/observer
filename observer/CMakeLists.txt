cmake_minimum_required(VERSION 3.8)
##
## PROJECT
## name and version
##
project(csari_observer VERSION 1.0.1 LANGUAGES CXX)
##
## OPTIONS
##
## No options yet

##
## TARGET
## create target and add include path
##
add_library(${PROJECT_NAME} INTERFACE)
target_compile_features(${PROJECT_NAME} INTERFACE cxx_std_17)
target_include_directories(${PROJECT_NAME} INTERFACE ./include)