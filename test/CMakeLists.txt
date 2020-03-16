set(testcase unit)

add_executable(${testcase} src/${testcase}.cpp)

set_target_properties(${testcase} PROPERTIES CXX_STANDARD 17
                                             CXX_STANDARD_REQUIRED ON)
target_link_libraries(${testcase} PUBLIC csari_observer)                                           
add_test(NAME ${testcase}_test COMMAND ${testcase})
