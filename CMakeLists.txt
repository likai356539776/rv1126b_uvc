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

# Buildroot images may ship both libjpeg v6b (SONAME .62) and libjpeg-turbo v8 (SONAME .8).
# Our headers (JPEG_LIB_VERSION=80 in sysroot/usr/include/jconfig.h) must match the runtime library.
# Force linking against libjpeg.so.8 when available in sysroot.
set(MY_UVC_JPEG_LIB "")
if(DEFINED CMAKE_SYSROOT AND EXISTS "${CMAKE_SYSROOT}/usr/lib/libjpeg.so.8")
	set(MY_UVC_JPEG_LIB "${CMAKE_SYSROOT}/usr/lib/libjpeg.so.8")
endif()

add_executable(my_uvc
	src/main.cpp
	src/app_config.cpp
	src/pip_mjpeg.cpp
	src/mpp_jpeg.cpp
	src/uevent_stub.c
	${MY_UVC_VENDOR_DIR}/uvc_control.c
	${MY_UVC_VENDOR_DIR}/uvc_encode.c
	${MY_UVC_VENDOR_DIR}/uvc_video.cpp
	${MY_UVC_VENDOR_DIR}/uvc-gadget.c
	${MY_UVC_VENDOR_DIR}/yuv.c
)

target_include_directories(my_uvc PRIVATE
	${CMAKE_CURRENT_SOURCE_DIR}/include
	${MY_UVC_VENDOR_DIR}
	${CMAKE_SYSROOT}/usr/include/rockchip
	${CMAKE_SYSROOT}/usr/include/rga
)

target_compile_options(my_uvc PRIVATE
	-Wall
	-Wextra
	-Wno-unused-parameter
)

if(MY_UVC_JPEG_LIB)
	target_link_libraries(my_uvc PRIVATE pthread "${MY_UVC_JPEG_LIB}" rockchip_mpp rga)
else()
	target_link_libraries(my_uvc PRIVATE pthread JPEG::JPEG rockchip_mpp rga)
endif()

install(TARGETS my_uvc RUNTIME DESTINATION bin)
install(PROGRAMS scripts/my_uvc_usb_config.sh DESTINATION bin)
install(FILES config/my_uvc.ini DESTINATION share)
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
