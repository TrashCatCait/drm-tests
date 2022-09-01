#pragma once

#include <stdint.h>

typedef struct bo {
	//Buffer Handle 
	uint32_t handle;

	//Buffer height, width and pitch 
	uint32_t height;
	uint32_t width;
	uint32_t pitch;
	
	//Bits per pixel 
	uint32_t bpp;
	
	//Mapped buffer details
	void *buffer;
	uint64_t size;
	uint64_t offset;
} bo_t;

int buffer_destroy_dumb(int fd, bo_t *bo);
bo_t *buffer_create_dumb(int fd, uint32_t bpp, uint32_t height, uint32_t width);
int bo_map(int fd, bo_t *bo);
void buffer_unmap(bo_t *bo); 
