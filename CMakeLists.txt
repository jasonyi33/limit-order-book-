cmake_minimum_required(VERSION 3.16)

project(limit_order_book LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

add_library(
    lob_core
    src/Benchmark.cpp
    src/CsvParser.cpp
    src/OrderBook.cpp
    src/SyntheticOrderGenerator.cpp
)

target_include_directories(lob_core PUBLIC include)
target_compile_options(lob_core PRIVATE -Wall -Wextra -Wpedantic)

add_executable(lob src/main.cpp)
target_link_libraries(lob PRIVATE lob_core)
target_compile_options(lob PRIVATE -Wall -Wextra -Wpedantic)

add_executable(generator src/generator_main.cpp)
target_link_libraries(generator PRIVATE lob_core)
target_compile_options(generator PRIVATE -Wall -Wextra -Wpedantic)

add_executable(lob_tests tests/OrderBookTests.cpp)
target_link_libraries(lob_tests PRIVATE lob_core)
target_compile_options(lob_tests PRIVATE -Wall -Wextra -Wpedantic)

enable_testing()
add_test(NAME lob_tests COMMAND lob_tests)
set_tests_properties(lob_tests PROPERTIES WORKING_DIRECTORY ${CMAKE_SOURCE_DIR})
