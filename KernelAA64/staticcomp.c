/**
 * staticcomp.c -- the table of drivers linked into the kernel image when it is
 * built with STATIC_COMPONENTS=1 (the unikernel profile). In a normal build the
 * table is empty and every lookup misses, so drivers load from PE files as usual.
 *
 * Each driver is compiled with its exported entry points renamed
 * (AuDriverMain -> AuDriverMain_<name>) so several drivers can live in one image.
 */
#include <austatic.h>
#include <string.h>
#include <_null.h>

#ifdef __XENEVA_STATIC_COMPONENTS__
extern int AuDriverMain_virtnet(void);
extern int AuDriverMain_virtgpu(void);
extern int AuDriverMain_virtsnd(void);
extern int AuDriverMain_virtblk(void);
extern void VirtGpuConsolePresent(void);

static const AuStaticComponent g_components[] = {
	{"virtnet", (au_drv_entry)AuDriverMain_virtnet, NULL},
	{"virtgpu", (au_drv_entry)AuDriverMain_virtgpu, (void*)VirtGpuConsolePresent},
	{"virtsnd", (au_drv_entry)AuDriverMain_virtsnd, NULL},
	{"virtblk", (au_drv_entry)AuDriverMain_virtblk, NULL},
};
#define N_COMPONENTS ((int)(sizeof(g_components) / sizeof(g_components[0])))
#else
static const AuStaticComponent g_components[1];
#define N_COMPONENTS 0
#endif

int AuStaticComponentCount(void) {
	return N_COMPONENTS;
}

const AuStaticComponent* AuStaticComponentFind(const char* path) {
	if (!path)
		return NULL;
	const char* base = path;
	for (const char* p = path; *p; p++)
		if (*p == '/')
			base = p + 1;
	size_t n = 0;
	while (base[n] && base[n] != '.')
		n++;
	for (int i = 0; i < N_COMPONENTS; i++)
		if (strlen(g_components[i].name) == n && !strncmp(g_components[i].name, base, n))
			return &g_components[i];
	return NULL;
}
