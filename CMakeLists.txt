cmake_minimum_required(VERSION 3.16)

project(my_uvc LANGUAGES C CXX)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_C_EXTENSIONS ON)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS ON)

set(MY_UVC_VENDOR_DIR "${CMAKE_CURRENT_SOURCE_DIR}/libuvc/third_party/uvc")

if(NOT EXISTS "${MY_UVC_VENDOR_DIR}/uvc_control.c")
	message(FATAL_ERROR "local uvc sources not found: ${MY_UVC_VENDOR_DIR}")
endif()

find_package(JPEG REQUIRED)

set(MY_UVC_JPEG_LIB "")
if(DEFINED CMAKE_SYSROOT AND EXISTS "${CMAKE_SYSROOT}/usr/lib/libjpeg.so.8")
	set(MY_UVC_JPEG_LIB "${CMAKE_SYSROOT}/usr/lib/libjpeg.so.8")
endif()

# Add subdirectories
add_subdirectory(libuvc)
add_subdirectory(uvc_main)

option(MY_UVC_ENABLE_UNIT_TESTS "Build host-only unit tests (no toolchain)" OFF)
if(MY_UVC_ENABLE_UNIT_TESTS AND NOT CMAKE_CROSSCOMPILING)
	enable_testing()
	add_executable(test_my_uvc_config_mapping
		tests/unit/test_my_uvc_config_mapping.cpp
		uvc_main/libmy_uvc_config_bridge.cpp
		libuvc/src/app_config.cpp
	)
	target_include_directories(test_my_uvc_config_mapping PRIVATE
		${CMAKE_CURRENT_SOURCE_DIR}/libuvc/include
		${CMAKE_CURRENT_SOURCE_DIR}/uvc_main
	)
	target_compile_options(test_my_uvc_config_mapping PRIVATE -Wall -Wextra)
	add_test(NAME test_my_uvc_config_mapping COMMAND test_my_uvc_config_mapping)

	add_executable(test_my_uvc_submit_errors
		tests/unit/test_my_uvc_submit_errors.cpp
		libuvc/src/core/my_uvc_submit_validate.cpp
	)
	target_include_directories(test_my_uvc_submit_errors PRIVATE
		${CMAKE_CURRENT_SOURCE_DIR}/libuvc/include
		${CMAKE_CURRENT_SOURCE_DIR}/libuvc/src/core
	)
	target_compile_options(test_my_uvc_submit_errors PRIVATE -Wall -Wextra)
	add_test(NAME test_my_uvc_submit_errors COMMAND test_my_uvc_submit_errors)

	add_executable(test_uvctest_cli_overrides_ini
		tests/unit/test_uvctest_cli_overrides_ini.cpp
		uvc_main/uvctest_cli.cpp
		libuvc/src/app_config.cpp
	)
	target_include_directories(test_uvctest_cli_overrides_ini PRIVATE
		${CMAKE_CURRENT_SOURCE_DIR}/libuvc/include
		${CMAKE_CURRENT_SOURCE_DIR}/uvc_main
	)
	target_compile_options(test_uvctest_cli_overrides_ini PRIVATE -Wall -Wextra)
	add_test(NAME test_uvctest_cli_overrides_ini COMMAND test_uvctest_cli_overrides_ini)

	add_executable(test_app_config_path_directory_vs_file
		tests/unit/test_app_config_path_directory_vs_file.cpp
		libuvc/src/app_config.cpp
	)
	target_include_directories(test_app_config_path_directory_vs_file PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/libuvc/include)
	target_compile_options(test_app_config_path_directory_vs_file PRIVATE -Wall -Wextra)
	add_test(NAME test_app_config_path_directory_vs_file COMMAND test_app_config_path_directory_vs_file)

	add_executable(test_app_config_ini_merge
		tests/unit/test_app_config_ini_merge.cpp
		libuvc/src/app_config.cpp
	)
	target_include_directories(test_app_config_ini_merge PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/libuvc/include)
	target_compile_options(test_app_config_ini_merge PRIVATE -Wall -Wextra)
	add_test(NAME test_app_config_ini_merge COMMAND test_app_config_ini_merge)

	add_executable(test_pip_tile_layout_rects tests/unit/test_pip_tile_layout_rects.cpp)
	target_include_directories(test_pip_tile_layout_rects PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/libuvc/include)
	target_compile_options(test_pip_tile_layout_rects PRIVATE -Wall -Wextra)
	add_test(NAME test_pip_tile_layout_rects COMMAND test_pip_tile_layout_rects)

	add_executable(test_pip_helper_config_defaults
		tests/unit/test_pip_helper_config_defaults.cpp
		libuvc/src/app_config.cpp
	)
	target_include_directories(test_pip_helper_config_defaults PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/libuvc/include)
	target_compile_options(test_pip_helper_config_defaults PRIVATE -Wall -Wextra)
	add_test(NAME test_pip_helper_config_defaults COMMAND test_pip_helper_config_defaults)

	add_executable(test_pip_stale_timeout_policy tests/unit/test_pip_stale_timeout_policy.cpp)
	target_include_directories(test_pip_stale_timeout_policy PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/libuvc/include)
	target_compile_options(test_pip_stale_timeout_policy PRIVATE -Wall -Wextra)
	add_test(NAME test_pip_stale_timeout_policy COMMAND test_pip_stale_timeout_policy)

	add_executable(test_pip_ini_stale_timeout_parse
		tests/unit/test_pip_ini_stale_timeout_parse.cpp
		libuvc/src/app_config.cpp
	)
	target_include_directories(test_pip_ini_stale_timeout_parse PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/libuvc/include)
	target_compile_options(test_pip_ini_stale_timeout_parse PRIVATE -Wall -Wextra)
	add_test(NAME test_pip_ini_stale_timeout_parse COMMAND test_pip_ini_stale_timeout_parse)

	add_executable(test_pip_tile_ini_parse tests/unit/test_pip_tile_ini_parse.cpp libuvc/src/app_config.cpp)
	target_include_directories(test_pip_tile_ini_parse PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/libuvc/include)
	target_compile_options(test_pip_tile_ini_parse PRIVATE -Wall -Wextra)
	add_test(NAME test_pip_tile_ini_parse COMMAND test_pip_tile_ini_parse)

	add_executable(test_uvctest_pip_tile_nv12_paths_ini tests/unit/test_uvctest_pip_tile_nv12_paths_ini.cpp
		libuvc/src/app_config.cpp
	)
	target_include_directories(test_uvctest_pip_tile_nv12_paths_ini PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/libuvc/include)
	target_compile_options(test_uvctest_pip_tile_nv12_paths_ini PRIVATE -Wall -Wextra)
	add_test(NAME test_uvctest_pip_tile_nv12_paths_ini COMMAND test_uvctest_pip_tile_nv12_paths_ini)

	add_executable(test_my_uvc_ini_section_loader
		tests/unit/test_my_uvc_ini_section_loader.cpp
		libuvc/src/app_config.cpp
	)
	target_include_directories(test_my_uvc_ini_section_loader PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/libuvc/include)
	target_compile_options(test_my_uvc_ini_section_loader PRIVATE -Wall -Wextra)
	add_test(NAME test_my_uvc_ini_section_loader COMMAND test_my_uvc_ini_section_loader)

	add_executable(test_my_uvc_load_ini_section_c_api
		tests/unit/test_my_uvc_load_ini_section_c_api.cpp
		libuvc/src/core/my_uvc_load_ini_section_c.cpp
		libuvc/src/app_config.cpp
	)
	target_include_directories(test_my_uvc_load_ini_section_c_api PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/libuvc/include)
	target_compile_options(test_my_uvc_load_ini_section_c_api PRIVATE -Wall -Wextra)
	add_test(NAME test_my_uvc_load_ini_section_c_api COMMAND test_my_uvc_load_ini_section_c_api)

	add_executable(test_my_uvc_channel_video_stub
		tests/unit/test_my_uvc_channel_video_stub.cpp
		libuvc/src/core/my_uvc_video_id_resolve.cpp
	)
	target_include_directories(test_my_uvc_channel_video_stub PRIVATE
		${CMAKE_CURRENT_SOURCE_DIR}/libuvc/include
		${CMAKE_CURRENT_SOURCE_DIR}/libuvc/src/core
	)
	target_compile_options(test_my_uvc_channel_video_stub PRIVATE -Wall -Wextra)
	add_test(NAME test_my_uvc_channel_video_stub COMMAND test_my_uvc_channel_video_stub)

	add_executable(test_pip_compose_offline_golden tests/unit/test_pip_compose_offline_golden.cpp)
	target_compile_options(test_pip_compose_offline_golden PRIVATE -Wall -Wextra)
	add_test(NAME test_pip_compose_offline_golden COMMAND test_pip_compose_offline_golden)
