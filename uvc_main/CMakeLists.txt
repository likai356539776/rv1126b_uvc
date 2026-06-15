# uvc_main/CMakeLists.txt

set(MODEL_ZOO_DIR "/home/kama/workspace/ubuntu20.04/rknpu_work/rknn_model_zoo")
set(TARGET_SOC "rv1126b")

add_definitions(-DRKNPU1)

# Import model zoo subdirectories
add_subdirectory(${MODEL_ZOO_DIR}/3rdparty 3rdparty.out)
add_subdirectory(${MODEL_ZOO_DIR}/utils utils.out)

add_executable(uvc_main
	main.cpp
	libmy_uvc_config_bridge.cpp
	uvctest_cli.cpp
	../libuvc/src/app_config.cpp
	yolo_person_detector.cc
	vendor/yolov8/postprocess.cc
	vendor/yolov8/rknpu1/yolov8.cc
	camera_rockit_vi_vo.cc
	app_log.cc
)

target_compile_definitions(uvc_main PRIVATE
	RV1126B=1
	MY_APP_ENABLE_ROCKIT_CAMERA=1
)

target_include_directories(uvc_main PRIVATE
	${CMAKE_CURRENT_SOURCE_DIR}
	${CMAKE_CURRENT_SOURCE_DIR}/vendor/yolov8
	${CMAKE_SYSROOT}/usr/include/rockchip
	${CMAKE_SYSROOT}/usr/include/rga
	${MODEL_ZOO_DIR}/utils
	${LIBRGA_INCLUDES}
	${LIBRKNNRT_INCLUDES}
	${STB_INCLUDES}
	${LIBROCKIT_INCLUDES}
)

target_compile_options(uvc_main PRIVATE
	-Wall
	-Wextra
	-Wno-unused-parameter
)

target_link_libraries(uvc_main PRIVATE 
	my_uvc_lib 
	my_uvc_pip_helper
	fileutils
	imageutils
	imagedrawing
	${LIBRKNNRT}
	${LIBROCKIT}
	dl
)

if(LIBMPP)
	target_link_libraries(uvc_main PRIVATE ${LIBMPP})
endif()

if(LIBDRM_ROCKIT)
	target_link_libraries(uvc_main PRIVATE ${LIBDRM_ROCKIT})
else()
	target_link_libraries(uvc_main PRIVATE drm)
endif()

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
