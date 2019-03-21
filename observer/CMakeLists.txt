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

## Another project to display csari_observer on project list
add_custom_target(${PROJECT_NAME}_ SOURCES ./include/csari/observer.hpp)
set_target_properties(${PROJECT_NAME}_ PROPERTIES FOLDER Observer PROJECT_LABEL ${PROJECT_NAME})
