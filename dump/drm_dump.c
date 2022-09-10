#include "drm.h"
#include "drm_mode.h"
#include <stdlib.h>
#include <stdio.h>
#include <stdint.h>

#include <log.h>

#include <string.h>
#include <xf86drm.h>
#include <xf86drmMode.h>

#include <unistd.h>
#include <fcntl.h>

#include <drm_common.h>
#include <buffers.h>

#include <pci/pci.h>
#include <pci/types.h>

typedef struct drm_dev {
	int fd;
	drmModeResPtr res;
	drmModePlaneResPtr pres;
} drm_dev_t;

#define LINE "║"
drm_dev_t *drm_init(const char *dev_path) {
	drm_dev_t *dev = calloc(1, sizeof(*dev));
	
	dev->fd = open_drm(dev_path, DRM_CAP_DUMB_BUFFER);
	if(dev->fd < 0) {
		free(dev);
		return NULL;
	}

	drmSetClientCap(dev->fd, DRM_CLIENT_CAP_UNIVERSAL_PLANES, 1);

	dev->res = drmModeGetResources(dev->fd);
	if(!dev->res) {
		close(dev->fd);
		free(dev);
		return NULL;
	}

	dev->pres = drmModeGetPlaneResources(dev->fd);
	if(!dev->pres) {
		drmModeFreeResources(dev->res);
		close(dev->fd);
		free(dev);
		return NULL;
	}

	return dev;
}

void drm_cleanup(drm_dev_t *dev) {
	drmModeFreePlaneResources(dev->pres);
	
	drmModeFreeResources(dev->res);

	close(dev->fd);

	free(dev);
}

char *drm_bustype_str(int bustype) {
	switch(bustype) {
		case DRM_BUS_PCI:
			return "PCI";
		case DRM_BUS_USB:
			return "USB";
		case DRM_BUS_HOST1X:
			return "HOST1X";
		case DRM_BUS_PLATFORM:
			return "Platform";
		default:
			return "Unknown";
	}
}

/* drm_dev_dump_name
 * A Attempt to dump out the drm devices actual name through PCI/other methods if possible
 * Else B dump out the generic /dev/dri/card* name instead but ideally dump the real name of the device 
 */
void drm_dev_dump_name(drm_dev_t *dev) {
	drmDevicePtr drm_dev;
	int ret = drmGetDevice(dev->fd, &drm_dev);
	
	printf("╔ DRM Device Info:\n╠══ Bus: %s\n", drm_bustype_str(drm_dev->bustype));
	
	printf("╠══ ");	
	switch(drm_dev->bustype) {
		case DRM_BUS_PCI:
			printf("Vendor: 0x%x, Device: 0x%x\n", drm_dev->deviceinfo.pci->vendor_id, drm_dev->deviceinfo.pci->device_id);
			break;
		case DRM_BUS_USB:
			printf("Vendor: 0x%x, Product: 0x%x\n", drm_dev->deviceinfo.usb->vendor, drm_dev->deviceinfo.usb->product);
			break;
		default: {
			char *dev_path = drmGetDeviceNameFromFd2(dev->fd);
			printf("DRM Device: %s\n", dev_path);
			free(dev_path);
		}
	}
	
	/*
	 * Use the available_nodes bit mask to display
	 */
	printf("╠═╦ Device Nodes:\n");
	for(int i = 0; i < DRM_NODE_MAX; i++) {
		if((drm_dev->available_nodes >> i) & 1) {
			printf("║ %s══ %s\n", (drm_dev->available_nodes >> i) == 1 ? "╚" : "╠", drm_dev->nodes[i]);
		}
	}
	drmFreeDevice(&drm_dev);
}

/*
 *	Get the planes type string using the type int 
 *
 */
char *drm_plane_type_str(uint64_t type) {
	switch(type) {
		case DRM_PLANE_TYPE_PRIMARY:
			return "Primary";
		case DRM_PLANE_TYPE_CURSOR: 
			return "Cursor";
		case DRM_PLANE_TYPE_OVERLAY:
			return "Overlay";
		default:
			return "Unknown";
	}
}

/*
 * Seems like my GPU has one plane that supports all CRTCs and then 
 * has two planes per crtc with one being the primary and one being the cursor plane 
 * and finally one overlay plane that supports all CRTCS. 
 * So I would probably want to get a cursor plane and a primary plane pair when rendering something serious 
 * like a display server then that leaves the overlay plane free to be used for anything else
 */
