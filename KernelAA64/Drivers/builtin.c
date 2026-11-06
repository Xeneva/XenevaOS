/**
 * builtin.c -- registry of hand-over-able built-in drivers, see aubuiltin.h
 */
#include <aubuiltin.h>
#include <string.h>
#include <Drivers/uart.h>

typedef struct {
	char name[24];
	int bus, dev, func;
	bool used, bound;
	au_builtin_fn release, reinit;
} AuBuiltin;

static AuBuiltin entries[AU_BUILTIN_MAX];

static AuBuiltin* find(const char* name) {
	for (int i = 0; i < AU_BUILTIN_MAX; i++)
		if (entries[i].used && strcmp(entries[i].name, name) == 0)
			return &entries[i];
	return 0;
}

void AuBuiltinRegister(const char* name, int bus, int dev, int func,
					   au_builtin_fn release, au_builtin_fn reinit) {
	AuBuiltin* e = find(name);
	if (!e) {
		for (int i = 0; i < AU_BUILTIN_MAX && !e; i++)
			if (!entries[i].used)
				e = &entries[i];
	}
	if (!e)
		return;
	e->used = true;
	strncpy(e->name, name, sizeof(e->name) - 1);
	e->name[sizeof(e->name) - 1] = 0;
	e->bus = bus;
	e->dev = dev;
	e->func = func;
	e->release = release;
	e->reinit = reinit;
	e->bound = true;
}

int AuBuiltinUnload(const char* name) {
	AuBuiltin* e = find(name);
	if (!e)
		return -1;
	if (!e->bound)
		return -2;
	if (e->release)
		e->release();
	e->bound = false;
	UARTDebugOut("[builtin]: released %s (%x:%x.%x)\r\n", e->name, e->bus, e->dev, e->func);
	return 0;
}

int AuBuiltinLoad(const char* name) {
	AuBuiltin* e = find(name);
	if (!e)
		return -1;
	if (e->bound)
		return -2;
	if (e->reinit)
		e->reinit();               /* the driver re-registers itself and sets bound */
	e->bound = true;
	UARTDebugOut("[builtin]: reloaded %s\r\n", e->name);
	return 0;
}

bool AuBuiltinClaimsPci(int bus, int dev, int func) {
	for (int i = 0; i < AU_BUILTIN_MAX; i++)
		if (entries[i].used && entries[i].bound && entries[i].bus == bus &&
			entries[i].dev == dev && entries[i].func == func)
			return true;
	return false;
}

int AuBuiltinList(void (*cb)(const char*, bool, int, int, int)) {
	int n = 0;
	for (int i = 0; i < AU_BUILTIN_MAX; i++)
		if (entries[i].used) {
			if (cb)
				cb(entries[i].name, entries[i].bound, entries[i].bus, entries[i].dev, entries[i].func);
			n++;
		}
	return n;
}
