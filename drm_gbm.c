#include "drm.h"
#include "drm_mode.h"
#include <stddef.h>
#include <stdint.h>
#include <xf86drm.h>
#include <xf86drmMode.h>

#include <gbm.h>

#include <unistd.h>
#include <fcntl.h>

#include <stdlib.h>
#include <log.h>

#include <sys/mman.h>
#include <stdio.h>

typedef struct output {
	drmModeConnectorPtr connector;
	drmModeCrtcPtr new_crtc;
	drmModeEncoderPtr encoder;
	drmModeCrtcPtr saved_crtc;
	uint32_t *buffer;
	size_t buffer_size;
	uint32_t fb_id;
	drmModeModeInfo mode; 
}output_t;


typedef struct drm {
	int fd;
	
	struct gbm_device *gbm_dev;
	struct gbm_surface *surface;
	drmModeResPtr res;
	drmModeConnectorPtr connector;
	drmModeEncoderPtr encoder;
	drmModeCrtcPtr crtc;
	drmModeModeInfo mode;
	//output_t out;
	//drmModeResPtr res;
}drm_t;

/* Open a drm device and return the fd 
 * if successful else returns negative
 * number
 *
 * PARAMS: 
 * Path - String of the file to open 
 * Cap - Optional DRM CAPS you want to check for support for 
 * Out_fd - a pointer to a int to output the DRM devices fd into 
 *
 * Returns:
 * fd on success >= 0 
 * -1 if device failed to open
 * -2 if cap wasn't supported 
 */
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

/* Helper function to clean up a drm_t 
 * please make sure any non initilised 
 * ptrs equal null before passing through 
 * device to be cleaned
 */
void drm_cleanup(drm_t *dev) {
	if(!dev->connector) {
		drmModeFreeConnector(dev->connector);
	}

	if(!dev->res) {
		drmModeFreeResources(dev->res);
	}
	
	if(!dev->gbm_dev) {
		gbm_device_destroy(dev->gbm_dev);
	}

	if(dev->fd > -1) {
		close(dev->fd);
	}
	free(dev);
}

drm_t *init_drm() {
	drm_t *dev = calloc(1, sizeof(*dev));
	if(!dev) {
		return NULL;
	}

	dev->fd = open_drm("/dev/dri/card0", DRM_CAP_DUMB_BUFFER);
	if(dev->fd < 0) {
		drm_cleanup(dev);
		return NULL;
	}
	
	
	dev->gbm_dev = gbm_create_device(dev->fd);
	if(!dev->gbm_dev) {
		logger_fatal("Failed to init gbm device");
		drm_cleanup(dev);
		return NULL;
	}
	
	dev->res = drmModeGetResources(dev->fd);
	if(!dev->res) {
		logger_fatal("Failed to get resources");
		drm_cleanup(dev);
		return NULL;
	}

	for(int i = 0; i < dev->res->count_connectors; i++) {
		dev->connector = drmModeGetConnector(dev->fd, dev->res->connectors[i]);
		if(dev->connector == NULL) {
			continue;
		} else if(dev->connector->connection == DRM_MODE_CONNECTED) {
			break;
		}
		drmModeFreeConnector(dev->connector);
	}

	if(!dev->connector) {
		logger_fatal("Failed to get connector");
		drm_cleanup(dev);
		return NULL;
	}

	dev->encoder = drmModeGetEncoder(dev->fd, dev->connector->encoder_id);
	if(!dev->encoder) {
		logger_fatal("Failed to get encoder");
		return NULL;
	}
	
	dev->crtc = drmModeGetCrtc(dev->fd, dev->encoder->crtc_id);
	if(!dev->crtc) {
		logger_fatal("Failed to get crtc");
		return NULL;
	}
	
	if(dev->connector->count_modes == 0) {
		logger_fatal("connector has no modes");
		drm_cleanup(dev);
		return NULL;
	}

	dev->mode = dev->connector->modes[0];
		
	struct gbm_bo *bo = gbm_bo_create(dev->gbm_dev, dev->mode.hdisplay, dev->mode.vdisplay, GBM_BO_FORMAT_XRGB8888, 0);
	if(!bo) {
		logger_fatal("Surface failed to create");
		drm_cleanup(dev);
		return NULL;
	}
	logger_info("Here");
	//Get the buffer 
	
	if(!bo) {
		logger_fatal("Failed to create buffer object");
		return NULL;
	}
	//Get the handle and pitch for the bo 
	uint32_t handle = gbm_bo_get_handle (bo).u32;
	uint32_t pitch = gbm_bo_get_stride (bo);
	uint32_t fb;
	
	logger_info("%p %p", dev->crtc, dev->encoder);
	drmModeAddFB(dev->fd, dev->mode.hdisplay, dev->mode.vdisplay, 24, 32, pitch, handle, &fb);		
	
	drmModeSetCrtc(dev->fd, dev->crtc->crtc_id, fb, 0, 0, &dev->connector->connector_id, 1, &dev->mode);
	
	sleep(4);
	
	drmModeSetCrtc(dev->fd, dev->crtc->crtc_id, dev->crtc->buffer_id, dev->crtc->x, dev->crtc->y, &dev->connector->connector_id, 1, &dev->crtc->mode);
	return dev;
}

int main(int argc, char **argv) {
	void *var = init_drm();

	if(var != NULL) drm_cleanup(var);
	return 0;
}
