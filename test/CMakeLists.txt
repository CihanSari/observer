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
set(gtest_disable_pthreads OFF CACHE BOOL "" FORCE)

# Add googletest directly to our build. This adds
# the following targets: gtest, gtest_main, gmock
# and gmock_main
add_subdirectory("${CMAKE_BINARY_DIR}/googletest-src"
                 "${CMAKE_BINARY_DIR}/googletest-build")

# Now simply link against gtest or gtest_main as needed. Eg
add_executable(${testcase} src/${testcase}.cpp)

target_link_libraries(${testcase} gtest_main csari_observer)
set_target_properties(${testcase} PROPERTIES CXX_STANDARD 17
                                             CXX_STANDARD_REQUIRED ON)
add_test(NAME ${testcase}_test COMMAND ${testcase})

# Set up project folders
set_target_properties(gmock gmock_main gtest gtest_main PROPERTIES FOLDER 3rdParty)
set_target_properties(${testcase} PROPERTIES FOLDER Tests)
