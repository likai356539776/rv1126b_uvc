/*
 * Copyright (C) 2019 Rockchip Electronics Co., Ltd.
 *
 * This software is available to you under a choice of one of two
 * licenses.  You may choose to be licensed under the terms of the GNU
 * General Public License (GPL), available from the file
 * COPYING in the main directory of this source tree, or the
 * OpenIB.org BSD license below:
 *
 *     Redistribution and use in source and binary forms, with or
 *     without modification, are permitted provided that the following
 *     conditions are met:
 *
 *      - Redistributions of source code must retain the above
 *        copyright notice, this list of conditions and the following
 *        disclaimer.
 *
 *      - Redistributions in binary form must reproduce the above
 *        copyright notice, this list of conditions and the following
 *        disclaimer in the documentation and/or other materials
 *        provided with the distribution.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
 * EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
 * MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND
 * NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS
 * BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN
 * ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
 * CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 */

#include "uvc_control.h"
#include "uevent.h"
#include "uvc_encode.h"
#include "uvc_video.h"
#include <dirent.h>
#include <fcntl.h>
#include <pthread.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#define SYS_ISP_NAME "isp"
#define SYS_CIF_NAME "cif"
#define UVC_STREAMING_INTF_PATH                                                                    \
	"/sys/kernel/config/usb_gadget/rockchip/functions/uvc.gs1/streaming/bInterfaceNumber"

struct uvc_ctrl {
	int id;
	int width;
	int height;
	int fps;
};

#define UVC_CTRL_MAX 16
static struct uvc_ctrl uvc_ctrl[UVC_CTRL_MAX];
static int uvc_ctrl_count = 0;
struct uvc_encode uvc_enc;
static pthread_mutex_t lock = PTHREAD_MUTEX_INITIALIZER;
static int uvc_streaming_intf = -1;
static int uvc_active_streams = 0;

static pthread_t run_id = 0;
static bool run_flag = true;
static pthread_mutex_t run_mutex = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t run_cond = PTHREAD_COND_INITIALIZER;
static pthread_cond_t video_added = PTHREAD_COND_INITIALIZER;

static uvc_open_camera_callback uvc_open_camera_cb = NULL;
static uvc_close_camera_callback uvc_close_camera_cb = NULL;

void register_uvc_open_camera(uvc_open_camera_callback cb) { uvc_open_camera_cb = cb; }

void register_uvc_close_camera(uvc_close_camera_callback cb) { uvc_close_camera_cb = cb; }

static bool is_uvc_video(void *buf) {
	if (strstr(buf, "usb") || strstr(buf, "gadget"))
		return true;
	else
		return false;
}

static void query_uvc_streaming_intf(void) {
	int fd;

	fd = open(UVC_STREAMING_INTF_PATH, O_RDONLY);
	if (fd >= 0) {
		char intf[32] = {0};
		read(fd, intf, sizeof(intf) - 1);
		uvc_streaming_intf = atoi(intf);
		printf("uvc_streaming_intf = %d\n", uvc_streaming_intf);
		close(fd);
	} else {
		printf("open %s failed!\n", UVC_STREAMING_INTF_PATH);
	}
}

int get_uvc_streaming_intf(void) { return uvc_streaming_intf; }

int get_max_video_number() {
	const char *dir_path = "/sys/class/video4linux/";
	struct dirent *entry;
	int max_video_number = -1;

	DIR *dir = opendir(dir_path);
	if (dir == NULL) {
		printf("open %s failed.\n", dir_path);
		return -1;
	}

	while ((entry = readdir(dir)) != NULL) {
		if (strncmp(entry->d_name, "video", 5) == 0) {
			int video_number = atoi(entry->d_name + 5);
			if (video_number > max_video_number) {
				max_video_number = video_number;
			}
		}
	}

	closedir(dir);

	return max_video_number;
}

