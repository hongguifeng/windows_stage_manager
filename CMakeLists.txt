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
    target_compile_definitions(${target} PRIVATE UNICODE _UNICODE NOMINMAX)

    if(MSVC)
        target_compile_options(${target} PRIVATE /W4 /permissive- /WX)
    else()
        target_compile_options(${target} PRIVATE -Wall -Wextra -Wpedantic -Werror)
    endif()
endfunction()

add_library(stage_manager_core STATIC
    src/app/settings.cpp
    src/diagnostics/logger.cpp
    src/diagnostics/runtime_metrics.cpp
    src/geometry/rect.cpp
    src/geometry/region.cpp
    src/geometry/dpi.cpp
    src/geometry/work_area.cpp
    src/geometry/interaction_zones.cpp
    src/geometry/hit_testing.cpp
    src/solver/visibility_analyzer.cpp
    src/solver/candidate_generator.cpp
    src/solver/candidate_ranker.cpp
    src/solver/layout_hash.cpp
    src/solver/layout_solver.cpp
    src/solver/z_order_solver.cpp
    src/window/event_queue.cpp
    src/window/event_coalescer.cpp
    src/window/coordinator_health.cpp
    src/window/reconcile_scheduler.cpp
    src/window/internal_move_tracker.cpp
    src/window/move_applier.cpp
    src/window/move_transaction.cpp
    src/window/mvp_coordinator.cpp
    src/window/tracking_window_provider.cpp
    src/window/window_identity.cpp
    src/window/window_classifier.cpp
    src/window/snapshot_report.cpp
    src/platform/win32/window_provider.cpp
    src/platform/win32/window_mover.cpp
    src/platform/win32/hit_tester.cpp
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
    target_link_libraries(stage_manager_core PUBLIC user32 dwmapi ole32)
    target_link_libraries(stage_manager PRIVATE shell32)
    if(MSVC)
        target_link_options(stage_manager PRIVATE /MANIFEST:NO)
    endif()
endif()

if(WIN32)
    add_executable(window_inspector
        src/tools/window_inspector.cpp
    )

    stage_manager_target_defaults(window_inspector)
    target_link_libraries(window_inspector PRIVATE stage_manager_core)
endif()

add_executable(scenario_runner
    src/tools/scenario_runner.cpp
)

stage_manager_target_defaults(scenario_runner)
target_link_libraries(scenario_runner PRIVATE stage_manager_core)

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

    add_executable(stage_manager_runtime_metrics_tests
        tests/runtime_metrics_tests.cpp
    )

    stage_manager_target_defaults(stage_manager_runtime_metrics_tests)
    target_link_libraries(stage_manager_runtime_metrics_tests PRIVATE stage_manager_core)
    add_test(NAME stage_manager_runtime_metrics COMMAND stage_manager_runtime_metrics_tests)

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

        add_executable(stage_manager_window_provider_tests
            tests/window_provider_tests.cpp
        )

        stage_manager_target_defaults(stage_manager_window_provider_tests)
        target_link_libraries(stage_manager_window_provider_tests PRIVATE stage_manager_core user32)
        add_test(NAME stage_manager_window_provider COMMAND stage_manager_window_provider_tests)

        add_executable(stage_manager_identity_classifier_tests
            tests/identity_classifier_tests.cpp
        )

        stage_manager_target_defaults(stage_manager_identity_classifier_tests)
        target_link_libraries(stage_manager_identity_classifier_tests PRIVATE stage_manager_core user32)
        add_test(NAME stage_manager_identity_classifier COMMAND stage_manager_identity_classifier_tests)

        add_executable(stage_manager_hit_testing_tests
            tests/hit_testing_tests.cpp
        )

        stage_manager_target_defaults(stage_manager_hit_testing_tests)
        target_link_libraries(stage_manager_hit_testing_tests PRIVATE stage_manager_core user32)
        add_test(NAME stage_manager_hit_testing COMMAND stage_manager_hit_testing_tests)
    endif()

    add_executable(stage_manager_event_pipeline_tests
        tests/event_pipeline_tests.cpp
    )

    stage_manager_target_defaults(stage_manager_event_pipeline_tests)
    target_link_libraries(stage_manager_event_pipeline_tests PRIVATE stage_manager_core)
    add_test(NAME stage_manager_event_pipeline COMMAND stage_manager_event_pipeline_tests)

    add_executable(stage_manager_coordinator_health_tests
        tests/coordinator_health_tests.cpp
    )

    stage_manager_target_defaults(stage_manager_coordinator_health_tests)
    target_link_libraries(stage_manager_coordinator_health_tests PRIVATE stage_manager_core)
    add_test(NAME stage_manager_coordinator_health COMMAND stage_manager_coordinator_health_tests)

    add_executable(stage_manager_snapshot_report_tests
        tests/snapshot_report_tests.cpp
    )

    stage_manager_target_defaults(stage_manager_snapshot_report_tests)
    target_link_libraries(stage_manager_snapshot_report_tests PRIVATE stage_manager_core)
    add_test(NAME stage_manager_snapshot_report COMMAND stage_manager_snapshot_report_tests)

    add_executable(stage_manager_geometry_core_tests
        tests/geometry_core_tests.cpp
    )

    stage_manager_target_defaults(stage_manager_geometry_core_tests)
    target_link_libraries(stage_manager_geometry_core_tests PRIVATE stage_manager_core)
    add_test(NAME stage_manager_geometry_core COMMAND stage_manager_geometry_core_tests)

    add_executable(stage_manager_region_tests
        tests/region_tests.cpp
    )

    stage_manager_target_defaults(stage_manager_region_tests)
    target_link_libraries(stage_manager_region_tests PRIVATE stage_manager_core)
    add_test(NAME stage_manager_region COMMAND stage_manager_region_tests)

    add_executable(stage_manager_geometry_property_tests
        tests/geometry_property_tests.cpp
    )

    stage_manager_target_defaults(stage_manager_geometry_property_tests)
    target_link_libraries(stage_manager_geometry_property_tests PRIVATE stage_manager_core)
    add_test(NAME stage_manager_geometry_property COMMAND stage_manager_geometry_property_tests)

    add_executable(stage_manager_candidate_tests
        tests/candidate_tests.cpp
    )

    stage_manager_target_defaults(stage_manager_candidate_tests)
    target_link_libraries(stage_manager_candidate_tests PRIVATE stage_manager_core)
    add_test(NAME stage_manager_candidate COMMAND stage_manager_candidate_tests)

    add_executable(stage_manager_candidate_ranker_tests
        tests/candidate_ranker_tests.cpp
    )

    stage_manager_target_defaults(stage_manager_candidate_ranker_tests)
    target_link_libraries(stage_manager_candidate_ranker_tests PRIVATE stage_manager_core)
    add_test(NAME stage_manager_candidate_ranker COMMAND stage_manager_candidate_ranker_tests)

    add_executable(stage_manager_layout_solver_tests
        tests/layout_solver_tests.cpp
    )

    stage_manager_target_defaults(stage_manager_layout_solver_tests)
    target_link_libraries(stage_manager_layout_solver_tests PRIVATE stage_manager_core)
    add_test(NAME stage_manager_layout_solver COMMAND stage_manager_layout_solver_tests)

    add_executable(stage_manager_z_order_solver_tests
        tests/z_order_solver_tests.cpp
    )

    stage_manager_target_defaults(stage_manager_z_order_solver_tests)
    target_link_libraries(stage_manager_z_order_solver_tests PRIVATE stage_manager_core)
    add_test(NAME stage_manager_z_order_solver COMMAND stage_manager_z_order_solver_tests)

    add_executable(stage_manager_solver_property_tests
        tests/solver_property_tests.cpp
    )

    stage_manager_target_defaults(stage_manager_solver_property_tests)
    target_link_libraries(stage_manager_solver_property_tests PRIVATE stage_manager_core)
    add_test(NAME stage_manager_solver_property COMMAND stage_manager_solver_property_tests)

    add_test(NAME stage_manager_scenario_runner
        COMMAND scenario_runner --iterations 2 --seed 151068710)

    add_executable(stage_manager_move_applier_tests
        tests/move_applier_tests.cpp
        tests/move_transaction_tests.cpp
    )

    stage_manager_target_defaults(stage_manager_move_applier_tests)
    target_link_libraries(stage_manager_move_applier_tests PRIVATE stage_manager_core)
    add_test(NAME stage_manager_move_applier COMMAND stage_manager_move_applier_tests)

    add_executable(stage_manager_mvp_coordinator_tests
        tests/mvp_coordinator_tests.cpp
    )

    stage_manager_target_defaults(stage_manager_mvp_coordinator_tests)
    target_link_libraries(stage_manager_mvp_coordinator_tests PRIVATE stage_manager_core)
    add_test(NAME stage_manager_mvp_coordinator COMMAND stage_manager_mvp_coordinator_tests)

    if(WIN32)
        add_test(NAME stage_manager_release_bundle
            COMMAND powershell.exe -NoProfile -ExecutionPolicy Bypass
                -File ${CMAKE_CURRENT_SOURCE_DIR}/tests/release_bundle_tests.ps1
                -ScriptPath ${CMAKE_CURRENT_SOURCE_DIR}/scripts/stage_release.ps1
                -BuildDirectory $<TARGET_FILE_DIR:stage_manager>)

        add_test(NAME stage_manager_documentation
            COMMAND powershell.exe -NoProfile -ExecutionPolicy Bypass
                -File ${CMAKE_CURRENT_SOURCE_DIR}/tests/documentation_tests.ps1
                -Workspace ${CMAKE_CURRENT_SOURCE_DIR})

        add_executable(stage_manager_win32_window_mover_tests
            tests/win32_window_mover_tests.cpp
        )

        stage_manager_target_defaults(stage_manager_win32_window_mover_tests)
        target_link_libraries(stage_manager_win32_window_mover_tests PRIVATE stage_manager_core user32)
        add_test(NAME stage_manager_win32_window_mover COMMAND stage_manager_win32_window_mover_tests)
    endif()
endif()
