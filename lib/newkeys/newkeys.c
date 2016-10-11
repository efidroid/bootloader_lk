#include <err.h>
#include <bits.h>
#include <list.h>
#include <debug.h>
#include <string.h>
#include <malloc.h>
#include <dev/keys.h>

#define LOCAL_TRACE 0

// NEW EVENTS
typedef struct {
    struct list_node node;
    uint16_t code;
    uint16_t value;
} key_event_t;

static struct list_node event_queue;
static struct list_node event_sources;

void newkeys_init(void)
{
    list_initialize(&event_queue);
    list_initialize(&event_sources);
}

int newkeys_post_event(uint16_t code, int16_t value)
{
    int i;
    uint8_t seq[3];
    uint8_t seqsz = 0;

    if (code >= MAX_KEYS) {
        return ERR_INVALID_ARGS;
    }

    if (code<=0xff) {
        seq[seqsz++] = code;
    } else {
        switch (code) {
            case KEY_VOLUMEUP:
                seq[seqsz++] = 0x1b;
                seq[seqsz++] = 0x5b;
                seq[seqsz++] = 0x41;
                break;

            case KEY_VOLUMEDOWN:
                seq[seqsz++] = 0x1b;
                seq[seqsz++] = 0x5b;
                seq[seqsz++] = 0x42;
                break;

            case KEY_BACK:
                seq[seqsz++] = 0x1b;
                seq[seqsz++] = 0x5b;
                seq[seqsz++] = 0x44;
                break;
        }
    }

    for (i=0; i<seqsz; i++) {
        // create event
        key_event_t *event = malloc(sizeof(key_event_t));
        event->code = seq[i];
        event->value = value;

        // add event
        list_add_tail(&event_queue, &event->node);
    }

    return NO_ERROR;
}

int newkeys_poll(void)
{
    newkey_event_source_t *source;
    list_for_every_entry(&event_sources, source, newkey_event_source_t, node) {
        // update time
        time_t current = current_time();
        source->delta = current-source->last;
        source->last = current;

        source->poll(source);
    }

    return 0;
}

void newkeys_add_source(newkey_event_source_t *source)
{
    int i;

    source->last = INFINITE_TIME;

    for (i=0; i<MAX_KEYS; i++) {
        source->keymap[i].time = 0;
        source->keymap[i].repeat = false;
        source->keymap[i].longpress = false;
        source->keymap[i].state = KEYSTATE_RELEASED;
    }

    list_add_tail(&event_sources, &source->node);
}

void newkeys_delete_all_events(void)
{
    while (newkeys_has_event()) {
        key_event_t *event = list_remove_tail_type(&event_queue, key_event_t, node);
        free(event);
    }
}

int newkeys_get_next_event(uint16_t *code, uint16_t *value)
{
    // wait for next event
    if (!newkeys_has_event())
        return ERR_NOT_READY;

    // get event
    key_event_t *event = list_remove_head_type(&event_queue, key_event_t, node);

    // set result code
    *code = event->code;
    *value = event->value;

    // free event
    free(event);

    return NO_ERROR;
}

int newkeys_has_event(void)
{
    return !list_is_empty(&event_queue);
}
