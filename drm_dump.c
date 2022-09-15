/* 
 * Program: drm_dump 
 *
 * basic drm util to dump out details of device
 *
 * @Authors: Caitcat
 *
 */

#include "drm.h"
#include "drm_mode.h"
#include <fcntl.h>
#include <stdint.h>
#include <unistd.h>
#include <log.h>
#include <xf86drm.h>
#include <xf86drmMode.h>
#include <stdlib.h>
#include <stdio.h>
#include <errno.h>

typedef struct connector {
	drmModeConnectorPtr conn;
	drmModeEncoderPtr enc; //The used Encoder for this connector 
	drmModeCrtcPtr crtc; //The used CRTC for this connector 
	drmModePropertyRes **props;
} connector_t;

typedef struct drm_dev {
	int fd;
	
	drmModeResPtr res;
	drmModePlaneResPtr pres;
	drmModePlanePtr *planes;
	drmModeEncoderPtr *encoders;
	drmModeCrtcPtr *crtcs;
	connector_t *connector;
	drmVersionPtr ver;
} drm_dev_t;

//Cleanup the drm device
void drm_clean_up(drm_dev_t *dev) {
	
	for(int i = 0; i < dev->res->count_connectors; i++) {
		drmModeFreeEncoder(dev->connector[i].enc);
		drmModeFreeCrtc(dev->connector[i].crtc);
		
		for(int j = 0; j < dev->connector[i].conn->count_props; j++) {
			drmModeFreeProperty(dev->connector[i].props[j]);
		}
		free(dev->connector[i].props);

		drmModeFreeConnector(dev->connector[i].conn);
	}

	free(dev->connector);
	

	for(int i = 0; i < dev->pres->count_planes; i++) {
		drmModeFreePlane(dev->planes[i]);
	}
	free(dev->planes);

	for(int i = 0; i < dev->res->count_encoders; i++) {
		drmModeFreeEncoder(dev->encoders[i]);
	}
	free(dev->encoders);

	for(int i = 0; i < dev->res->count_crtcs; i++) {
		drmModeFreeCrtc(dev->crtcs[i]);
	}
	free(dev->crtcs);

	drmModeFreeResources(dev->res);

	drmModeFreePlaneResources(dev->pres);
	
	drmFreeVersion(dev->ver);

	if(dev->fd >= 0) {
		close(dev->fd);
	}

	if(dev) {
		free(dev);
	}
}

int drm_open(const char *path, uint64_t capget) {
	int fd;
	int ret;
	uint64_t hascap;

	fd = open(path, O_CLOEXEC | O_RDWR);

	if(fd < 0) {
		logger_warn("Failed to open drm device %m");
		return -1;
	}

	ret = drmGetCap(fd, capget, &hascap);
	if(ret < 0 || hascap != capget) {
		logger_warn("Drm device doesn't support requested capablities %m");
		close(fd);
		return -2;
	}
	
	return fd;
}

/*
 * TODO: add more error checking as some of these calls can fail 
 * Though that's very unlikely so for now we just assume they succeeded
 */
