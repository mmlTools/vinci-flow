cmake_minimum_required(VERSION 3.28)
project(vinciflow_regressions LANGUAGES CXX)
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_MAP_IMPORTED_CONFIG_RELWITHDEBINFO Release)
find_package(libobs REQUIRED)
find_package(Qt6 REQUIRED COMPONENTS Core)
add_executable(browser_lifecycle browser_lifecycle.cpp)
target_include_directories(browser_lifecycle PRIVATE ../src/headers ../src/config)
target_link_libraries(browser_lifecycle PRIVATE OBS::libobs Qt6::Core)
if(MSVC)
  target_compile_options(browser_lifecycle PRIVATE /utf-8)
endif()
enable_testing()
add_test(NAME browser_lifecycle COMMAND browser_lifecycle ${CMAKE_CURRENT_BINARY_DIR}/overlay)
find_program(NODE_EXECUTABLE node REQUIRED)
add_test(
  NAME overlay_runtime
  COMMAND
    ${NODE_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/overlay-runtime.test.cjs ${CMAKE_CURRENT_BINARY_DIR}/overlay/lt.js
)
set_tests_properties(browser_lifecycle PROPERTIES FIXTURES_SETUP generated_overlay)
set_tests_properties(overlay_runtime PROPERTIES FIXTURES_REQUIRED generated_overlay)
