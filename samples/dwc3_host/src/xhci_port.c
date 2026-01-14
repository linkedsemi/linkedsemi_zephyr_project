#include <stddef.h>
#include <zephyr/kernel.h>
#include <zephyr/cache.h>

static volatile int event_sync = 0;
static volatile int event_wait = 0;


void xhci_enqueue_lock()
{

}

void xhci_enqueue_unlock()
{

}

void xhci_event_complete()
{
    /* 有人等，才唤醒线程，没人等，不能唤醒，避免下一个线程直接不用等 */
    if (event_wait)
        event_sync = 1;
}

void xhci_event_wait()
{
    event_wait = 1;
    while (!event_sync);
    event_wait = 0;
    event_sync = 0;
}

void *xhci_mem_alloc(size_t align, size_t size)
{
    return k_aligned_alloc(align, size);;
}

void xhci_mem_free(void *ptr)
{
    k_free(ptr);
}

void xhci_cache_flush(void *buf, size_t size)
{
    sys_cache_data_flush_range(buf, size);
}

void xhci_cache_invalid(void *buf, size_t size)
{
    sys_cache_data_invd_range(buf, size);
}