#ifndef __LINUX_MODULE_LOADER_H__
#define __LINUX_MODULE_LOADER_H__

#include <stdint.h>
#include <stddef.h>

#define MOD_MAX_SECTIONS   64
#define MOD_MAX_SYMBOLS    512
#define MOD_MAX_NAME       64


struct mod_handle {
	char name[MOD_MAX_NAME];
	uint64_t base;           
	uint64_t phys;           
	size_t   size;           
	uint64_t init_func;      
	uint64_t exit_func;      
	int      loaded;         
};

int mod_load(const void *data, size_t size, struct mod_handle *handle);

void mod_unload(struct mod_handle *handle);

int mod_call_init(struct mod_handle *handle);

void mod_call_exit(struct mod_handle *handle);

uint64_t k_export_lookup(const char *name);

#endif
