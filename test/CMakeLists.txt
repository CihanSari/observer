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

set_property(GLOBAL PROPERTY USE_FOLDERS ON)

# Now simply link against gtest or gtest_main as needed. Eg
add_executable(${testcase} 
src/${testcase}.cpp
../observer/include/csari/observer.hpp)
target_link_libraries(${testcase} gtest_main csari_observer)
add_test(NAME ${testcase}_test COMMAND ${testcase})