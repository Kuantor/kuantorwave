cmake_minimum_required(VERSION 3.16)
project(kuantorwave C)

set(CMAKE_C_STANDARD 99)
set(CMAKE_C_STANDARD_REQUIRED ON)

# Static core library used by the unit tests. The distributable shared
# library target will be added once the full API surface is implemented.
add_library(kuantorwave_core STATIC
    core/src/detect/kw_detect.c
)
target_include_directories(kuantorwave_core PUBLIC include)
target_compile_definitions(kuantorwave_core PUBLIC KW_STATIC)

if(MSVC)
    target_compile_options(kuantorwave_core PRIVATE /W4 /WX)
    target_compile_definitions(kuantorwave_core PUBLIC _CRT_SECURE_NO_WARNINGS)
else()
    target_compile_options(kuantorwave_core PRIVATE -Wall -Wextra -Werror)
endif()

enable_testing()

add_executable(test_detect tests/test_detect.c)
target_link_libraries(test_detect PRIVATE kuantorwave_core)
if(MSVC)
    target_compile_options(test_detect PRIVATE /W4 /WX)
else()
    target_compile_options(test_detect PRIVATE -Wall -Wextra -Werror)
endif()

add_test(NAME detect COMMAND test_detect)
