#include <stdlib.h>
#include <string.h>
#include <arch/ops.h>

#define atomic_read(v)  (*(volatile int *)(v))
#define atomic_set(v,i) ((*(v)) = (i))

static inline int atomic_cmpxchg(int *ptr, int old, int new)
{
    unsigned long oldval, res;

    dmb();

    do {
        __asm__ __volatile__("@ atomic_cmpxchg\n"
                             "ldrex	%1, [%3]\n"
                             "mov	%0, #0\n"
                             "teq	%1, %4\n"
                             "strexeq %0, %5, [%3]\n"
                             : "=&r" (res), "=&r" (oldval), "+Qo" (*ptr)
                             : "r" (ptr), "Ir" (old), "r" (new)
                             : "cc");
    } while (res);

    dmb();

    return oldval;
}

struct persistent_ram_buffer {
    uint32_t    sig;
    int      start;
    int      size;
    uint8_t  data[0];
};

#define PERSISTENT_RAM_SIG (0x43474244) /* DBGC */

static int allow_write = 0;
static struct persistent_ram_buffer *pram_buffer = NULL;
static int pram_buffer_size = 0;
static char *old_log = NULL;
static int old_log_size = 0;

static inline int buffer_size(void)
{
    assert(pram_buffer);
    return atomic_read(&pram_buffer->size);
}

static inline int buffer_start(void)
{
    assert(pram_buffer);
    return atomic_read(&pram_buffer->start);
}

/* increase and wrap the start pointer, returning the old value */
static inline int buffer_start_add(int a)
{
    int old;
    int new;

    assert(pram_buffer);

    do {
        old = atomic_read(&pram_buffer->start);
        new = old + a;
        while (unlikely(new > pram_buffer_size))
            new -= pram_buffer_size;
    } while (atomic_cmpxchg(&pram_buffer->start, old, new) != old);

    return old;
}

/* increase the size counter until it hits the max size */
static inline void buffer_size_add(int a)
{
    int old;
    int new;

    assert(pram_buffer);

    if (atomic_read(&pram_buffer->size) == pram_buffer_size)
        return;

    do {
        old = atomic_read(&pram_buffer->size);
        new = old + a;
        if (new > pram_buffer_size)
            new = pram_buffer_size;
    } while (atomic_cmpxchg(&pram_buffer->size, old, new) != old);
}

static void persistent_ram_update(const void *s, unsigned int start, unsigned int count)
{
    assert(pram_buffer);
    memcpy(pram_buffer->data + start, s, count);
}

static void
persistent_ram_save_old(void)
{
#ifdef WITH_KERNEL_UEFIAPI
    return;
#endif

    int size = buffer_size();
    int start = buffer_start();
    char *dest;

    dest = malloc(size);
    if (dest == NULL) {
        dprintf(CRITICAL, "persistent_ram: failed to allocate buffer\n");
        return;
    }

    old_log = dest;
    old_log_size = size;
    memcpy(old_log, &pram_buffer->data[start], size - start);
    memcpy(old_log + size - start, &pram_buffer->data[0], start);
}

int persistent_ram_write(const void *s, unsigned int count)
{
    int rem;
    int c = count;
    int start;

    if (!pram_buffer || !allow_write)
        return 0;

    if (unlikely(c > pram_buffer_size)) {
        s += c - pram_buffer_size;
        c = pram_buffer_size;
    }

    buffer_size_add(c);

    start = buffer_start_add(c);

    rem = pram_buffer_size - start;
    if (unlikely(rem < c)) {
        persistent_ram_update(s, start, rem);
        s += rem;
        c -= rem;
        start = 0;
    }
    persistent_ram_update(s, start, c);

    return count;
}

int persistent_ram_old_size(void)
{
    return old_log_size;
}

void *persistent_ram_old(void)
{
    return old_log;
}

void persistent_ram_free_old(void)
{
    free(old_log);
    old_log = NULL;
    old_log_size = 0;
}

int persistent_ram_init(addr_t start, addr_t size)
{
    pram_buffer = (void *) start;
    pram_buffer_size = size - sizeof(struct persistent_ram_buffer);

    if (pram_buffer->sig == PERSISTENT_RAM_SIG) {
        if (buffer_size() > pram_buffer_size ||
                buffer_start() > buffer_size())
            dprintf(INFO, "persistent_ram: found existing invalid buffer,"
                    " size %zu, start %zu\n",
                    buffer_size(), buffer_start());
        else {
            dprintf(INFO, "persistent_ram: found existing buffer,"
                    " size %zu, start %zu\n",
                    buffer_size(), buffer_start());
            persistent_ram_save_old();
        }
    } else {
        dprintf(INFO, "persistent_ram: no valid data in buffer"
                " (sig = 0x%08x)\n", pram_buffer->sig);
    }

    pram_buffer->sig = PERSISTENT_RAM_SIG;
    atomic_set(&pram_buffer->start, 0);
    atomic_set(&pram_buffer->size, 0);
    allow_write = 1;

    return 0;
}
