# libuvc/CMakeLists.txt

option(MY_UVC_PIP_HELPER_TRANSITIONAL_IO "pip_helper may preload overlay from pip_overlay_path (JPEG on disk)" ON)

set(MY_UVC_VENDOR_DIR "${CMAKE_CURRENT_SOURCE_DIR}/third_party/uvc")

add_library(my_uvc_config_lib STATIC
	src/app_config.cpp
)
set_target_properties(my_uvc_config_lib PROPERTIES
	POSITION_INDEPENDENT_CODE ON
)
target_include_directories(my_uvc_config_lib
	PUBLIC
	$<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
	$<INSTALL_INTERFACE:include>
)

add_library(my_uvc_lib SHARED
	src/core/my_uvc.cpp
	src/core/my_uvc_video_id_resolve.cpp
	src/core/my_uvc_submit_validate.cpp
	src/core/my_uvc_load_ini_section_c.cpp
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
	$<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
	$<INSTALL_INTERFACE:include>
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
	target_link_libraries(my_uvc_lib PRIVATE pthread "${MY_UVC_JPEG_LIB}" rockchip_mpp rga my_uvc_config_lib)
else()
	target_link_libraries(my_uvc_lib PRIVATE pthread JPEG::JPEG rockchip_mpp rga my_uvc_config_lib)
endif()

add_library(my_uvc_pip_helper STATIC
	src/pip_helper/pip_helper_mjpeg.cpp
	src/pip_mjpeg.cpp
	src/mpp_jpeg.cpp
)

if(MY_UVC_PIP_HELPER_TRANSITIONAL_IO)
	target_compile_definitions(my_uvc_pip_helper PRIVATE MY_UVC_PIP_HELPER_TRANSITIONAL_IO=1)
else()
	target_compile_definitions(my_uvc_pip_helper PRIVATE MY_UVC_PIP_HELPER_TRANSITIONAL_IO=0)
endif()

target_include_directories(my_uvc_pip_helper
	PUBLIC
	$<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
	$<INSTALL_INTERFACE:include>
	PRIVATE
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
