#include "./drm_common.h"

#include <log.h>
#include <stdint.h>
#include <xf86drm.h>
#include <xf86drmMode.h>
#include <fcntl.h>
#include <unistd.h>

int open_drm(const char *path, uint64_t cap) {
	uint64_t hasCap = 0;
	int ret;
	int fd = open(path, O_CLOEXEC | O_RDWR);

	if(fd < 0) {
		logger_warn("Failed to open DRM device %s %m", path);
		return -1;
	}

	ret = drmGetCap(fd, cap, &hasCap);
	if(ret < 0 || !hasCap) {
		logger_warn("DRM dev %s, does not support request capablities", path);
		close(fd);
		return -2;
	}

	return fd;
}


