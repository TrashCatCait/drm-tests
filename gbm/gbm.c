/*STD libs*/
#include <asm-generic/errno.h>
#include <gbm.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <stdarg.h>

/*cli options parse*/
#include <getopt.h>
#include <bits/getopt_ext.h>
#include <bits/getopt_core.h>

/*mmap*/
#include <sys/mman.h>

/*Linux DRM*/
#include <xf86drm.h>
#include <xf86drmMode.h>

/*posix file operations*/
#include <fcntl.h>
#include <unistd.h>


//Errno 
#include <errno.h>

struct config {
	uint8_t ver;
	uint8_t kms;
	uint8_t mas;
};

static struct config cfg = { 0 };

#define VER_MAJ 0 
#define VER_MIN 1

void usage(char *progname) {
	printf("USAGE: %s [-pvVhkm]\n", progname);
	printf("Options:\
			\n-p | --path = path to drm device (takes arg),\
			\n-v | --verbose = enable verbose output,\
			\n-h | --help = print this help message,\
			\n-V | --version = print version info\
			\n-k | --kms = override kms error(not reccommended)\
			\n-m | --master = override drm master error(not reccommended)\n");
}

void version(char *progname) {
	printf("%s version: %d.%d\n", progname, VER_MAJ, VER_MIN);
}

int verbose(const char *fmt, ...) {
	int ret = 0;
	va_list args;
	
	va_start(args, fmt);
	switch(cfg.ver) {
		case 1:
			ret = vfprintf(stderr, fmt, args);
	}
	va_end(args);
	return ret ;
}

int drm_open(char *path) {
	int fd = open(path, O_CLOEXEC | O_RDWR);
	if(fd < 0) {
		return fd;
	}

	int iskms = drmIsKMS(fd) | cfg.kms;
	int ismaster = drmIsMaster(fd) | cfg.mas;
	errno = 0; //Reset errorno as drmIsMaster has affected it 
	
	switch(iskms & ismaster) {
		case 0:
			close(fd);
			fd = -1;
	}

	verbose("FD: %d\nMaster: %d\nKMS: %d\n", fd, iskms, ismaster);

	return fd;
}

#include <EGL/egl.h>
#include <EGL/eglplatform.h>
#include <EGL/eglext.h>
int run(char *path) {

	eglBindAPI(EGL_OPENGL_API);

	int fd = drm_open(path);
	if(fd < 0) {
		return fd;
	}
	EGLint min = 0, maj = 0;
	int configs = 0;	
	verbose("Open: %m\n");
	drmSetMaster(fd);


	struct gbm_device *dev = gbm_create_device(fd);
	verbose("Device %p: %m\n", dev);


	EGLDisplay display = eglGetPlatformDisplay(EGL_PLATFORM_GBM_MESA, dev, NULL);
	//eglGetDisplay(dev);
	verbose("DISPLAY: %p %m\n", display);
	errno = 0;
	if(display == EGL_NO_DISPLAY) {
		printf("Failed to get EGL display\n");
	}
	if(!eglInitialize(display, &maj, &min)) {
		verbose("EGL INIT FAIL\n");
	}
	
	verbose("GBM DEVICE: %s", gbm_device_get_backend_name(dev));

	

	verbose("EGL VER: %d.%d %m\n", maj,min);
	struct gbm_surface *surf = gbm_surface_create(dev, 1920, 1080, GBM_FORMAT_XRGB8888, 0);
	verbose("Surface %p: %m\n", surf);
	struct gbm_bo *bo = gbm_bo_create(dev, 1920, 1080, GBM_FORMAT_XRGB8888, GBM_BO_USE_SCANOUT);
	
	verbose("BO %p: %m\n", bo);
	close(fd);
	return 0;
}

int main(int argc, char **argv) {
	int arg = 0;
	char *path = "/dev/dri/card0";
	int opt_ind = 0;
	struct option long_opts[] = { 
		{ "path", required_argument, 0, 'p'},
		{ "verbose", no_argument, 0, 'v'},
		{ "help", no_argument, 0, 'h'},
		{ "kms", no_argument, 0, 'k'},
		{ "master", no_argument, 0, 'm'},
		{ "version", no_argument, 0, 'V'},
	};



	while((arg = getopt_long(argc, argv, ":p:vVhkm", long_opts, &opt_ind)) != -1) {
		switch(arg) {
			case 'k':
				cfg.kms = 1;
				break;
			case 'm':
				cfg.mas = 1;
				break;
			case 'p':
				path = optarg;
				break;
			case 'V':
				version(argv[0]);
				break;
			case 'v':
				cfg.ver = 1;
				break;
			case 'h':
				usage(argv[0]);
				return 1;
			case ':':
				printf("Option %c requires an argument\n", optopt);
				usage(argv[0]);
				return 1;
			case '?':
				printf("Option %d is unknown and will be ignored\n", optind);
		}
	}
	verbose("path: %s\n", path);	
	run(path);
	return 1;
}