drm_dev_t *drm_init(const char *dev_path, uint64_t caps) {
	drm_dev_t *dev = calloc(1, sizeof(*dev));
	if(!dev) {
		logger_fatal("Failed to allocate device %m");
		return NULL;
	}

	dev->fd = drm_open(dev_path, DRM_CAP_DUMB_BUFFER);
	if(dev->fd < 0) {
		drm_clean_up(dev);
		return NULL;
	}

	drmSetClientCap(dev->fd, DRM_CLIENT_CAP_UNIVERSAL_PLANES, 1);
	
	dev->ver = drmGetVersion(dev->fd);
	
	dev->res = drmModeGetResources(dev->fd);
	if(!dev->res) {
		logger_fatal("Failed to obtain drm resources");
		drm_clean_up(dev);
		return NULL;
	}

	dev->pres = drmModeGetPlaneResources(dev->fd);
	if(!dev->pres) {
		logger_fatal("Failed to obtain plane resources");
		drm_clean_up(dev);
		return NULL;
	}
	

	dev->planes = calloc(dev->pres->count_planes, sizeof(drmModePlane));
	for(int i = 0; i < dev->pres->count_planes; i++) {
		dev->planes[i] = drmModeGetPlane(dev->fd, dev->pres->planes[i]);
	}

	dev->connector = calloc(dev->res->count_connectors, sizeof(connector_t));
	
	for(int i = 0; i < dev->res->count_connectors; i++) {
		dev->connector[i].conn = drmModeGetConnector(dev->fd, dev->res->connectors[i]);
		if(!dev->connector[i].conn) {
			//This really shouldn't happen like ever 
			logger_warn("Failed to get connector %d %d %m", dev->res->connectors[i], i);
			continue;
		}
		
		dev->connector[i].enc = drmModeGetEncoder(dev->fd, dev->connector[i].conn->encoder_id);
		
		if(dev->connector[i].enc) {
			dev->connector[i].crtc = drmModeGetCrtc(dev->fd, dev->connector[i].enc->crtc_id);
		}
		
		dev->connector[i].props = calloc(dev->connector[i].conn->count_props, sizeof(drmModePropertyRes*));
		for(int j = 0; j < dev->connector[i].conn->count_props; j++) {
			dev->connector[i].props[j] = drmModeGetProperty(dev->fd, dev->connector->conn->props[j]);
			if(!dev->connector->props[j]) {
				logger_warn("Failed to get property");
			}
		}
	}

	
	dev->encoders = calloc(dev->res->count_encoders, sizeof(drmModeEncoderPtr));
	for(int i = 0; i < dev->res->count_encoders; i++) {
		dev->encoders[i] = drmModeGetEncoder(dev->fd, dev->res->encoders[i]);
		if(!dev->encoders[i]) {
			logger_warn("Failed to get encoder %d %d %m", i, dev->res->encoders[i]);
		}
	}
		
	dev->crtcs = calloc(dev->res->count_crtcs, sizeof(drmModeCrtcPtr));
	for(int i = 0; i < dev->res->count_crtcs; i++) {
		dev->crtcs[i] = drmModeGetCrtc(dev->fd, dev->res->crtcs[i]);
		if(!dev->crtcs[i]) {
			logger_warn("Failed to get crtc %d %d %m", i, dev->res->crtcs[i]);
		}
	}
	
	return dev;
}