endif()

if(CMAKE_CROSSCOMPILING)
	add_executable(my_uvc_minimal examples/my_uvc_minimal.cpp)
	target_link_libraries(my_uvc_minimal PRIVATE my_uvc_lib)
	target_include_directories(my_uvc_minimal PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/libuvc/include)
	set_target_properties(my_uvc_minimal PROPERTIES
		BUILD_RPATH "${CMAKE_BINARY_DIR}/libuvc"
		INSTALL_RPATH "\$ORIGIN/../lib"
	)
endif()

install(TARGETS uvc_main my_uvc_lib my_uvc_pip_helper
	RUNTIME DESTINATION bin
	LIBRARY DESTINATION lib
	ARCHIVE DESTINATION lib
)
if(CMAKE_CROSSCOMPILING)
	install(TARGETS my_uvc_minimal RUNTIME DESTINATION bin)
endif()
install(FILES
	libuvc/include/my_uvc/my_uvc.h
	DESTINATION include/my_uvc
)
install(FILES
	libuvc/include/my_uvc_pip/pip_helper.h
	libuvc/include/my_uvc_pip/pip_tile_layout.hpp
	libuvc/include/my_uvc_pip/pip_overlay_stale_policy.hpp
	DESTINATION include/my_uvc_pip
)
install(PROGRAMS scripts/my_uvc_usb_config.sh DESTINATION bin)
install(FILES
	config/libmy_uvc.ini
	config/libmy_uvc_pip.ini
	config/uvctest.ini
	config/README_CONFIG.md
	DESTINATION share
)
install(FILES
	config/profiles/my_uvc_1ch_independent.ini
	config/profiles/my_uvc_2ch_independent.ini
	config/profiles/my_uvc_4ch_independent.ini
	config/profiles/my_uvc_6ch_independent.ini
	config/profiles/my_uvc_8ch_independent.ini
	config/profiles/my_uvc_10ch_independent.ini
	config/profiles/my_uvc_12ch_independent.ini
	config/profiles/my_uvc_16ch_independent.ini
	DESTINATION share/profiles
)
