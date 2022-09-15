#include "drm.h"
#include "drm_mode.h"
#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <xf86drm.h>
#include <xf86drmMode.h>
#include <stdlib.h>

#include <unistd.h>
#include <fcntl.h>

#include <sys/mman.h>
#include <getopt.h>

#include <cairo/cairo.h>


static int g_verbose = 0;
static bool g_master = false;
#define likely(x)       __builtin_expect((x),1)
#define unlikely(x)     __builtin_expect((x),0)

typedef struct bo {
	int fd;
	int handle;
	uint32_t height;
	uint32_t width;
	uint32_t pitch;
	uint32_t bpp;
	uint32_t id; 
	uint64_t size;
	off_t offset;
	void *buffer;
	cairo_surface_t *csurf;
} bo_t;

typedef struct outputs {
	drmModeCrtcPtr saved_crtc;
	drmModeConnectorPtr connector;
	drmModeEncoderPtr encoder;
	bo_t *bo;
} outputs_t;

typedef struct drm_backend { 
	int fd;
	drmModeResPtr res;
	drmModePlaneResPtr pres;
	outputs_t *outputs;
	int out_count;
} drm_backend_t;

typedef struct bo_creq {
	int fd; 
	uint32_t height;
	uint32_t width;
	uint32_t bpp;
	uint32_t flags;
} bo_creq_t;

void verbose(const char *fmt, ...) {
	switch(g_verbose) {
		case 1: {
			va_list args; 
			va_start(args, fmt);
			vfprintf(stderr, fmt, args);
		}
		default: 
			return;
	}
}
	
int bo_map(bo_t *bo) {
	struct drm_mode_map_dumb mreq = { 0 };
	mreq.handle = bo->handle;

	//Non of these should fail really if a correct bo type is passed in 
	//but best to just check them 
	if(drmIoctl(bo->fd, DRM_IOCTL_MODE_MAP_DUMB, &mreq) < 0) {
		return -1;
	}

	bo->offset = mreq.offset;
	bo->buffer = mmap(NULL, bo->size, PROT_READ | PROT_WRITE, MAP_SHARED, bo->fd, bo->offset);
	if(bo->buffer == MAP_FAILED) {
		return -2;
	}

	return 0;
}

bo_t *bo_create(bo_creq_t bo_info) {
	bo_t *bo = calloc(1, sizeof(*bo)); 
	if(bo == NULL) {
		NULL;
	}

	struct drm_mode_create_dumb creq = { 0 };
	creq.height = bo_info.height;
	creq.bpp = bo_info.bpp;
	creq.width = bo_info.width;
	creq.flags = bo_info.flags;

	if(drmIoctl(bo_info.fd, DRM_IOCTL_MODE_CREATE_DUMB, &creq) < 0) {
		NULL; 
	}

	bo->width = creq.width;
	bo->bpp = creq.bpp;
	bo->height = creq.height;
	bo->size = creq.size;
	bo->pitch = creq.pitch;
	bo->fd = bo_info.fd;
	bo->handle = creq.handle;
	
	return bo;
}

int bo_unmap(bo_t *bo) {
	if(bo->buffer == NULL) {
		return -1;
	}

	return munmap(bo->buffer, bo->size);
}

int bo_destroy(bo_t *bo) {
	struct drm_mode_destroy_dumb dreq = { 0 };
	dreq.handle = bo->handle;
	
	//This shouldn't really fail but just check in case the user has passed in a bad ptr 
	if(drmIoctl(bo->fd, DRM_IOCTL_MODE_DESTROY_DUMB, &dreq) < 0) {
		return -1;
	}
	
	//Free the bo 
	free(bo);
	return 0;
}

