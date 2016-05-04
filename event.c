#include <debug.h>
#include <err.h>
#include <kernel/event.h>
#include <uefiapi.h>

void event_init(event_t *e, bool initial, uint flags)
{
    ASSERT(uefiapi.event_init);
    uefiapi.event_init((void **)&e->flags);
}

void event_destroy(event_t *e)
{
    ASSERT(uefiapi.event_destroy);
    uefiapi.event_destroy((void *)e->flags);
}

status_t event_wait(event_t *e)
{
    ASSERT(uefiapi.event_wait);
    uefiapi.event_wait((void **)&e->flags);

    return NO_ERROR;
}

status_t event_signal(event_t *e, bool reschedule)
{
    ASSERT(uefiapi.event_signal);
    uefiapi.event_signal((void *)e->flags);

    return NO_ERROR;
}
