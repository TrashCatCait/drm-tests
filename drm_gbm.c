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

#include <string.h>
#include <memory.h>

#include <sys/mman.h>
#include <stdio.h>

typedef struct drm {
	int fd;
	
	struct gbm_device *gbm_dev;
	struct gbm_bo *bo;
	uint32_t handle;
	uint32_t pitch;
	uint32_t fb;
	uint32_t height;
	uint32_t width;
	uint32_t bpp;

	drmModeResPtr res;
	drmModeConnectorPtr connector;
	drmModeEncoderPtr encoder;
	drmModeCrtcPtr crtc;
	drmModeModeInfo mode;

	void *buffer;
	void *map_data;
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
	if(dev->buffer) {
		gbm_bo_unmap(dev->bo, dev->buffer);
	}

	if(dev->bo) {
		gbm_bo_destroy(dev->bo);
	}
	
	if(dev->crtc) {
		drmModeFreeCrtc(dev->crtc);
	}

	if(dev->encoder) {
		drmModeFreeEncoder(dev->encoder);
	}

	if(dev->connector) {
		drmModeFreeConnector(dev->connector);
	}

	if(dev->res) {
		drmModeFreeResources(dev->res);
	}
	
	if(dev->gbm_dev) {
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
		} else if(dev->connector->connection == DRM_MODE_CONNECTED && dev->connector->count_modes > 0) {
			break;
		}
		drmModeFreeConnector(dev->connector);
		dev->connector = NULL;
	}

	if(!dev->connector) {
		logger_fatal("Failed to get connector");
		drm_cleanup(dev);
		return NULL;
	}

	dev->encoder = drmModeGetEncoder(dev->fd, dev->connector->encoder_id);
	if(!dev->encoder) {
		logger_fatal("Failed to get encoder");
		drm_cleanup(dev);
		return NULL;
	}
	
	dev->crtc = drmModeGetCrtc(dev->fd, dev->encoder->crtc_id);
	if(!dev->crtc) {
		logger_fatal("Failed to get crtc");
		drm_cleanup(dev);
		return NULL;
	}
	
	dev->mode = dev->connector->modes[0];
		
	dev->bo = gbm_bo_create(dev->gbm_dev, dev->mode.hdisplay, dev->mode.vdisplay, GBM_BO_FORMAT_XRGB8888, GBM_BO_USE_SCANOUT | GBM_BO_USE_WRITE);
	if(!dev->bo) {
		logger_fatal("Failed to create GBM_BO");
		drm_cleanup(dev);
		return NULL;
	}

	//Get the handle and pitch for the bo 
	dev->handle = gbm_bo_get_handle (dev->bo).u32;
	dev->pitch = gbm_bo_get_stride (dev->bo);
	
	//Get info about this bo 
	dev->bpp = gbm_bo_get_bpp(dev->bo);
	dev->height = gbm_bo_get_height(dev->bo);
	dev->width = gbm_bo_get_width(dev->bo);

	logger_info("%p %p", dev->buffer, dev->map_data);
	drmModeAddFB(dev->fd, dev->mode.hdisplay, dev->mode.vdisplay, 24, 32, dev->pitch, dev->handle, &dev->fb);		
	
	//Map the buffer so we can edit it 
	dev->buffer = gbm_bo_map(dev->bo, 0, 0, dev->width, dev->height, 
			GBM_BO_TRANSFER_READ_WRITE, &dev->pitch, (void **) &dev->map_data);
	
	if(dev->buffer == NULL) {
		logger_fatal("Failed to map gbm bo");
		drm_cleanup(dev);
		return NULL;
	}

	logger_info("Map Data: %p\nBuffer: %p", dev->map_data, dev->buffer);
	drmModeSetCrtc(dev->fd, dev->crtc->crtc_id, dev->fb, 0, 0, &dev->connector->connector_id, 1, &dev->mode);
	
	//Draw Gradient 
	for(int i = 0; i < dev->height; i++) {
		for(int j = 0; j < dev->width; j++) {
			uint8_t color = 0xff * (i * j) / (dev->height * dev->width);
			*(uint32_t *)(dev->buffer + i * (dev->pitch) + j * 4) = color | (color << 16);
		}
	}
	
	//Unmap the buffer 
	gbm_bo_unmap(dev->bo, (void *)dev->buffer);

	//Sleep for 4 seconds so we have a chance to see what's on 
	//screen
	sleep(4);
	
	//reset the original CRTC 
	drmModeSetCrtc(dev->fd, dev->crtc->crtc_id, dev->crtc->buffer_id, dev->crtc->x, dev->crtc->y, &dev->connector->connector_id, 1, &dev->crtc->mode);
	return dev;
}

int main(int argc, char **argv) {
	void *var = init_drm();

	if(var != NULL) drm_cleanup(var);
	return 0;
}
