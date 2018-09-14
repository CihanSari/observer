set(testcase unit)

# Download and unpack googletest at configure time
set(GOOGLETEST_DIR ${CMAKE_BINARY_DIR}/googletest-download)
message(STATUS ${CMAKE_BINARY_DIR}/googletest-download)
configure_file(CMakeLists.txt.in ${GOOGLETEST_DIR}/CMakeLists.txt)
execute_process(COMMAND "${CMAKE_COMMAND}" -G "${CMAKE_GENERATOR}" .
    WORKING_DIRECTORY "${CMAKE_BINARY_DIR}/googletest-download" )
execute_process(COMMAND "${CMAKE_COMMAND}" --build .
    WORKING_DIRECTORY "${CMAKE_BINARY_DIR}/googletest-download" )

# Prevent GoogleTest from overriding our compiler/linker options
# when building with Visual Studio
set(gtest_force_shared_crt ON CACHE BOOL "" FORCE)
set(gtest_disable_pthreads ON CACHE BOOL "" FORCE)

# Add googletest directly to our build. This adds
# the following targets: gtest, gtest_main, gmock
# and gmock_main
add_subdirectory("${CMAKE_BINARY_DIR}/googletest-src"
                 "${CMAKE_BINARY_DIR}/googletest-build")

# The gtest/gmock targets carry header search path
# dependencies automatically when using CMake 2.8.11 or
# later. Otherwise we have to add them here ourselves.
if(CMAKE_VERSION VERSION_LESS 2.8.11)
include_directories("${gtest_SOURCE_DIR}/include"
                    "${gmock_SOURCE_DIR}/include")
endif()


# Add simplecallback include
include_directories(${testcase} ${CSARI_SIMPLECALLBACK_INCLUDE_BUILD_DIR})

# Now simply link against gtest or gtest_main as needed. Eg
add_executable(${testcase} src/${testcase}.cpp)
target_link_libraries(${testcase} gtest_main csari_observer)
add_test(NAME ${testcase}_test COMMAND ${testcase})