const char *connector_get_connection_str(uint32_t connection) {
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

const char *connector_get_type_str(uint32_t type) {
	switch(type) {
		case DRM_MODE_CONNECTOR_VGA:
			return "VGA";
		case DRM_MODE_CONNECTOR_HDMIA:
			return "HDMI-A";
		case DRM_MODE_CONNECTOR_HDMIB:
			return "HDMI-B";
		case DRM_MODE_CONNECTOR_DisplayPort:
			return "DP";
		case DRM_MODE_CONNECTOR_eDP:
			return "eDP";
		case DRM_MODE_CONNECTOR_VIRTUAL:
			return "Virtual";
		case DRM_MODE_CONNECTOR_9PinDIN:
			return "9PinDIN";
		case DRM_MODE_CONNECTOR_Component:
			return "Component";
		case DRM_MODE_CONNECTOR_Composite:
			return "Composite";
		case DRM_MODE_CONNECTOR_WRITEBACK:
			return "Writeback";
		case DRM_MODE_CONNECTOR_USB:
			return "USB";
		case DRM_MODE_CONNECTOR_DPI:
			return "DPI";
		case DRM_MODE_CONNECTOR_TV:
			return "TV";
		case DRM_MODE_CONNECTOR_SPI:
			return "SPI";
		case DRM_MODE_CONNECTOR_DSI:
			return "DSI";
		case DRM_MODE_CONNECTOR_SVIDEO:
			return "SVideo";
		case DRM_MODE_CONNECTOR_LVDS:
			return "LVDS";
		case DRM_MODE_CONNECTOR_DVII:
			return "DVI-I";
		case DRM_MODE_CONNECTOR_DVIA:
			return "DVI-A";
		case DRM_MODE_CONNECTOR_DVID:
			return "DVI-D";
		case DRM_MODE_CONNECTOR_Unknown:
		default:
			return "Unknown";
	}
}

const char *drm_encoder_get_type(uint32_t type) {
	switch(type) {
		case DRM_MODE_ENCODER_DAC: 
			return "DAC";
		case DRM_MODE_ENCODER_DPI:
			return "DPI";
		case DRM_MODE_ENCODER_DPMST:
			return "DPMST";
		case DRM_MODE_ENCODER_DSI:
			return "DSI";
		case DRM_MODE_ENCODER_LVDS:
			return "LVDS";
		case DRM_MODE_ENCODER_TMDS:
			return "TMDS";
		case DRM_MODE_ENCODER_TVDAC:
			return "TVDAC";
		case DRM_MODE_ENCODER_VIRTUAL:
			return "Virtual";
		case DRM_MODE_ENCODER_NONE:
			return "None";
		default:
			return "Unknown";
	}
}

void drm_dump_encoder(drmModeEncoderPtr enc) {
	if(enc) {
		logger_info("%-7d | %-7s | %-7d | %-7d", enc->encoder_id, drm_encoder_get_type(enc->encoder_type), enc->possible_crtcs, enc->possible_clones);
	}
}

void drm_dump_crtc(drmModeCrtcPtr crtc) {
	if(crtc) {
		logger_info("%-8d | %-8d | %-9d | %-7d | %-7d | %-2d | %-6d | %-6d", crtc->crtc_id, crtc->buffer_id, crtc->gamma_size, crtc->height, crtc->width, crtc->mode_valid, crtc->x, crtc->y);
	}
}

void drm_dump_prop(drmModePropertyRes *prop) {
	logger_info("%s", prop->count_enums);
}

void drm_dump_connector(connector_t connector) {
	//Avoid having to type connector-> when referncing data
	drmModeConnectorPtr conn = connector.conn;
	drmModeEncoderPtr enc = connector.enc; 
	drmModeCrtcPtr crtc = connector.crtc;
	drmModePropertyRes **props = connector.props;

	logger_info("%s-%d (%s):", 
			connector_get_type_str(conn->connector_type), conn->connector_type_id, connector_get_connection_str(conn->connection));
	logger_info("\t%-6s | %-6s | %-12s | %-4s | %-4s", 
			"Modes:", "Props:", "Encoders:", "mmH:", "mmW");
	logger_info("\t%-6d | %-6d | %-12d | %-4d | %-4d\n", 
			conn->count_modes, conn->count_props, conn->count_encoders, conn->mmHeight, conn->mmWidth);
	//If this connector has an encoder 
	if(enc) {
		logger_info("\t%-7s | %-7s | %-7s | %-7s", "ENC ID:", "Type:", "CRTCs:", "Clones:");
		logger_info("\t%-7d | %-7s | %-7d | %-7d\n", enc->encoder_id, drm_encoder_get_type(enc->encoder_type), enc->possible_crtcs, enc->possible_clones);
	}

	//If this connector's encoder has a crtc
	if(crtc) {
		logger_info("\t%-8s | %-8s | %-9s | %-7s | %-7s | %-2s | %-6s | %-6s", "CRTC ID:", "FBUF ID:", "Gamma Sz:", "Height:", "Width:", "V:", "Xpos:", "Ypos:");
		logger_info("\t%-8d | %-8d | %-9d | %-7d | %-7d | %-2d | %-6d | %-6d\n", crtc->crtc_id, crtc->buffer_id, crtc->gamma_size, crtc->height, crtc->width, crtc->mode_valid, crtc->x, crtc->y);
	}
	
	logger_info("Properties:");
	for(int i = 0; i < conn->count_props; i++) {
		
		logger_info("\t%d %s %d %d %d %d", props[i]->prop_id, props[i]->name, props[i]->count_enums, props[i]->count_blobs, props[i]->count_values, props[i]->flags);
		for(int j = 0; j < props[i]->count_enums; j++) {
			logger_info("\t\t%s %lu", props[i]->enums[j].name, props[i]->enums[j].value);
		}
	}
}


void drm_dump_planes(int fd, drmModePlanePtr plane) {
	logger_info("%-9s | %-6s | %-9s | %-8s | %-6s | %-9s | %-5s | %-5s |", "Plane ID:", "FB ID:", "Formats:", "CRTC ID:", "CRTCs:", "Gamma Sz:", "Xpos:", "Ypos:");
	logger_info("%-9d | %-6d | %-9d | %-8d | %-6d | %-9d | %-5d | %-5d | %d | %d", 
			plane->plane_id, plane->fb_id, plane->count_formats, plane->crtc_id,
			plane->possible_crtcs, plane->gamma_size, plane->x, plane->y, 
			plane->crtc_x, plane->crtc_y);
	
	drmModeObjectPropertiesPtr objs = drmModeObjectGetProperties(fd, plane->plane_id, DRM_MODE_OBJECT_PLANE);
	for(int i = 0; i < objs->count_props; i++) {
		drmModePropertyPtr prop = drmModeGetProperty(fd, objs->props[i]);
		logger_info("%s %lu %lu %lu",prop->name, prop->count_values, objs->count_props > i ? objs->prop_values[i] : 0, DRM_PLANE_TYPE_CURSOR);
		
		drmModeFreeProperty(prop);
	}

	logger_info("Formats:");	
	for(int i = 0; i < plane->count_formats; i++) {
		logger_info("%.4s, ", (char *)&plane->formats[i]);
	}
	fprintf(stderr, "\n");
}

void drm_dump_resources(drmModeResPtr res) {
	logger_info("%-11s | %-6s | %-12s | %-4s | %-5s | %-5s | %-5s | %-5s", 
			"Connectors:", "CRTCS:", "Encoders:", "FBs:", "MaxW:", 
			"MaxH:", "MinW:", "MinH:");	
	logger_info("%-11d | %-6d | %-12d | %-4d | %-5d | %-5d | %-5d | %-5d", res->count_connectors, res->count_crtcs, 
			res->count_encoders, res->count_fbs, res->max_width, res->max_height, res->min_width, res->min_height);
}

void drm_dump_planes_res(drmModePlaneResPtr pres) {
	logger_info("Planes: %d", pres->count_planes);
}

void drm_dump_version(drmVersionPtr ver) {
	logger_info("DRM Version Info: %s %s %s %d.%d.%d", ver->name, ver->date, ver->desc, ver->version_major, ver->version_minor, ver->version_patchlevel);
}

void drm_connector_dump_modes(drmModeConnectorPtr conn) {
	if(conn) {
		
		logger_info("Modes:");
		for(int i = 0; i < conn->count_modes; i++) {
			logger_info("\t%dx%d@%dHz", conn->modes[i].hdisplay, conn->modes[i].vdisplay, conn->modes[i].vrefresh);
		}
		fprintf(stderr, "\n");
	}
}


int main(int argc, char **argv) {
	if(argc < 2) {
		logger_error("Example: %s <PATH>", argv[0]);
		return 1;
	}
	
	drm_dev_t *dev = drm_init(argv[1], DRM_CAP_DUMB_BUFFER);
	if(!dev) {
		return 1;
	}
	logger_info("DRM Device: %s", argv[1]);	
	drm_dump_version(dev->ver);
	drm_dump_resources(dev->res);
	drm_dump_planes_res(dev->pres);
	
	for(int i = 0; i < dev->res->count_connectors; i++) {
		drm_dump_connector(dev->connector[i]);
		drm_connector_dump_modes(dev->connector[i].conn);
	}

	for(int i = 0; i < dev->pres->count_planes; i++) {
		drm_dump_planes(dev->fd, dev->planes[i]);
	}
	logger_info("Encoders: ");
	logger_info("%-7s | %-7s | %-7s | %-7s", "ENC ID:", "Type:", "CRTCs:", "Clones:");
	for(int i = 0; i < dev->res->count_encoders; i++) {
		drm_dump_encoder(dev->encoders[i]);
	}
		
	logger_info("\n"); 
	logger_info("%-8s | %-8s | %-9s | %-7s | %-7s | %-2s | %-6s | %-6s", "CRTC ID:", "FBUF ID:", "Gamma Sz:", "Height:", "Width:", "V:", "Xpos:", "Ypos:");
	for(int i = 0; i < dev->res->count_crtcs; i++) {
		drm_dump_crtc(dev->crtcs[i]);
	}


	drm_clean_up(dev);	
	return 0;
}