void drm_dump_property(drm_dev_t *dev, drmModePropertyPtr prop, uint64_t value) {
	printf("║ ╠%s %d %d %d %d %d\n", prop->name, prop->count_blobs, prop->count_enums, prop->count_values, prop->flags, DRM_MODE_PROP_IMMUTABLE | DRM_MODE_PROP_BLOB);
	if(strcmp(prop->name, "IN_FORMATS") == 0) {
		drmModePropertyBlobPtr blob = drmModeGetPropertyBlob(dev->fd, value);
		if(!blob) {
			logger_error("Failed to get blob ptr %m");
			return;
		}
		
		struct drm_format_modifier_blob *format_blob = blob->data;
		uint32_t *formats = (uint32_t *)(((char *)blob->data) + format_blob->formats_offset);
		struct drm_format_modifier *blob_modifiers = (void *)(((char *)blob->data) + format_blob->modifiers_offset);
		//printf("%d %d %d %d %d\n", format_blob->count_formats, format_blob->count_modifiers, format_blob->version, format_blob->formats_offset, format_blob->modifiers_offset);
		for(int i = 0; i < format_blob->count_formats; i++) {
			//printf("Blob format %d = %d\n", i, formats[i]);
			for(int j = 0; j < format_blob->count_modifiers; j++) {
				struct drm_format_modifier mod = blob_modifiers[j];
				if ((i < mod.offset) || (i > mod.offset + 63)) continue;
				if (!(mod.formats & (1 << (i - mod.offset)))) continue;

				//printf("Blob mod: %d = %lld %lld\n", j, mod.modifier, mod.formats);
				
			}
		}
		drmModeFreePropertyBlob(blob);
	}

}

void drm_dump_plane(uint32_t plane_id, drm_dev_t *dev) {
	drmModePlanePtr plane = drmModeGetPlane(dev->fd, plane_id);
	if(!plane) {
		logger_error("Failed to get plane");
		return;
	}

	drmModeObjectPropertiesPtr props = drmModeObjectGetProperties(dev->fd, plane_id, DRM_MODE_OBJECT_PLANE);
	if(!props) {
		logger_error("Failed to get plane properties");
		return;
	}
	
	printf("╠═╦ Plane:\n");
	
	printf("  ╠| %-6s | %-6s | %-5s\n  ╠| %-6d | %-6d | %-5d | %d \n", "ID:", "FB ID:", "CRTC:", plane->plane_id, plane->fb_id, plane->crtc_id, plane->count_formats);
	
	for(int i = 0; i < props->count_props; i++) {
		drmModePropertyPtr property = drmModeGetProperty(dev->fd, props->props[i]);
		drm_dump_property(dev, property, props->prop_values[i]);
		drmModeFreeProperty(property);
	}
	printf("\n");
	drmModeFreePlane(plane);
	drmModeFreeObjectProperties(props);
}

char *drm_enc_type_str(uint64_t type) {
	switch (type) {
		case DRM_MODE_ENCODER_DAC:
			return "DAC";
		case DRM_MODE_ENCODER_DSI:
			return "DSI";
		case DRM_MODE_ENCODER_DPMST:
			return "DPMST";
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

void drm_dump_encoder(uint32_t enc_id, drm_dev_t *dev) {
	drmModeEncoderPtr enc = drmModeGetEncoder(dev->fd, enc_id);
	
	printf("%-8s | %-5s | %-7s | Valid CRTCs: | Clones:\n", "Encoder:", "CRTC:", "Type:");
	printf("%-8d | %-5d | %-7s | 0x%-2x         | 0x%-3x\n\n", enc->encoder_id, enc->crtc_id, drm_enc_type_str(enc->encoder_type), enc->possible_crtcs, enc->possible_clones);
	
	drmModeFreeEncoder(enc);
}

void drm_dump_crtc(uint32_t crtc_id, drm_dev_t *dev) {
	drmModeCrtcPtr crtc = drmModeGetCrtc(dev->fd, crtc_id);
	drmModeFB2Ptr fb2 = drmModeGetFB2(dev->fd, crtc->buffer_id);
	printf("%d %d\n", crtc->crtc_id, crtc->buffer_id);
	
	if(crtc->buffer_id) {
		printf("%d %d %d %.*s %d %d %d %d\n", fb2->fb_id, fb2->width, fb2->height, 4, (char *)&fb2->pixel_format, fb2->handles[0], fb2->handles[1], fb2->handles[2], fb2->handles[3]);
		printf("%d %d %d %d %d %lu\n", fb2->pitches[0], fb2->pitches[1], fb2->pitches[2], fb2->pitches[3], fb2->flags, fb2->modifier);
		printf("%d %d %d %d\n\n", fb2->offsets[0], fb2->offsets[1], fb2->offsets[2], fb2->offsets[3]);
	}

	drmModeFreeCrtc(crtc);
}

int main(int argc, char **argv) {
	if(argc < 2) {
		printf("EXAMPLE: %s <PATH_TO_DRM_DEV>\n", argv[0]);
		return 1;
	}

	drm_dev_t *dev = drm_init(argv[1]);
	if(dev == NULL) {
		logger_fatal("Failed to create DRM_DEVICE");
		return 1;
	}

	drm_dev_dump_name(dev);
	
	for(int i = 0; i < dev->pres->count_planes; i++) {
		drm_dump_plane(dev->pres->planes[i], dev);
	}

	for(int i = 0; i < dev->res->count_encoders; i++) {
		drm_dump_encoder(dev->res->encoders[i], dev);
	}
	
	for(int i = 0; i < dev->res->count_crtcs; i++) {
		drm_dump_crtc(dev->res->crtcs[i], dev);
	}
	drm_cleanup(dev);
	return 0;
}
