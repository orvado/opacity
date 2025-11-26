# Tests configuration for Opacity

# Placeholder for test infrastructure
# Phase 1 will include:
# - Unit tests for core subsystems
# - Integration tests for file operations
# - Performance benchmarks

message(STATUS "Tests configured (infrastructure ready)")

add_executable(opacity_imgui_test
	imgui_scoped_compile_test.cpp
)

target_include_directories(opacity_imgui_test PUBLIC ${PROJECT_SOURCE_DIR}/include)
target_link_libraries(opacity_imgui_test PRIVATE imgui::imgui)

set_target_properties(opacity_imgui_test PROPERTIES FOLDER "Opacity/Tests")

add_custom_target(run_imgui_test
	COMMAND $<TARGET_FILE:opacity_imgui_test>
	DEPENDS opacity_imgui_test
	WORKING_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}
)