int check_uvc_video_id(void) {
	FILE *fp = NULL;
	char buf[1024];
	int i;
	char cmd[128];
	int max = -1;
	int uvc_cnt = 1;
	int find_cnt = 0;

	if (getenv("UVC_CNT"))
		uvc_cnt = atoi(getenv("UVC_CNT"));
	if (uvc_cnt < 1)
		uvc_cnt = 1;
	if (uvc_cnt > UVC_CTRL_MAX)
		uvc_cnt = UVC_CTRL_MAX;

	memset(&uvc_ctrl, 0, sizeof(uvc_ctrl));
	for (i = 0; i < UVC_CTRL_MAX; i++)
		uvc_ctrl[i].id = -1;
	uvc_ctrl_count = 0;
	max = get_max_video_number();
	if (max < 0)
		return -1;
	/*
	 * 参照 rkipc 的逻辑：
	 * - 扫描仍按 video id 从大到小（便于快速找到 gadget 节点）
	 * - 但写入 uvc_ctrl[] 时按“反向”写入，确保 uvc_video_id_get(0) 对应更小的 video id，
	 *   这样主机打开第 1 路时更容易对应到 channel0（避免通道顺序倒序）。
	 */
	for (i = max; i >= 0; i--) {
		snprintf(cmd, sizeof(cmd), "/sys/class/video4linux/video%d/name", i);
		if (access(cmd, F_OK))
			continue;
		snprintf(cmd, sizeof(cmd), "cat /sys/class/video4linux/video%d/name", i);
		fp = popen(cmd, "r");
		if (fp) {
			if (fgets(buf, sizeof(buf), fp)) {
				if (is_uvc_video(buf)) {
					if (find_cnt < uvc_cnt && find_cnt < UVC_CTRL_MAX) {
						int store = (uvc_cnt - 1) - find_cnt;
						if (store < 0)
							store = 0;
						if (store >= UVC_CTRL_MAX)
							store = UVC_CTRL_MAX - 1;
						uvc_ctrl[store].id = i;
						find_cnt++;
					}
				}
			}
			pclose(fp);
		}
		if (find_cnt >= uvc_cnt)
			break;
	}
	if (find_cnt <= 0) {
		printf("Please configure uvc...\n");
		return -1;
	}
	uvc_ctrl_count = uvc_cnt;
	if (uvc_ctrl_count > find_cnt)
		uvc_ctrl_count = find_cnt;
	printf("detected uvc video nodes: %d\n", uvc_ctrl_count);
	query_uvc_streaming_intf();
	return 0;
}

void add_uvc_video() {
	int i;
	for (i = 0; i < uvc_ctrl_count; i++) {
		if (uvc_ctrl[i].id >= 0)
			uvc_video_id_add(uvc_ctrl[i].id);
	}
}

void uvc_control_init(int width, int height, int fcc, int fps) {
	pthread_mutex_lock(&lock);
	if (uvc_active_streams == 0) {
		memset(&uvc_enc, 0, sizeof(uvc_enc));
		if (uvc_encode_init(&uvc_enc, width, height, fcc)) {
			printf("%s fail!\n", __func__);
			abort();
		}
	}
	uvc_active_streams++;
	pthread_mutex_unlock(&lock);
	if (uvc_open_camera_cb)
		uvc_open_camera_cb(width, height, fcc, fps);
}

void uvc_control_exit() {
	pthread_mutex_lock(&lock);
	if (uvc_active_streams > 0)
		uvc_active_streams--;
	if (uvc_active_streams == 0) {
		uvc_encode_exit(&uvc_enc);
		memset(&uvc_enc, 0, sizeof(uvc_enc));
	}
	pthread_mutex_unlock(&lock);
	if (uvc_close_camera_cb)
		uvc_close_camera_cb();
}

void uvc_read_camera_buffer(void *cam_buf, int cam_fd, size_t cam_size, void *extra_data,
                            size_t extra_size) {
	int i;
	pthread_mutex_lock(&lock);
	if (cam_size <= (size_t)uvc_enc.width * (size_t)uvc_enc.height * 2u) {
		uvc_enc.extra_data = extra_data;
		uvc_enc.extra_size = extra_size;
		for (i = 0; i < uvc_ctrl_count; i++) {
			uvc_enc.video_id = uvc_video_id_get(i);
			if (uvc_enc.video_id >= 0)
				uvc_encode_process(&uvc_enc, cam_buf, cam_fd, cam_size);
		}
	} else if (uvc_enc.width > 0 && uvc_enc.height > 0) {
		printf("%s: cam_size = %zu, uvc_enc.width = %d, uvc_enc.height = %d\n", __func__, cam_size,
		       uvc_enc.width, uvc_enc.height);
	}
	pthread_mutex_unlock(&lock);
}