int drm_open(const char *path) {
	int fd = open(path, O_CLOEXEC | O_RDWR);
	//This shouldn't really happen and is a crash anyway 
	//so we want to optimise for the expected 
	if(unlikely(fd < 0)) {
		printf("Error Unable to open file %m\n");
		return -1;
	}

	int master = drmIsMaster(fd) | g_master;
	int kms = drmIsKMS(fd);
	/*
	 * And the results of these two together to check them as one result 
	 * as if either of these are zero the program should fail to continue
	 * with this device so better to perform one check than two 
	 */
	int ret = master & kms;
	switch(ret) {
		case 0: 
			//Print out info so the user can see more info in what
			//went wrong 
			printf("Error Not master of device/device is not KMS\
					\nKMS: %d\
					\nMASTER: %d\n", kms, master);
			close(fd);
			fd = -1;
	}
	return fd; 
}

drmModeEncoderPtr drm_get_encoder(int fd, drmModeConnectorPtr conn) {
	if(conn->encoder_id) {
		return drmModeGetEncoder(fd, conn->encoder_id);
	}
	return drmModeGetEncoder(fd, conn->encoders[0]);
}

drmModeCrtcPtr drm_get_crtc(int fd, uint32_t crtc_id) {
	if(crtc_id) {
		return drmModeGetCrtc(fd, crtc_id);
	}
	return NULL;
}

outputs_t *drm_get_outputs(int fd, drmModeResPtr res) {
	outputs_t *outs = calloc(res->count_connectors, sizeof(*outs));
	if(!outs) {
		printf("Error Failed to allocate outputs\n");
		return NULL;
	}

	for(int i = 0; i < res->count_connectors; i++) {
		outs[i].connector = drmModeGetConnector(fd, res->connectors[i]);
		
		if(outs[i].connector->connection == DRM_MODE_CONNECTED) {
			drmModeModeInfo mode = outs[i].connector->modes[0];
			bo_creq_t creq = { 0 };
			creq.fd = fd;
			creq.width = mode.hdisplay;
			creq.bpp = 32;
			creq.height = mode.vdisplay;
			outs[i].encoder = drm_get_encoder(fd, outs[i].connector);
			outs[i].saved_crtc = drm_get_crtc(fd, outs[i].encoder->crtc_id);
			outs[i].bo = bo_create(creq);
		}
	}

	return outs;
}

void putpixel(bo_t *bo, int x, int y, uint8_t color) {
	volatile uint8_t *buffer = (uint8_t *)bo->buffer;
	buffer[(x * 4) + (y * bo->pitch)] = color;
	buffer[((x * 4) + 1) + (y * bo->pitch)] = color;
	buffer[((x * 4) + 2) + (y * bo->pitch)] = color;
	buffer[((x * 4) + 3) + (y * bo->pitch)] = color;}

#define RED 0xff 
void drawCircle(bo_t *bo, int xc, int yc, int x, int y, uint8_t color)
{
    putpixel(bo, xc+x, yc+y, color);
    putpixel(bo, xc-x, yc+y, color);
    putpixel(bo, xc+x, yc-y, color);
    putpixel(bo, xc-x, yc-y, color);
    putpixel(bo, xc+y, yc+x, color);
    putpixel(bo, xc-y, yc+x, color);
    putpixel(bo, xc+y, yc-x, color);
    putpixel(bo, xc-y, yc-x, color);
}

void drw_circle(bo_t *bo, int xc, int yc, int radius, uint8_t color) {
	int x = 0;
	int y = radius;
	int d = 3 - 2 * radius;
	drawCircle(bo, xc, yc, x, y, color);
	while(x <= y) {
		x++;
		for(int yy = yc - y; yy < yc + y; yy++) {
			for(int xx = xc - x; xx < xc + x; xx++) {
				putpixel(bo, xx, yy, color);
			}
		}

		for(int yy = yc - x; yy < yc + x; yy++) {
			for(int xx = xc - y; xx < xc + y; xx++) {
				putpixel(bo, xx, yy, color);
			}
		}
		if(d > 0) { 
			y--;
	        d = d + 4 * (x - y) + 10;
		} else {
			d = d + 4 * x + 6;
		}
		drawCircle(bo, xc, yc, x, y, color);
	}
}


void draw(bo_t *bo, uint32_t height, uint32_t pitch) {
	for(int i = 0; i < height; i++) {
		for(int x = 2; x < pitch / 4; x++) {
			putpixel(bo, x, i, 0x28);
		}
	}
}

