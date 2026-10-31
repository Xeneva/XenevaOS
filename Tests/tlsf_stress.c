#include <Mm/tlsf.h>
#include <stdint.h>
#include <string.h>

extern int puts(const char* string);

#define HEAP_SIZE (2U * 1024U * 1024U)
#define SLOT_COUNT 256
#define ITERATIONS 50000

static _Alignas(TLSF_ALIGN_SIZE) unsigned char heap[HEAP_SIZE];

typedef struct allocation {
	unsigned char* ptr;
	size_t size;
	unsigned char pattern;
} allocation_t;

static uint32_t random_state = 0x58454e45U;

static uint32_t next_random(void) {
	random_state ^= random_state << 13;
	random_state ^= random_state >> 17;
	random_state ^= random_state << 5;
	return random_state;
}

static int verify(const allocation_t* allocation) {
	for (size_t i = 0; i < allocation->size; ++i) {
		if (allocation->ptr[i] != allocation->pattern)
			return -1;
	}
	return 0;
}

int main(void) {
	tlsf_pool_t* pool = tlsf_create();
	allocation_t slots[SLOT_COUNT] = {0};
	if (!pool || tlsf_add_memory(pool, heap, sizeof(heap)) != 0)
		return 1;

	for (unsigned int iteration = 0; iteration < ITERATIONS; ++iteration) {
		unsigned int slot = next_random() % SLOT_COUNT;
		allocation_t* allocation = &slots[slot];
		if (allocation->ptr && verify(allocation) != 0)
			return 2;

		uint32_t operation = next_random() % 3;
		if (!allocation->ptr) {
			size_t size = 1 + (next_random() % 8192);
			allocation->ptr = tlsf_malloc(pool, size);
			if (!allocation->ptr)
				continue;
			allocation->size = size;
			allocation->pattern = (unsigned char)(slot + 1);
			memset(allocation->ptr, allocation->pattern, size);
		} else if (operation == 0) {
			tlsf_free(pool, allocation->ptr);
			memset(allocation, 0, sizeof(*allocation));
		} else {
			size_t old_size = allocation->size;
			size_t new_size = 1 + (next_random() % 12288);
			unsigned char* resized = tlsf_realloc(pool, allocation->ptr, new_size);
			if (!resized)
				continue;
			allocation->ptr = resized;
			allocation->size = new_size;
			if (verify(&(allocation_t){resized,
				old_size < new_size ? old_size : new_size,
				allocation->pattern}) != 0)
				return 3;
			memset(resized, allocation->pattern, new_size);
		}
	}

	for (unsigned int i = 0; i < SLOT_COUNT; ++i) {
		if (slots[i].ptr && verify(&slots[i]) != 0)
			return 4;
	}
	puts("tlsf stress: ok");
	return 0;
}