void uvc_read_camera_buffer_by_id(void *cam_buf, int cam_fd, size_t cam_size, void *extra_data,
                                  size_t extra_size, int video_id) {
	pthread_mutex_lock(&lock);
	if (cam_size <= (size_t)uvc_enc.width * (size_t)uvc_enc.height * 2u) {
		uvc_enc.video_id = video_id;
		uvc_enc.extra_data = extra_data;
		uvc_enc.extra_size = extra_size;
		if (uvc_enc.video_id >= 0)
			uvc_encode_process(&uvc_enc, cam_buf, cam_fd, cam_size);
	} else if (uvc_enc.width > 0 && uvc_enc.height > 0) {
		printf("%s: cam_size = %zu, uvc_enc.width = %d, uvc_enc.height = %d\n", __func__, cam_size,
		       uvc_enc.width, uvc_enc.height);
	}
	pthread_mutex_unlock(&lock);
}

static void uvc_control_wait(void) {
	pthread_mutex_lock(&run_mutex);
	if (run_flag)
		pthread_cond_wait(&run_cond, &run_mutex);
	pthread_mutex_unlock(&run_mutex);
}

static void uvc_control_wait_timeout_ms(int timeout_ms) {
	struct timespec ts;

	if (timeout_ms <= 0) {
		uvc_control_wait();
		return;
	}

	clock_gettime(CLOCK_REALTIME, &ts);
	ts.tv_sec += timeout_ms / 1000;
	ts.tv_nsec += (long)(timeout_ms % 1000) * 1000000L;
	if (ts.tv_nsec >= 1000000000L) {
		ts.tv_sec += 1;
		ts.tv_nsec -= 1000000000L;
	}

	pthread_mutex_lock(&run_mutex);
	if (run_flag)
		(void)pthread_cond_timedwait(&run_cond, &run_mutex, &ts);
	pthread_mutex_unlock(&run_mutex);
}

void uvc_control_signal(void) {
	pthread_mutex_lock(&run_mutex);
	pthread_cond_signal(&run_cond);
	pthread_mutex_unlock(&run_mutex);
}

void uvc_added_signal(void) {
	pthread_mutex_lock(&run_mutex);
	pthread_cond_signal(&video_added);
	pthread_mutex_unlock(&run_mutex);
}

static void uvc_added_wait(void) {
	pthread_mutex_lock(&run_mutex);
	if (run_flag)
		pthread_cond_wait(&video_added, &run_mutex);
	pthread_mutex_unlock(&run_mutex);
}

static void *uvc_control_thread(void *arg) {
	uint32_t flag = *(uint32_t *)arg;

	while (run_flag) {
		if (!check_uvc_video_id()) {
			add_uvc_video();
			/* Ensure main was waiting for this signal */
			usleep(500);
			uvc_added_signal();
			if (flag & UVC_CONTROL_LOOP_ONCE)
				break;
			uvc_control_wait();
			uvc_video_id_exit_all();
		} else {
			/*
			 * USB unplug/replug may miss one netlink "add" event.
			 * When no UVC node is present, retry periodically.
			 */
			uvc_control_wait_timeout_ms(500);
		}
	}
	pthread_exit(NULL);
}

int uvc_control_run(uint32_t flags) {
	if (flags & UVC_CONTROL_CHECK_STRAIGHT) {
		if (!check_uvc_video_id())
			add_uvc_video();
		else
			return -1;
	} else {
		uevent_monitor_run(flags);
		if (pthread_create(&run_id, NULL, uvc_control_thread, &flags)) {
			printf("%s: pthread_create failed!\n", __func__);
			return -1;
		}
		uvc_added_wait();
	}

	return 0;
}

void uvc_control_join(uint32_t flags) {
	if (flags & UVC_CONTROL_CHECK_STRAIGHT) {
		uvc_video_id_exit_all();
	} else {
		run_flag = false;
		uvc_control_signal();
		pthread_join(run_id, NULL);
		if (flags & UVC_CONTROL_LOOP_ONCE) {
			/* One-shot mode: join already waited for the control thread. */
		}
		uvc_video_id_exit_all();
	}
}
