# Windowless tests: core logic only (no GLFW, no ImGui).
FetchContent_Declare(catch2
    GIT_REPOSITORY https://github.com/catchorg/Catch2.git
    GIT_TAG v3.7.1
    GIT_SHALLOW TRUE)
FetchContent_MakeAvailable(catch2)

add_executable(rtr-bench-tests
    theme_test.cpp
    scope_protocol_test.cpp
    event_queue_test.cpp
    port_state_test.cpp
    scope_engine_test.cpp
    math_formula_test.cpp
    circuit_test.cpp
    ${CMAKE_SOURCE_DIR}/src/ui/theme.cpp)
target_include_directories(rtr-bench-tests PRIVATE ${CMAKE_SOURCE_DIR}/src)
target_link_libraries(rtr-bench-tests PRIVATE rtr-core Catch2::Catch2WithMain)
if(NOT (MSVC AND NOT CMAKE_CXX_COMPILER_ID MATCHES "Clang"))
    target_compile_options(rtr-bench-tests PRIVATE -Wall -Wextra -Werror -Wconversion -Wshadow)
endif()

add_test(NAME rtr-bench-tests COMMAND rtr-bench-tests)
