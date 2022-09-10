/*
 *	Attempt to find all drm devices using built in drm methods
 *
 */

#include <stdio.h>
#include <stdlib.h>
#include <xf86drmMode.h>
#include <xf86drm.h>
#include <unistd.h>
#include <fcntl.h>

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

int main(int argc, char **argv) {
	int max_devs = 0;
	drmDevicePtr *devs = NULL;

	/*Get the number of devices*/
	max_devs = drmGetDevices(NULL, max_devs);
	if(max_devs < 0) {
		printf("Failed to successfully call drmGetDevices: %m\n");
		return 1;
	}	


	devs = calloc(max_devs, sizeof(drmDevicePtr));
	if(!devs) {
		printf("Failed to allocate devices ptr: %m\n");
		return 1;
	}

	/*actually get devices*/
	max_devs = drmGetDevices(devs, max_devs);

	/*Print the devices*/
	for(int i = 0; i < max_devs; i++) {
		printf("Device %d:\n", i);
		print_dev(devs[i]);	
	}
	
	//Free used memory 
	drmFreeDevices(devs, max_devs);
	free(devs);
	return 0;
}
