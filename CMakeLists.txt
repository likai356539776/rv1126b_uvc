cmake_minimum_required(VERSION 3.16)

project(my_uvc LANGUAGES C CXX)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_C_EXTENSIONS ON)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS ON)

set(MY_UVC_VENDOR_DIR "${CMAKE_CURRENT_SOURCE_DIR}/third_party/uvc")

if(NOT EXISTS "${MY_UVC_VENDOR_DIR}/uvc_control.c")
	message(FATAL_ERROR "local uvc sources not found: ${MY_UVC_VENDOR_DIR}")
endif()

find_package(JPEG REQUIRED)

set(MY_UVC_JPEG_LIB "")
if(DEFINED CMAKE_SYSROOT AND EXISTS "${CMAKE_SYSROOT}/usr/lib/libjpeg.so.8")
	set(MY_UVC_JPEG_LIB "${CMAKE_SYSROOT}/usr/lib/libjpeg.so.8")
endif()

add_library(my_uvc_lib SHARED
	src/core/my_uvc.cpp
	src/core/my_uvc_video_id_resolve.cpp
	src/core/my_uvc_submit_validate.cpp
	src/core/my_uvc_load_ini_section_c.cpp
	src/app_config.cpp
	src/uevent_stub.c
	${MY_UVC_VENDOR_DIR}/uvc_control.c
	${MY_UVC_VENDOR_DIR}/uvc_encode.c
	${MY_UVC_VENDOR_DIR}/uvc_video.cpp
	${MY_UVC_VENDOR_DIR}/uvc-gadget.c
	${MY_UVC_VENDOR_DIR}/yuv.c
)
add_library(my_uvc::my_uvc ALIAS my_uvc_lib)

set_target_properties(my_uvc_lib PROPERTIES
	OUTPUT_NAME "my_uvc"
	VERSION 1.0.0
	SOVERSION 1
	C_VISIBILITY_PRESET hidden
	CXX_VISIBILITY_PRESET hidden
	VISIBILITY_INLINES_HIDDEN ON
)
target_include_directories(my_uvc_lib
	PUBLIC
	${CMAKE_CURRENT_SOURCE_DIR}/include
	PRIVATE
	${CMAKE_CURRENT_SOURCE_DIR}/src/core
	${MY_UVC_VENDOR_DIR}
	${CMAKE_SYSROOT}/usr/include/rockchip
	${CMAKE_SYSROOT}/usr/include/rga
)
target_compile_options(my_uvc_lib PRIVATE
	-Wall
	-Wextra
	-Wno-unused-parameter
)
if(MY_UVC_JPEG_LIB)
	target_link_libraries(my_uvc_lib PRIVATE pthread "${MY_UVC_JPEG_LIB}" rockchip_mpp rga)
else()
	target_link_libraries(my_uvc_lib PRIVATE pthread JPEG::JPEG rockchip_mpp rga)
endif()

add_library(my_uvc_pip_helper STATIC
	src/pip_helper/pip_helper_mjpeg.cpp
	src/pip_mjpeg.cpp
	src/mpp_jpeg.cpp
)
target_include_directories(my_uvc_pip_helper PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include)
target_include_directories(my_uvc_pip_helper PRIVATE
	${CMAKE_SYSROOT}/usr/include/rockchip
	${CMAKE_SYSROOT}/usr/include/rga
)
target_compile_options(my_uvc_pip_helper PRIVATE
	-Wall
	-Wextra
	-Wno-unused-parameter
)
if(MY_UVC_JPEG_LIB)
	target_link_libraries(my_uvc_pip_helper PRIVATE pthread "${MY_UVC_JPEG_LIB}" rockchip_mpp rga)
else()
	target_link_libraries(my_uvc_pip_helper PRIVATE pthread JPEG::JPEG rockchip_mpp rga)
endif()

add_executable(uvctest
	src/uvctest/main.cpp
	src/uvctest/libmy_uvc_config_bridge.cpp
	src/uvctest/uvctest_cli.cpp
	src/app_config.cpp
)

target_include_directories(uvctest PRIVATE
	${CMAKE_CURRENT_SOURCE_DIR}/include
	${MY_UVC_VENDOR_DIR}
	${CMAKE_SYSROOT}/usr/include/rockchip
	${CMAKE_SYSROOT}/usr/include/rga
)

target_compile_options(uvctest PRIVATE
	-Wall
	-Wextra
	-Wno-unused-parameter
)

target_link_libraries(uvctest PRIVATE my_uvc_lib my_uvc_pip_helper)
if(MY_UVC_JPEG_LIB)
	target_link_libraries(uvctest PRIVATE pthread "${MY_UVC_JPEG_LIB}" rockchip_mpp rga)
else()
	target_link_libraries(uvctest PRIVATE pthread JPEG::JPEG rockchip_mpp rga)
endif()

set_target_properties(uvctest PROPERTIES
	BUILD_RPATH "${CMAKE_BINARY_DIR}"
	INSTALL_RPATH "\$ORIGIN/../lib"
)

