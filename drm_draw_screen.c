#include "drm.h"
#include "drm_mode.h"
#include <stddef.h>
#include <stdint.h>
#include <xf86drm.h>
#include <xf86drmMode.h>

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
	output_t out;
	drmModeResPtr res;
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
	if(dev->out.buffer) {
		munmap(dev->out.buffer, dev->out.buffer_size);	
	}

	if(dev->out.saved_crtc) {
		drmModeFreeCrtc(dev->out.saved_crtc);
	}

	if(dev->out.encoder) {
		drmModeFreeEncoder(dev->out.encoder);
	}

	if(dev->out.connector) {
		drmModeFreeConnector(dev->out.connector);
	}

	if(dev->res) {
		drmModeFreeResources(dev->res);
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
	
	//TODO: Functionise this drm init stuff 
	dev->res = drmModeGetResources(dev->fd);
	if(!dev->res) {
		drm_cleanup(dev);
		return NULL;
	}

	//TODO allow multiple monitors to be displayed to as at the moment it's 
	//just the first connected monitor 
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
		drm_cleanup(dev);
		return NULL;
	}

	dev->out.encoder = drmModeGetEncoder(dev->fd, dev->out.connector->encoder_id);
	if(dev->out.encoder == NULL) {
		logger_fatal("Failed to get Encoder");
		drm_cleanup(dev);
		return NULL;
	}

	dev->out.saved_crtc = drmModeGetCrtc(dev->fd, dev->out.encoder->crtc_id);
	if(dev->out.saved_crtc == NULL) { 
		logger_fatal("Failed to get CRTC");
		drm_cleanup(dev);
		return NULL;
	}

	/*TODO: Maybe don't just take the first mode and look for preffered mode
	 * also maybe check if there are any modes (there always should be but best to check 
	 */
	/*TODO: Move this code to it's own function*/
	dev->out.mode = dev->out.connector->modes[0];

	struct drm_mode_create_dumb creq = { 0 };
	struct drm_mode_map_dumb mreq = { 0 };
	
	creq.bpp = 32;
	creq.height = dev->out.mode.vdisplay;
	creq.width = dev->out.mode.hdisplay;

	if(drmIoctl(dev->fd, DRM_IOCTL_MODE_CREATE_DUMB, &creq) < 0) {
		logger_fatal("Failed to create DRM Dumb buffer");
		drm_cleanup(dev);
		return NULL;
	}
	

	if(drmModeAddFB(dev->fd, creq.width, creq.height, 24, 32, creq.pitch, creq.handle, &dev->out.fb_id)) {
		logger_fatal("Failed to add DRM FB");
		drm_cleanup(dev);
		return NULL; 
	}

	mreq.handle = creq.handle;
	if(drmIoctl(dev->fd, DRM_IOCTL_MODE_MAP_DUMB, &mreq)) {
		logger_fatal("Failed to map dumb buffer");
		drm_cleanup(dev);
		return NULL;
	}

	dev->out.buffer_size = creq.size; //save this for freeing later
	dev->out.buffer = (uint32_t *) mmap(0, creq.size, PROT_READ | PROT_WRITE, MAP_SHARED, dev->fd, mreq.offset);
	if(dev->out.buffer == MAP_FAILED) {
		logger_fatal("Failed to memory map code into user space");
		drm_cleanup(dev);
		return NULL;
	}

	if(drmModeSetCrtc(dev->fd, 
				dev->out.saved_crtc->crtc_id, dev->out.fb_id, 0, 0, 
				&dev->out.connector->connector_id, 1, &dev->out.mode)) {
		logger_fatal("Failed to set CRTC: %m");
		drm_cleanup(dev);
		return NULL;
	}
	

	//TODO Draw something not sure what yet maybe try like a PNG though because that 
	//would be cool
	for (int i = 0; i < creq.height; i++) {
		for (int j = 0; j < creq.width; j++) {
			uint8_t color = (double) (i * j) / (creq.height * creq.width) * 0xFF;
			*(dev->out.buffer + i * creq.width + j) = (uint32_t) 0xFFFFFF & (0x00 << 16 | color << 8 | color);
		}
	}

	//TODO maybe try implementing some sort of crash protection because atm if we crash the TTY just dies with us
	sleep(10);

	if(drmModeSetCrtc(dev->fd, dev->out.saved_crtc->crtc_id, dev->out.saved_crtc->buffer_id, dev->out.saved_crtc->x, dev->out.saved_crtc->y, &dev->out.connector->connector_id, 1, &dev->out.saved_crtc->mode)) {
		logger_fatal("Failed to reset CRTC: %m");
		drm_cleanup(dev);
		return NULL;
	}
	return dev;
}

int main(int argc, char **argv) {
	void *var = init_drm();

	if(var != NULL) drm_cleanup(var);
	return 0;
}
