#include <stdint.h>
#include <string.h>
#include <xf86drm.h>
#include <xf86drmMode.h>
#include <log.h>
#include <stdlib.h>

#include <sys/mman.h>

#include "./buffers.h"
#include "drm.h"
#include "drm_mode.h"

void buffer_destroy_dumb(bo_t *bo) {
	struct drm_mode_destroy_dumb dreq;
	memset(&dreq, 0, sizeof(dreq));

	dreq.handle = bo->handle;

}

bo_t *buffer_create_dumb(int fd, uint32_t bpp, uint32_t height, uint32_t width) {
	struct drm_mode_create_dumb creq;
	//Clear out creq 
	memset(&creq, 0, sizeof(creq));

	bo_t *bo = calloc(1, sizeof(*bo));
	if(!bo) {
		logger_error("Failed to allocate buffer %m");
		return NULL;
	}

	creq.bpp = bpp;
	creq.height = height;
	creq.width = width;

	if(drmIoctl(fd, DRM_IOCTL_MODE_CREATE_DUMB, &creq)) {
		logger_error("Failed to create dumb buffer %m");
		free(bo);
		return NULL;
	}

	bo->handle = creq.handle;
	bo->pitch = creq.pitch;
	bo->size = creq.size;
	bo->bpp = creq.bpp;
	bo->width = creq.width;
	bo->height = creq.height;

	return bo;
}

int buffer_map(int fd, bo_t *bo) {
	struct drm_mode_map_dumb mreq;
	//clear out mreq 
	memset(&mreq, 0, sizeof(mreq));

	mreq.handle = bo->handle; 
	if(drmIoctl(fd, DRM_IOCTL_MODE_MAP_DUMB, &mreq)) {
		logger_error("Failed to map buffer");
		return 1;
	}

	bo->offset = mreq.offset;
	bo->buffer = mmap(0, bo->size, PROT_READ | PROT_WRITE, MAP_SHARED, fd, bo->offset);
	if(bo->buffer == MAP_FAILED) {
		logger_error("Failed to map buffer");
		return 2;
	}

	return 0;
}

void buffer_unmap(bo_t *bo) {
	if(!bo->buffer) {
		return;
	}
	
	munmap(bo->buffer, bo->size);
	bo->buffer = NULL;
}
