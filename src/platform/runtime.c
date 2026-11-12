/* newlib glue: the heap and the init hooks. */
#include <errno.h>
#include <stddef.h>

extern char heap_start[], heap_end[];
static char *heap_top = heap_start;

void *_sbrk(ptrdiff_t increment)
{
    if (heap_top + increment > heap_end) {
        errno = ENOMEM;
        return (void *)-1;
    }
    char *previous = heap_top;
    heap_top += increment;
    return previous;
}

void *sbrk_top(void) { return heap_top; }

void _init(void) {}
void _fini(void) {}
