#include "drm_mode.h"
#include <fcntl.h>
#include <stdint.h>
#include <stdio.h>
#include <unistd.h>

#include <xf86drm.h>
#include <xf86drmMode.h>

#include <stdlib.h>

#include <log.h>

typedef struct device {
	int fd;

	drmModeResPtr res;
	drmModePlaneResPtr plane_res;
	drmModeConnectorPtr connectors; 
	
}device_t;

//Turn the connection status into a nice looking string
static const char *drm_get_conn(drmModeConnection connection) {
	switch(connection) {
		case DRM_MODE_DISCONNECTED:
			return "Disconected";
		case DRM_MODE_CONNECTED:
			return "Connected";
		case DRM_MODE_UNKNOWNCONNECTION:
		default:
			return "Unknown";
	}

}

//return the drm connector type as a string 
static const char *drm_get_connector_type(uint32_t type) {
	switch(type) {
		case DRM_MODE_CONNECTOR_VGA:
			return "VGA";
		case DRM_MODE_CONNECTOR_DVII: 
			return "DVI-I";
		case DRM_MODE_CONNECTOR_DVID:
			return "DVI-D";
		case DRM_MODE_CONNECTOR_DVIA:
			return "DVI-A";
		case DRM_MODE_CONNECTOR_Composite:
			return "Composite";
		case DRM_MODE_CONNECTOR_9PinDIN:
			return "9-pin DIN";
		case DRM_MODE_CONNECTOR_DisplayPort: 
			return "DisplayPort";
		case DRM_MODE_CONNECTOR_HDMIA:
			return "HDMI-A";
		case DRM_MODE_CONNECTOR_HDMIB:
			return "HDMI-B";
		case DRM_MODE_CONNECTOR_TV:
			return "TV";
		case DRM_MODE_CONNECTOR_eDP:
			return "eDP";
		case DRM_MODE_CONNECTOR_VIRTUAL:
			return "Virtual";
		case DRM_MODE_CONNECTOR_DSI:
			return "DSI";
		case DRM_MODE_CONNECTOR_Unknown:
		default:
			return "Unknown";
	}
}

int main(int argc, char **argv) {
	int fd = open("/dev/dri/card0", O_RDWR | O_CLOEXEC);
	if(fd < 0) {
		logger_fatal("Error Opening Card ");
		return 1;
	}

	drmSetClientCap(fd, DRM_CLIENT_CAP_UNIVERSAL_PLANES, 1);

	drmModePlaneResPtr planes_res = drmModeGetPlaneResources(fd);
	drmModeResPtr resources = drmModeGetResources(fd); 
	if(resources == NULL || planes_res == NULL) {
		logger_fatal("Failed to get drm resources");
		close(fd);
		return 1;
	}
	logger_debug("%d", planes_res->count_planes);
	logger_info("Got Drm Resources %d connctors detected", resources->count_connectors);
	
	drmModeConnectorPtr *connectors = calloc(resources->count_connectors, sizeof(drmModeConnectorPtr));
	uint32_t size = 0;
	if(connectors == NULL) {
		drmModeFreeResources(resources);
		close(fd);
		return 1;
	}
	
	drmModePlanePtr *planes = calloc(planes_res->count_planes, sizeof(drmModePlane));
	if(planes == NULL) {
		drmModeFreeResources(resources);
		close(fd);
		return 1;
	}

	for(int i = 0; i < planes_res->count_planes; i++) {
		planes[i] = drmModeGetPlane(fd, planes_res->planes[i]);
		logger_info("ID: %d, CRTC: %d, CRTC_X: %d, CRTC_Y: %d, X: %d Y: %d\nFormat Count: %d, FB: %d, GAMMA Size: %d CRTCS: %d\
				",
				planes[i]->plane_id, planes[i]->crtc_id, planes[i]->crtc_x, 
				planes[i]->crtc_y, planes[i]->x, planes[i]->y, planes[i]->count_formats,
				planes[i]->fb_id, planes[i]->gamma_size, planes[i]->possible_crtcs);	
		for(int j = 0; j < planes[i]->count_formats; j++) {
			logger_info("%4.4s", &planes[i]->formats[j]);
		}
	}


	for(uint32_t i = 0; i < resources->count_connectors; i++) {
		connectors[size] = drmModeGetConnector(fd, resources->connectors[i]);
		if(connectors[size] == NULL) {
			logger_warn("Failed to get connector for connector id %d", resources->connectors[i]);
			continue;
		}
		size++;
	}
	
	for(int i = 0; i < size; i++) {
		logger_info("%s-%d (%s) modes: %d", drm_get_connector_type(connectors[i]->connector_type), 
				connectors[i]->connector_type_id, drm_get_conn(connectors[i]->connection), connectors[i]->count_modes);
		
		if(connectors[i]->connection != DRM_MODE_CONNECTED || connectors[i]->count_modes < 1) {
			logger_info("Monitor not connected or has no modes skipping");
			continue; 
		}

		for(int j = 0; j < connectors[i]->count_modes; j++) {
			drmModeModeInfo mode = connectors[i]->modes[j];
			logger_info("MODE: %dx%d@%d", mode.hdisplay, mode.vdisplay, mode.vrefresh);
		}
	}
	
	for(int i = 0; i < size; i++) {
		drmModeFreeConnector(connectors[i]);
	}
	free(connectors);
	drmModeFreeResources(resources);	
	close(fd);
	return 0;
}
