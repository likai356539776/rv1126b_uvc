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

add_executable(my_uvc
	src/main.cpp
	src/app_config.cpp
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
)

target_compile_options(my_uvc PRIVATE
	-Wall
	-Wextra
	-Wno-unused-parameter
)

target_link_libraries(my_uvc PRIVATE pthread)

install(TARGETS my_uvc RUNTIME DESTINATION bin)
install(PROGRAMS scripts/my_uvc_usb_config.sh DESTINATION bin)
install(FILES config/my_uvc.ini DESTINATION share)