int drm_prepare_buffers(int fd, outputs_t *out, int connectors) {
	drmModeModeInfo mode;
	drmModeConnectorPtr conn; 
	bo_creq_t creq;

	printf("%d\n", connectors);
	for(int i = 0; i < connectors; i++) {
		printf("%p %p\n", out[i].connector, &out[i]);
		conn = out[i].connector;
		if(conn->connection == DRM_MODE_CONNECTED) {
			mode = conn->modes[0];
			creq.fd = fd;
			creq.bpp = 32;
			creq.flags = 0;
			creq.height = mode.vdisplay;
			creq.width = mode.hdisplay;
			out[i].bo = bo_create(creq);
			printf("BO: %p\n", out[i].bo);
			drmModeAddFB(out[i].bo->fd, mode.hdisplay, mode.vdisplay, 24, 32, out[i].bo->pitch, out[i].bo->handle, &out[i].bo->id);
			bo_map(out[i].bo);
			printf("Buffer: %p\n", out[i].bo->buffer);	
			
			out[i].bo->csurf = cairo_image_surface_create_for_data(out[i].bo->buffer, CAIRO_FORMAT_ARGB32, out[i].bo->width, out[i].bo->height, out[i].bo->pitch);
			drmModeSetCrtc(out[i].bo->fd, out[i].saved_crtc->crtc_id, out[i].bo->id, 0, 0, &out[i].connector->connector_id, 1, &out[i].connector->modes[0]);
			draw(out[i].bo, out[i].bo->height, out[i].bo->pitch);
			drw_circle(out[i].bo, 200, 200, 100, 0x00);
			drw_circle(out[i].bo, 200, 200, 90, 0xff);
			drw_circle(out[i].bo, 200, 200, 20, 0x00);
			drw_circle(out[i].bo, 400, 200, 100, 0x00); 
			drw_circle(out[i].bo, 400, 200, 90, 0xff); 
			drw_circle(out[i].bo, 400, 200, 20, 0x00);
			cairo_surface_write_to_png(out[i].bo->csurf, "./image.png");
			getchar();
			drmModeSetCrtc(out[i].bo->fd, out[i].saved_crtc->crtc_id, out[i].saved_crtc->buffer_id, out[i].saved_crtc->x, out[i].saved_crtc->y, &out[i].connector->connector_id, 1, &out[i].saved_crtc->mode);
		}
	}

	return 0;
}

int drm_init(const char *path) {
	drm_backend_t *backend = calloc(1, sizeof(*backend));
	
	//Get the file descriptor
	backend->fd = drm_open(path);
	if(backend->fd < 0) {
		return -1;
	}

	backend->res  = drmModeGetResources(backend->fd);
	backend->pres = drmModeGetPlaneResources(backend->fd);
	verbose("FD: %d\nRes: %p\nPres: %p", backend->fd,
			backend->res, backend->pres);
	
	backend->outputs = drm_get_outputs(backend->fd, backend->res);
	drm_prepare_buffers(backend->fd, backend->outputs, backend->res->count_connectors);
	return 0;
}

void usage(const char *progname) {
	printf("%s [-mvh] -p <PATH_TO_DRM_DEV>\n", progname);
	printf("Options:\n-h = prints this help message\
			\n-m = override drm master lock(may cause errors)\
			\n-v = verbose output\
			\n-p = provide path to drm device\n");
}

int main(int argc, char **argv) {
	int arg = 0;
	char *dev_path = "/dev/dri/card0"; //default
	
	while((arg = getopt(argc, argv, ":p:mvh")) != -1) {
		switch(arg) {
		case 'h': 
			usage(argv[0]);
			return 1;
		case 'p':
			dev_path = optarg;
			break;
		case 'm':
			//We want the master lock but allow the user
			//to override as it's useful for 
			g_master = 1;
			break;
		case 'v':
			g_verbose = 1;
			break;
		case ':':
			printf("-p requires an optional argument\n");
			return 1;
			break;
		case '?':
			printf("Unknown argument used -%c\n", optopt);
			return 1;
			break;
		}
	}
	
	drm_init(dev_path);
}
