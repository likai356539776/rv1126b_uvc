#include "uevent.h"

#include "uvc_control.h"

#include <errno.h>
#include <linux/netlink.h>
#include <pthread.h>
#include <stdbool.h>
#include <stdio.h>
#include <string.h>
#include <sys/prctl.h>
#include <sys/socket.h>
#include <unistd.h>
#include <stdlib.h>

/* Keep behavior aligned with rkipc: wake control thread on video add/delete. */
static const char *kVideoSubsystem = "video4linux";
static int g_uevent_dump_all = -1;

static int should_dump_all_uevents(void) {
	if (g_uevent_dump_all >= 0)
		return g_uevent_dump_all;
	const char *env = getenv("MY_UVC_UEVENT_DUMP_ALL");
	if (!env) {
		g_uevent_dump_all = 0;
		return g_uevent_dump_all;
	}
	if (strcmp(env, "1") == 0 || strcmp(env, "true") == 0 || strcmp(env, "TRUE") == 0 ||
	    strcmp(env, "yes") == 0 || strcmp(env, "YES") == 0 || strcmp(env, "on") == 0 ||
	    strcmp(env, "ON") == 0) {
		g_uevent_dump_all = 1;
	} else {
		g_uevent_dump_all = 0;
	}
	return g_uevent_dump_all;
}

static void dump_uevent(const struct _uevent *event, const char *tag) {
	if (!event)
		return;
	printf("uevent dump [%s]: size=%d\n", tag ? tag : "raw", event->size);
	for (int i = 0; i < event->size && i < (int)(sizeof(event->strs) / sizeof(event->strs[0])); i++) {
		if (event->strs[i])
			printf("  uevent.strs[%d]=%s\n", i, event->strs[i]);
	}
}

static void parse_event_and_signal(const struct _uevent *event) {
	if (!event)
		return;

	if (should_dump_all_uevents())
		dump_uevent(event, "all");

	if (event->size <= 0)
		return;

	const char *action = NULL;
	const char *subsystem = NULL;
	const char *usb_state = NULL;

	for (int i = 0; i < event->size && i < (int)(sizeof(event->strs) / sizeof(event->strs[0])); i++) {
		const char *s = event->strs[i];
		if (!s)
			continue;

		if (strncmp(s, "ACTION=", 7) == 0 && !action)
			action = s + 7;
		else if (strncmp(s, "SUBSYSTEM=", 10) == 0 && !subsystem)
			subsystem = s + 10;
		else if (strncmp(s, "USB_STATE=", 10) == 0 && !usb_state)
			usb_state = s + 10;
	}

	/* Compat for kernels that provide "add@/devices/..." in slot 0. */
	if (!action && event->strs[0]) {
		if (strncmp(event->strs[0], "add@", 4) == 0)
			action = "add";
		else if (strncmp(event->strs[0], "remove@", 7) == 0)
			action = "remove";
		else if (strncmp(event->strs[0], "delete@", 7) == 0)
			action = "delete";
		else if (strncmp(event->strs[0], "bind@", 5) == 0)
			action = "bind";
		else if (strncmp(event->strs[0], "unbind@", 7) == 0)
			action = "unbind";
	}

	if (subsystem && strcmp(subsystem, kVideoSubsystem) == 0) {
		dump_uevent(event, "video4linux");
		if (action &&
		    (strcmp(action, "add") == 0 || strcmp(action, "remove") == 0 ||
		     strcmp(action, "delete") == 0 || strcmp(action, "change") == 0)) {
			printf("uevent video action=%s, signal uvc_control\n", action);
			uvc_control_signal();
		}
		return;
	}

	(void)usb_state;
}

static void *event_monitor_thread(void *arg) {
	uint32_t flags = *(uint32_t *)arg;
	(void)flags;

	prctl(PR_SET_NAME, "event_monitor", 0, 0, 0);

	int sockfd = -1;
	char buf[512];
	struct iovec iov;
	struct msghdr msg;
	struct sockaddr_nl sa;
	struct _uevent event;

	memset(&sa, 0, sizeof(sa));
	sa.nl_family = AF_NETLINK;
	sa.nl_groups = NETLINK_KOBJECT_UEVENT;
	sa.nl_pid = 0;

	memset(&msg, 0, sizeof(msg));
	iov.iov_base = (void *)buf;
	iov.iov_len = sizeof(buf);
	msg.msg_name = (void *)&sa;
	msg.msg_namelen = sizeof(sa);
	msg.msg_iov = &iov;
	msg.msg_iovlen = 1;

	sockfd = socket(AF_NETLINK, SOCK_RAW, NETLINK_KOBJECT_UEVENT);
	if (sockfd == -1) {
		printf("uevent socket create failed: %s\n", strerror(errno));
		goto out;
	}

	if (bind(sockfd, (struct sockaddr *)&sa, sizeof(sa)) == -1) {
		printf("uevent bind failed: %s\n", strerror(errno));
		goto out;
	}

	while (1) {
		int i = 0;
		int j = 0;
		int len = 0;

		event.size = 0;
		memset(event.strs, 0, sizeof(event.strs));
		len = recvmsg(sockfd, &msg, 0);
		if (len < 0)
			continue;
		if (len > (int)sizeof(buf))
			continue;

		for (i = 0, j = 0; i < len && j < (int)(sizeof(event.strs) / sizeof(event.strs[0])); i++) {
			if (*(buf + i) == '\0' && (i + 1) != len) {
				event.strs[j++] = buf + i + 1;
				event.size = j;
			}
		}

		parse_event_and_signal(&event);
	}

out:
	if (sockfd >= 0)
		close(sockfd);
	pthread_detach(pthread_self());
	pthread_exit(NULL);
}

int uevent_monitor_run(uint32_t flags) {
	pthread_t tid;
	return pthread_create(&tid, NULL, event_monitor_thread, &flags);
}
