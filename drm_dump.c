#include "drm_mode.h"
#include <stdint.h>
#include <unistd.h>
#include <log.h>
#include <fcntl.h>
#include <stdlib.h>
#include <xf86drm.h>
#include <xf86drmMode.h>
#include <libkms/libkms.h>

typedef struct drm_dev {
	int fd;

	drmModeResPtr res;
	drmModePlaneResPtr plane_res;
	drmModeConnectorPtr *connectors;

	drmModeEncoderPtr *encoders;

	int count_connectors; //Number of successfully recived connectors 
} drm_dev_t;

void drm_free(drm_dev_t *dev) {
	for(int i = 0; i < dev->count_connectors; i++) {
		drmModeFreeConnector(dev->connectors[i]);
	}
	
	free(dev->connectors);
	if(dev->res) {
		drmModeFreeResources(dev->res);
	}
	if(dev->fd >= 0) {
		close(dev->fd);
	}
	free(dev);
}

drm_dev_t *drm_init(const char *path) {
	drm_dev_t *dev = calloc(1, sizeof(*dev));
	if(dev == NULL) {
		logger_fatal("Error failed to allocate drm device");
		return NULL;
	}

	dev->fd = open(path, O_CLOEXEC | O_RDWR);
	if(dev->fd < 0) {
		free(dev);
		logger_fatal("Error opening drm device %s %m", path);
		return NULL;
	}

	dev->res = drmModeGetResources(dev->fd);
	if(dev->res == NULL) {
		logger_fatal("Error getting drm resources");
		close(dev->fd);
		free(dev);
		return NULL;
	}
		
	//Allocate space for pointers
	dev->connectors = calloc(dev->res->count_connectors, sizeof(drmModeConnectorPtr));
	for(int i = 0; i < dev->res->count_connectors; i++) {
		dev->connectors[dev->count_connectors] = drmModeGetConnector(dev->fd, dev->res->connectors[i]);
		if(dev->connectors == NULL) {
			//This shouldn't really ever happen but best to just error check it
			logger_warn("Failed to retrive connector from device with id %d", dev->res->connectors[i]);
			continue;
		}
		dev->count_connectors++;
	}
	
	dev->encoders = calloc(dev->res->count_encoders, sizeof(drmModeEncoderPtr));
	for(int i = 0; i < dev->res->count_encoders; i++) {
		dev->encoders[i] = drmModeGetEncoder(dev->fd, dev->res->encoders[i]);
		if(dev->encoders[i] == NULL) {
			logger_warn("Failed to get encoder for connector");
		}
	}
	return dev;
}

const char *get_drm_enc_type(uint32_t type) {
	switch(type) {
		case DRM_MODE_ENCODER_DPI:
			return "DPI";
		case DRM_MODE_ENCODER_LVDS:
			return "LVDS";
		case DRM_MODE_ENCODER_TMDS:
			return "TMDS";
		case DRM_MODE_ENCODER_TVDAC:
			return "TVDAC";
		case DRM_MODE_ENCODER_NONE:
		default:
			return "NONE";
	}
}

void dump_drm_encoder(drmModeEncoderPtr enc) {
	logger_info("%03d \t%s \t%d \t%d \t%d", enc->encoder_id, 
			get_drm_enc_type(enc->encoder_type), enc->crtc_id, 
			enc->possible_crtcs, enc->possible_clones);
}

void dump_drm_encoders(drm_dev_t *dev) {
	logger_info("ENC:\tTYPE:\tCRTC:\tPOSS:\tCLONES:"); 
	for(int i = 0; i < dev->res->count_encoders; i++) {
		dump_drm_encoder(dev->encoders[i]);
	}
}

void dump_drm_res(drmModeResPtr res) {
	logger_info("DRM Resources:\n\
					Connectors: %d\n\
					CRTCS: %d\n\
					Encoders: %d\n\
					FBS: %d\n\
					Max Height: %d\n\
					Max Width: %d\n\
					Min Height: %d\n\
					Min Width: %d",
			res->count_connectors, 
			res->count_crtcs, 
			res->count_encoders, 
			res->count_fbs,
			res->max_height,
			res->max_width,
			res->min_height, 
			res->min_width);
}

const char *connector_get_connection(drmModeConnection connection) {
	switch(connection) {
		case DRM_MODE_CONNECTED:
			return "Connected";
		case DRM_MODE_DISCONNECTED:
			return "Disconnected";
		case DRM_MODE_UNKNOWNCONNECTION:
		default:
			return "Unknown";
	}
}

const char *connector_get_type(uint32_t type) {
	switch(type) {
		case DRM_MODE_CONNECTOR_VGA:
			return "VGA";
		case DRM_MODE_CONNECTOR_DisplayPort:
			return "DP";
		case DRM_MODE_CONNECTOR_USB:
			return "USB";
		case DRM_MODE_CONNECTOR_9PinDIN:
			return "9PinDIN";
		case DRM_MODE_CONNECTOR_Component:
			return "Component";
		case DRM_MODE_CONNECTOR_DPI:
			return "DPI";
		case DRM_MODE_CONNECTOR_DSI:
			return "DSI";
		case DRM_MODE_CONNECTOR_DVIA:
			return "DVI-A";
		case DRM_MODE_CONNECTOR_DVID:
			return "DVI-D";
		case DRM_MODE_CONNECTOR_DVII:
			return "DVI-I";
		case DRM_MODE_CONNECTOR_eDP:
			return "eDP";
		case DRM_MODE_CONNECTOR_SVIDEO:
			return "SVideo";
		case DRM_MODE_CONNECTOR_WRITEBACK:
			return "Writeback";
		case DRM_MODE_CONNECTOR_VIRTUAL:
			return "Virtual";
		case DRM_MODE_CONNECTOR_Composite: 
			return "Composite";
		case DRM_MODE_CONNECTOR_SPI:
			return "SPI";
		case DRM_MODE_CONNECTOR_TV: 
			return "TV";
		case DRM_MODE_CONNECTOR_LVDS:
			return "LVDS";
		case DRM_MODE_CONNECTOR_HDMIA:
			return "HDMI-A";
		case DRM_MODE_CONNECTOR_HDMIB:
			return "HDMI-B";
		case DRM_MODE_CONNECTOR_Unknown:
		default:
			return "Unknown";
	}
}

void dump_drm_connectors_name(drm_dev_t *dev) {
	for(int i = 0; i < dev->count_connectors; i++) {
		logger_info("Connector %d: %s-%d (%s)", i, connector_get_type(dev->connectors[i]->connector_type),
						dev->connectors[i]->connector_type_id, connector_get_connection(dev->connectors[i]->connection));
	}
}

void dump_drm_connector(drmModeConnectorPtr conn) {
	
}

void drm_setup_fb(drm_dev_t *dev) {
	struct drm_mode_create_dumb creq = { 0 };
	struct drm_mode_map_dumb mreq = { 0 };
	
	for(int i = 0; i < dev->count_connectors; i++) {
		
	}
	

	return;
}

int main(int argc, char **argv) {
	drm_dev_t *dev = drm_init("/dev/dri/card0");

	dump_drm_res(dev->res);
	dump_drm_connectors_name(dev);

	dump_drm_encoders(dev);	

	drm_free(dev);
}
