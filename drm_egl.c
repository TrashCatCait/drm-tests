#include <log.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>

#include <EGL/egl.h>
#include <EGL/eglext.h>
#include <GLES3/gl3.h>

#include <xf86drm.h>
#include <xf86drmMode.h>

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

bool substrcmp(const char *str, const char *substr) {
	size_t substrlen = strlen(substr);
	const char *strend = str + strlen(str);

	while(str < strend) {
		size_t len = strcspn(str, " ");
		logger_debug("%d", len);
		if(len == substrlen && strncmp(str, substr, len) == 0) {
			return true;
		}
		str += len + 1;
	}
	return false;
}

int main(int argc, char **argv) {
	int fd = open_drm("/dev/dri/card0", DRM_CAP_DUMB_BUFFER);
	if(fd < 0) {
		return 0;
	}
	char *render = drmGetRenderDeviceNameFromFd(fd);
	char *primary = drmGetPrimaryDeviceNameFromFd(fd);
	
	logger_info("Primary: %s", primary);
	logger_info("Render: %s", render);

	const char *egl_ext_str = eglQueryString(EGL_NO_DISPLAY, EGL_EXTENSIONS);
	const char *egl_ver = eglQueryString(EGL_NO_DISPLAY, EGL_VERSION);
	const char *egl_ven = eglQueryString(EGL_NO_DISPLAY, EGL_VENDOR);
	logger_info("%s", egl_ext_str);
	logger_info("Version: %s", egl_ver);
	logger_info("Vendor: %s", egl_ver);
		
	EGLDeviceEXT *devices; 
	PFNEGLQUERYDEVICESEXTPROC eglQueryDevicesEXT;
	PFNEGLQUERYDEVICESTRINGEXTPROC eglQueryDeviceStringEXT;
	EGLint max_dev = 0;
	EGLint num_dev = 0;
	
	if(substrcmp(egl_ext_str, "EGL_EXT_device_enumeration")) {
		logger_debug("EGL_EXT_device_enumeration supported");
		
		eglQueryDevicesEXT = (void *)eglGetProcAddress("eglQueryDevicesEXT");
	} else {
		logger_debug("EGL_EXT_device_enumeration not supported");
	}
	
	if(substrcmp(egl_ext_str, "EGL_EXT_device_query")) {
		eglQueryDeviceStringEXT = (void *)eglGetProcAddress("eglQueryDeviceStringEXT");
	}

	eglQueryDevicesEXT(max_dev, NULL, &num_dev);
	logger_debug("%d %d", max_dev, num_dev);
	max_dev = num_dev;
	devices = calloc(num_dev, sizeof(EGLDeviceEXT));
	num_dev = 0;
	eglQueryDevicesEXT(max_dev, devices, &num_dev);
	logger_debug("%s", eglQueryDeviceStringEXT(devices[0], EGL_DRM_DEVICE_FILE_EXT));

	close(fd);
}
