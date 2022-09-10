/*
 *	Attempt to find all drm devices using udev to locate them
 *
 */

#include <stdio.h>
#include <stdlib.h>
#include <xf86drmMode.h>
#include <xf86drm.h>
#include <unistd.h>
#include <fcntl.h>
#include <libudev.h>
#include <pci/pci.h>

const char *get_bus_str(int bus) {
	switch(bus) {
		case DRM_BUS_PCI:
			return "PCI";
		case DRM_BUS_HOST1X:
			return "HOST1X";
		case DRM_BUS_USB:
			return "USB";
		case DRM_BUS_PLATFORM:
			return "Platform";
		default:
			return "Unknown";
	}
}

void print_nodes(char **nodes, int avalible_nodes) {
	printf("  Device Nodes:\n");
	for(int i = 0; i < DRM_NODE_MAX; i++) {
		if((avalible_nodes >> i) & 1) {
			printf("    %s\n", nodes[i]);
		}
	}
}

void print_dev(drmDevicePtr dev) {
	drmPciDeviceInfoPtr pci = dev->deviceinfo.pci;
	drmUsbDeviceInfoPtr usb = dev->deviceinfo.usb;
	char dev_name[512];
	struct pci_access *pci_access = NULL;
	
	printf("  Bus: %s\n", get_bus_str(dev->bustype));
	
	switch(dev->bustype) {
		case DRM_BUS_PCI:
			printf("  PCI Device: 0x%x:0x%x\n", pci->vendor_id, pci->device_id);
			pci_access = pci_alloc();
			if(!pci_access) {
				printf("  Failed to get PCI Device name: %m\n");
				break;
			}
			pci_init(pci_access);
				
			pci_lookup_name(pci_access, dev_name, 512, PCI_VENDOR_ID | PCI_DEVICE_ID, pci->vendor_id, pci->device_id);
			printf("  Device Name: %s\n", dev_name);
			
			pci_cleanup(pci_access);
			break;
		case DRM_BUS_USB:
			printf("  USB Device: 0x%x:0x%x\n", usb->vendor, usb->product);
			
			break;
		default:
			printf("  Device Unknown\n");
	}

	print_nodes(dev->nodes, dev->available_nodes);
}

void open_drm_dev(const char *path) {
	int fd = open(path, O_RDWR | O_CLOEXEC);
	if(fd < 0) {
		printf("Failed to open drm device %s %m\n", path);
		return;
	}

	drmDevicePtr dev;
	int i = drmGetDevice(fd, &dev);
	if(!dev) {
		printf("Failed to get device\n");
		close(fd);
		return;
	}
	print_dev(dev);

	drmFreeDevice(&dev);
	close(fd);
}

int main(int argc, char **argv) {
	struct udev *udev = udev_new();
	struct udev_enumerate *enumerate = udev_enumerate_new(udev);
	struct udev_list_entry *list = NULL;
	struct udev_list_entry *entry = NULL;
	int dev_count = 0;

	if(udev_enumerate_add_match_subsystem(enumerate, "drm") < 0) {
		printf("Failed to enumerate drm subsystem %m\n");
		return 1;
	}
	
	udev_enumerate_add_match_sysname(enumerate, DRM_PRIMARY_MINOR_NAME "[0-9]*");

	if(udev_enumerate_scan_devices(enumerate) < 0) {
		printf("Failed to scan devices %m\n");
		return 1;
	}
	list = udev_enumerate_get_list_entry(enumerate);
	udev_list_entry_foreach(entry, list) {
		const char *name = udev_list_entry_get_name(entry);
		
		struct udev_device *dev = udev_device_new_from_syspath(udev, name);
		if(!dev) {
			printf("failed to get udev device\n");
		} else {
		}
		
		//filter out the connector nodes
		if(!udev_device_get_devnode(dev)) {
			udev_device_unref(dev);
			continue;
		}
		const char *dev_node = udev_device_get_devnode(dev);
			
		printf("Device: %d\n", dev_count);
		open_drm_dev(dev_node);

		udev_device_unref(dev);
	}
	
	udev_enumerate_unref(enumerate);
	udev_unref(udev);
	return 0;
}

