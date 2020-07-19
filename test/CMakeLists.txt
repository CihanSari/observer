set(testcase unit)

add_executable(${testcase} src/${testcase}.cpp src/catch.hpp)

if(NOT WIN32)
  set(CMAKE_THREAD_PREFER_PTHREAD TRUE)
  set(THREADS_PREFER_PTHREAD_FLAG TRUE)
  find_package(Threads REQUIRED)
  target_link_libraries(${testcase} PUBLIC Threads::Threads)
endif()

set_target_properties(${testcase} PROPERTIES CXX_STANDARD 17
                                             CXX_STANDARD_REQUIRED ON)
target_link_libraries(${testcase} PUBLIC csari_observer)
target_include_directories(${testcase} PRIVATE src)