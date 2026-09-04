cmake_minimum_required(VERSION 3.25)

project(WindowsStageManager
    VERSION 0.1.0
    DESCRIPTION "Windows 11 freeform window visibility manager"
    LANGUAGES CXX
)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

option(STAGE_MANAGER_BUILD_TESTS "Build unit and smoke tests" ON)

add_executable(stage_manager WIN32
    src/app/main.cpp
)

if(WIN32)
    target_sources(stage_manager PRIVATE
        src/app/resources.rc
    )
endif()

target_compile_features(stage_manager PRIVATE cxx_std_20)

if(MSVC)
    target_compile_options(stage_manager PRIVATE /W4 /permissive- /WX)
else()
    target_compile_options(stage_manager PRIVATE -Wall -Wextra -Wpedantic -Werror)
endif()

if(WIN32)
    target_link_libraries(stage_manager PRIVATE user32)
endif()

if(STAGE_MANAGER_BUILD_TESTS)
    enable_testing()

    add_executable(stage_manager_smoke_tests
        tests/smoke.cpp
    )

    target_compile_features(stage_manager_smoke_tests PRIVATE cxx_std_20)

    if(MSVC)
        target_compile_options(stage_manager_smoke_tests PRIVATE /W4 /permissive- /WX)
    else()
        target_compile_options(stage_manager_smoke_tests PRIVATE -Wall -Wextra -Wpedantic -Werror)
    endif()

    add_test(NAME stage_manager_smoke COMMAND stage_manager_smoke_tests)
endif()