option(MY_UVC_ENABLE_UNIT_TESTS "Build host-only unit tests (no toolchain)" OFF)
if(MY_UVC_ENABLE_UNIT_TESTS AND NOT CMAKE_CROSSCOMPILING)
	enable_testing()
	add_executable(test_my_uvc_config_mapping
		tests/unit/test_my_uvc_config_mapping.cpp
		src/uvctest/libmy_uvc_config_bridge.cpp
		src/app_config.cpp
	)
	target_include_directories(test_my_uvc_config_mapping PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/include)
	target_compile_options(test_my_uvc_config_mapping PRIVATE -Wall -Wextra)
	add_test(NAME test_my_uvc_config_mapping COMMAND test_my_uvc_config_mapping)

	add_executable(test_my_uvc_submit_errors
		tests/unit/test_my_uvc_submit_errors.cpp
		src/core/my_uvc_submit_validate.cpp
	)
	target_include_directories(test_my_uvc_submit_errors PRIVATE
		${CMAKE_CURRENT_SOURCE_DIR}/include
		${CMAKE_CURRENT_SOURCE_DIR}/src/core
	)
	target_compile_options(test_my_uvc_submit_errors PRIVATE -Wall -Wextra)
	add_test(NAME test_my_uvc_submit_errors COMMAND test_my_uvc_submit_errors)

	add_executable(test_uvctest_cli_overrides_ini
		tests/unit/test_uvctest_cli_overrides_ini.cpp
		src/uvctest/uvctest_cli.cpp
		src/app_config.cpp
	)
	target_include_directories(test_uvctest_cli_overrides_ini PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/include)
	target_compile_options(test_uvctest_cli_overrides_ini PRIVATE -Wall -Wextra)
	add_test(NAME test_uvctest_cli_overrides_ini COMMAND test_uvctest_cli_overrides_ini)

	add_executable(test_app_config_path_directory_vs_file
		tests/unit/test_app_config_path_directory_vs_file.cpp
		src/app_config.cpp
	)
	target_include_directories(test_app_config_path_directory_vs_file PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/include)
	target_compile_options(test_app_config_path_directory_vs_file PRIVATE -Wall -Wextra)
	add_test(NAME test_app_config_path_directory_vs_file COMMAND test_app_config_path_directory_vs_file)

	add_executable(test_pip_tile_layout_rects tests/unit/test_pip_tile_layout_rects.cpp)
	target_include_directories(test_pip_tile_layout_rects PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/include)
	target_compile_options(test_pip_tile_layout_rects PRIVATE -Wall -Wextra)
	add_test(NAME test_pip_tile_layout_rects COMMAND test_pip_tile_layout_rects)

	add_executable(test_pip_helper_config_defaults
		tests/unit/test_pip_helper_config_defaults.cpp
		src/app_config.cpp
	)
	target_include_directories(test_pip_helper_config_defaults PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/include)
	target_compile_options(test_pip_helper_config_defaults PRIVATE -Wall -Wextra)
	add_test(NAME test_pip_helper_config_defaults COMMAND test_pip_helper_config_defaults)

	add_executable(test_my_uvc_ini_section_loader
		tests/unit/test_my_uvc_ini_section_loader.cpp
		src/app_config.cpp
	)
	target_include_directories(test_my_uvc_ini_section_loader PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/include)
	target_compile_options(test_my_uvc_ini_section_loader PRIVATE -Wall -Wextra)
	add_test(NAME test_my_uvc_ini_section_loader COMMAND test_my_uvc_ini_section_loader)

	add_executable(test_my_uvc_load_ini_section_c_api
		tests/unit/test_my_uvc_load_ini_section_c_api.cpp
		src/core/my_uvc_load_ini_section_c.cpp
		src/app_config.cpp
	)
	target_include_directories(test_my_uvc_load_ini_section_c_api PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/include)
	target_compile_options(test_my_uvc_load_ini_section_c_api PRIVATE -Wall -Wextra)
	add_test(NAME test_my_uvc_load_ini_section_c_api COMMAND test_my_uvc_load_ini_section_c_api)

	add_executable(test_my_uvc_channel_video_stub
		tests/unit/test_my_uvc_channel_video_stub.cpp
		src/core/my_uvc_video_id_resolve.cpp
	)
	target_include_directories(test_my_uvc_channel_video_stub PRIVATE
		${CMAKE_CURRENT_SOURCE_DIR}/include
		${CMAKE_CURRENT_SOURCE_DIR}/src/core
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
	set_target_properties(my_uvc_minimal PROPERTIES
		BUILD_RPATH "${CMAKE_BINARY_DIR}"
		INSTALL_RPATH "\$ORIGIN/../lib"
	)
endif()

install(TARGETS uvctest my_uvc_lib my_uvc_pip_helper
	RUNTIME DESTINATION bin
	LIBRARY DESTINATION lib
	ARCHIVE DESTINATION lib
)
if(CMAKE_CROSSCOMPILING)
	install(TARGETS my_uvc_minimal RUNTIME DESTINATION bin)
endif()
install(FILES
	include/my_uvc/my_uvc.h
	DESTINATION include/my_uvc
)
install(FILES
	include/my_uvc_pip/pip_helper.h
	include/my_uvc_pip/pip_tile_layout.hpp
	DESTINATION include/my_uvc_pip
)
install(PROGRAMS scripts/my_uvc_usb_config.sh DESTINATION bin)
install(FILES
	config/my_uvc.ini
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

