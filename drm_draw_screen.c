#include "drm.h"
#include "drm_mode.h"
#include <stdint.h>
#include <xf86drm.h>
#include <xf86drmMode.h>

#include <unistd.h>
#include <fcntl.h>

#include <stdlib.h>
#include <log.h>

#include <sys/mman.h>

typedef struct output {
	drmModeConnectorPtr connector;
	drmModeCrtcPtr new_crtc;
	drmModeEncoderPtr encoder;
	drmModeCrtcPtr saved_crtc;
	uint32_t *buffer;
	uint32_t fb_id;
	drmModeModeInfo mode; 
}output_t;



typedef struct drm {
	int fd;
	output_t out;
	drmModeResPtr res;
}drm_t;

drm_t *init_drm() {
	drm_t *dev = calloc(1, sizeof(*dev));

	if(!dev) {
		return NULL;
	}

	dev->fd = open("/dev/dri/card0", O_CLOEXEC | O_RDWR);
	if(dev->fd < 0) {
		free(dev);
		return NULL;
	}

	dev->res = drmModeGetResources(dev->fd);
	if(!dev->res) {
		close(dev->fd);
		free(dev);
		return NULL;
	}

	for(int i = 0; i < dev->res->count_connectors; i++) {
		dev->out.connector = drmModeGetConnector(dev->fd, dev->res->connectors[i]);
		if(dev->out.connector == NULL) {
			continue; 
		} else if(dev->out.connector->connection != DRM_MODE_CONNECTED) {
			drmModeFreeConnector(dev->out.connector);
			dev->out.connector = NULL;
			continue;
		}
		break;
	}

	if(dev->out.connector == NULL) {
		logger_fatal("Failed to obtain connector");
		drmModeFreeResources(dev->res);
		close(dev->fd);
		free(dev);
		return NULL;
	}

	dev->out.encoder = drmModeGetEncoder(dev->fd, dev->out.connector->encoder_id);
	if(dev->out.encoder == NULL) {
		logger_fatal("Failed to get Encoder");
	}

	dev->out.saved_crtc = drmModeGetCrtc(dev->fd, dev->out.encoder->crtc_id);
	if(dev->out.saved_crtc == NULL) { 
		logger_fatal("Failed to get CRTC");
	}
	dev->out.mode = dev->out.connector->modes[0];

	struct drm_mode_create_dumb creq = { 0 };
	struct drm_mode_map_dumb mreq = { 0 };
	
	creq.bpp = 32;
	creq.height = dev->out.mode.vdisplay;
	creq.width = dev->out.mode.hdisplay;

	if(drmIoctl(dev->fd, DRM_IOCTL_MODE_CREATE_DUMB, &creq) < 0) {
		logger_fatal("Failed to create DRM Dumb buffer");
		return NULL;
	}

	if(drmModeAddFB(dev->fd, creq.width, creq.height, 24, 32, creq.pitch, creq.handle, &dev->out.fb_id)) {
		logger_fatal("Failed to add DRM FB");
		return NULL; 
	}

	mreq.handle = creq.handle;
	if(drmIoctl(dev->fd, DRM_IOCTL_MODE_MAP_DUMB, &mreq)) {
		return NULL;
	}

	dev->out.buffer = (uint32_t *) mmap(0, creq.size, PROT_READ | PROT_WRITE, MAP_SHARED, dev->fd, mreq.offset);
	
	if(drmModeSetCrtc(dev->fd, dev->out.saved_crtc->crtc_id, dev->out.fb_id, 0, 0, &dev->out.connector->connector_id, 1, &dev->out.mode)) {
		logger_fatal("Failed to set CRTC: %m");
	}
	for (int i = 0; i < creq.height; i++) {
		for (int j = 0; j < creq.width; j++) {
			uint8_t color = (double) (i * j) / (creq.height * creq.width) * 0xFF;
			*(dev->out.buffer + i * creq.width + j) = (uint32_t) 0xFFFFFF & (0x00 << 16 | color << 8 | color);
		}
	}

	sleep(10);

	if(drmModeSetCrtc(dev->fd, dev->out.saved_crtc->crtc_id, dev->out.saved_crtc->buffer_id, dev->out.saved_crtc->x, dev->out.saved_crtc->y, &dev->out.connector->connector_id, 1, &dev->out.saved_crtc->mode)) {
		logger_fatal("Failed to reset CRTC: %m");
	}
	return dev;
}

int main(int argc, char **argv) {
	init_drm();	
	return 0;
}
