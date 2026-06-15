# uvc_main/CMakeLists.txt

add_executable(uvc_main
	main.cpp
	libmy_uvc_config_bridge.cpp
	uvctest_cli.cpp
	../libuvc/src/app_config.cpp
)

target_include_directories(uvc_main PRIVATE
	${CMAKE_CURRENT_SOURCE_DIR}
	${CMAKE_SYSROOT}/usr/include/rockchip
	${CMAKE_SYSROOT}/usr/include/rga
)

target_compile_options(uvc_main PRIVATE
	-Wall
	-Wextra
	-Wno-unused-parameter
)

target_link_libraries(uvc_main PRIVATE my_uvc_lib my_uvc_pip_helper)

if(MY_UVC_JPEG_LIB)
	target_link_libraries(uvc_main PRIVATE pthread "${MY_UVC_JPEG_LIB}" rockchip_mpp rga)
else()
	target_link_libraries(uvc_main PRIVATE pthread JPEG::JPEG rockchip_mpp rga)
endif()

set_target_properties(uvc_main PROPERTIES
	OUTPUT_NAME "uvctest"
	BUILD_RPATH "${CMAKE_BINARY_DIR}/libuvc"
	INSTALL_RPATH "\$ORIGIN/../lib"
)
