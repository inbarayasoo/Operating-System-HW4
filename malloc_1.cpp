#include <unistd.h>
#define MAX_SIZE 100000000

void *smalloc(size_t size)
{
	if (size > MAX_SIZE || size == 0) {
		return NULL;
	}
	void *current_ptr = sbrk(size);
    if (current_ptr == (void *)-1) {
		return NULL;
	}
	return current_ptr;
}