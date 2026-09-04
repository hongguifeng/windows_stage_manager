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

function(stage_manager_target_defaults target)
    target_compile_features(${target} PRIVATE cxx_std_20)
    target_include_directories(${target} PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src)
    target_compile_definitions(${target} PRIVATE UNICODE _UNICODE)

    if(MSVC)
        target_compile_options(${target} PRIVATE /W4 /permissive- /WX)
    else()
        target_compile_options(${target} PRIVATE -Wall -Wextra -Wpedantic -Werror)
    endif()
endfunction()

add_library(stage_manager_core STATIC
    src/app/settings.cpp
    src/diagnostics/logger.cpp
    src/window/event_queue.cpp
    src/window/event_coalescer.cpp
    src/window/reconcile_scheduler.cpp
    src/window/internal_move_tracker.cpp
    src/platform/win32/win_event_hook.cpp
)

target_include_directories(stage_manager_core PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/src)
stage_manager_target_defaults(stage_manager_core)

add_executable(stage_manager WIN32
    src/app/main.cpp
    src/app/app_lifecycle.cpp
    src/app/tray_controller.cpp
)

if(WIN32)
    target_sources(stage_manager PRIVATE
        src/app/resources.rc
    )
endif()

stage_manager_target_defaults(stage_manager)
target_link_libraries(stage_manager PRIVATE stage_manager_core)

if(WIN32)
    target_link_libraries(stage_manager_core PUBLIC user32)
    target_link_libraries(stage_manager PRIVATE shell32)
    if(MSVC)
        target_link_options(stage_manager PRIVATE /MANIFEST:NO)
    endif()
endif()

if(STAGE_MANAGER_BUILD_TESTS)
    enable_testing()

    add_executable(stage_manager_smoke_tests
        tests/smoke.cpp
    )

    stage_manager_target_defaults(stage_manager_smoke_tests)

    add_test(NAME stage_manager_smoke COMMAND stage_manager_smoke_tests)

    add_executable(stage_manager_core_tests
        tests/core_tests.cpp
    )

    stage_manager_target_defaults(stage_manager_core_tests)
    target_link_libraries(stage_manager_core_tests PRIVATE stage_manager_core)
    add_test(NAME stage_manager_core COMMAND stage_manager_core_tests)

    add_executable(stage_manager_event_queue_tests
        tests/event_queue_tests.cpp
    )

    stage_manager_target_defaults(stage_manager_event_queue_tests)
    target_link_libraries(stage_manager_event_queue_tests PRIVATE stage_manager_core)
    add_test(NAME stage_manager_event_queue COMMAND stage_manager_event_queue_tests)

    if(WIN32)
        add_executable(stage_manager_tray_tests
            tests/tray_controller_tests.cpp
            src/app/tray_controller.cpp
        )

        stage_manager_target_defaults(stage_manager_tray_tests)
        target_link_libraries(stage_manager_tray_tests PRIVATE stage_manager_core user32 shell32)
        add_test(NAME stage_manager_tray COMMAND stage_manager_tray_tests)

        add_executable(stage_manager_win_event_tests
            tests/win_event_hook_tests.cpp
        )

        stage_manager_target_defaults(stage_manager_win_event_tests)
        target_link_libraries(stage_manager_win_event_tests PRIVATE stage_manager_core user32)
        add_test(NAME stage_manager_win_event COMMAND stage_manager_win_event_tests)
    endif()

    add_executable(stage_manager_event_pipeline_tests
        tests/event_pipeline_tests.cpp
    )

    stage_manager_target_defaults(stage_manager_event_pipeline_tests)
    target_link_libraries(stage_manager_event_pipeline_tests PRIVATE stage_manager_core)
    add_test(NAME stage_manager_event_pipeline COMMAND stage_manager_event_pipeline_tests)
endif()
