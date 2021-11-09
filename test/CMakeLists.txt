project(UnitTests)

add_executable(${PROJECT_NAME} src/unit.cpp src/pipe.cpp src/catch.hpp)

if(NOT WIN32)
  set(CMAKE_THREAD_PREFER_PTHREAD TRUE)
  set(THREADS_PREFER_PTHREAD_FLAG TRUE)
  find_package(Threads REQUIRED)
  target_link_libraries(${PROJECT_NAME} PUBLIC Threads::Threads)
endif()

set_target_properties(${PROJECT_NAME} PROPERTIES CXX_STANDARD 17
                                                 CXX_STANDARD_REQUIRED ON)
target_link_libraries(${PROJECT_NAME} PUBLIC csari_observer)
target_include_directories(${PROJECT_NAME} PRIVATE src)
