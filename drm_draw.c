#include <xf86drm.h>
#include <xf86drmMode.h>
#include <stdint.h>
#include <fcntl.h>
#include <unistd.h>

#include "./common/buffers.h"
#include "./common/drm_common.h"
#include "drm.h"

typedef struct drm_dev {
	int fd;
	
} drm_dev_t;

int main(int argc, char **argv) {
	int fd = open_drm("/dev/dri/card0", DRM_CAP_DUMB_BUFFER);
	if(fd < 0) {
		return 1;
	}
		
	drmModeRes *res = drmModeGetResources(fd);
	drmModeAtomicReq *req; 
	
		
	close(fd);
	return 0;
}
