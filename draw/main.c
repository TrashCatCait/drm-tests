#include "drm.h"
#include <stdio.h>
#include <xf86drm.h>
#include <xf86drmMode.h>
#include <stdlib.h>

#include <unistd.h>
#include <fcntl.h>

#include <getopt.h>

#define likely(x)       __builtin_expect((x),1)
#define unlikely(x)     __builtin_expect((x),0)

typedef struct outputs {
	drmModeCrtc saved_crtc;
	drmModeConnectorPtr connector;
	drmModeEncoderPtr encoder;
} outputs_t;

typedef struct drm_backend { 
	int fd;
	drmModeResPtr res;
	drmModePlaneResPtr pres;
	outputs_t *outputs;
	int out_count;
} drm_backend_t;

int drm_open(const char *path) {
	int fd = open(path, O_CLOEXEC | O_RDWR);
	//This shouldn't really happen and is a crash anyway 
	//so we want to optimise for the expected 
	if(unlikely(fd < 0)) {
		printf("Error Unable to open file %m\n");
		return -1;
	}

	int master = drmIsMaster(fd);
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

int drm_get_outputs(drm_backend_t *backend) {
	drmModeResPtr res = backend->res;
	for(int i = 0; i < res->count_connectors; i++) {
		drmModeConnectorPtr connector = drmModeGetConnector(backend->fd, res->connectors[i]);
		if(unlikely(!connector)) {
			printf("Unable to get drm connector\n");
			continue;
		}
		switch(connector->connection) {
			case DRM_MODE_CONNECTED:
				backend->out_count++;
			default:
				drmModeFreeConnector(connector);
		}
	}

	//Allocate output 
	backend->outputs = calloc(backend->out_count, sizeof(outputs_t));
	if(!backend->outputs) {
		printf("Error allocating outputs\n");
		return -1;
	}

	for(int i = 0; i < res->count_connectors; i++) {
		drmModeConnectorPtr connector = drmModeGetConnector(backend->fd, res->connectors[i]);
		int connected = 0;
		if(unlikely(!connector)) {
			printf("Unable to get drm connector\n");
			continue;
		}
		switch(connector->connection) {
			case DRM_MODE_CONNECTED:
				backend->outputs[connected].connector = connector;
				connected++;
				break;
			default:
				drmModeFreeConnector(connector);
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
	
	drm_get_outputs(backend);
	for(int i = 0; i < backend->out_count; i++) {
		printf("%d %x\n", backend->res->count_crtcs, drmModeConnectorGetPossibleCrtcs(backend->fd, backend->outputs[i].connector));
	}
	
	return 0;
}

int main(int argc, char **argv) {
	int arg = 0;
	char *dev_path = "/dev/dri/card0"; //default
	
	while((arg = getopt(argc, argv, ":p:")) != -1) {
		switch(arg) {
		case 'p':
			dev_path = optarg;
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
