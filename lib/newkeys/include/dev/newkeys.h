/*
 * Copyright (c) 2009, Google Inc.
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 *  * Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 *  * Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in
 *    the documentation and/or other materials provided with the
 *    distribution.
 *  * Neither the name of Google, Inc. nor the names of its contributors
 *    may be used to endorse or promote products derived from this
 *    software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 * LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS
 * FOR A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE
 * COPYRIGHT OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT,
 * INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING,
 * BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS
 * OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED
 * AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY,
 * OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT
 * OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 */

#ifndef __DEV_NEWKEYS_H
#define __DEV_NEWKEYS_H

#include <sys/types.h>
#include <list.h>
#include <platform.h>
#include <assert.h>

typedef struct newkey_event_source newkey_event_source_t;
typedef int (*newkey_event_poll_t)(newkey_event_source_t *source);

typedef enum {
    KEYSTATE_RELEASED,
    KEYSTATE_PRESSED,
    KEYSTATE_LONGPRESS_WAIT,
    KEYSTATE_LONGPRESS_RELEASE,
} newkeystate_t;

typedef struct {
    time_t time;
    bool repeat;
    bool longpress;
    newkeystate_t state;

    bool enable_longpress;
} newkeymap_t;

struct newkey_event_source {
    struct list_node node;
    void *pdata;

    time_t last;
    time_t delta;
    newkey_event_poll_t poll;
    newkeymap_t keymap[MAX_KEYS];
};

void newkeys_init(void);
int newkeys_poll(void);
void newkeys_add_source(newkey_event_source_t *source);
void newkeys_delete_all_events(void);
int newkeys_get_next_event(uint16_t *code, uint16_t *value);
int newkeys_has_event(void);

static inline int newkeys_set_report_key(newkey_event_source_t *source, uint16_t code, uint16_t *pvalue)
{
    int rc = 0;
    uint16_t value = *pvalue;

    // update key time
    source->keymap[code].time+=source->delta;

    switch (source->keymap[code].state) {
        case KEYSTATE_RELEASED:
            if (value) {
                // change to pressed
                if (source->keymap[code].enable_longpress) {
                    source->keymap[code].time=0;
                    source->keymap[code].state = KEYSTATE_LONGPRESS_WAIT;
                } else {
                    *pvalue = 1;
                    rc = 1;
                    source->keymap[code].time=0;
                    source->keymap[code].state = KEYSTATE_PRESSED;
                }
            }
            break;

        case KEYSTATE_PRESSED:
            if (!value && source->keymap[code].time>50) {
                // change to released
                source->keymap[code].time=0;
                source->keymap[code].repeat=false;
                source->keymap[code].longpress = false;
                source->keymap[code].state = KEYSTATE_RELEASED;
                *pvalue = 0;
                rc = 1;
            }

            // key repeat
            else if ((source->keymap[code].repeat && source->keymap[code].time>=200) || source->keymap[code].time>=500) {
                *pvalue = 1;
                rc = 1;
                source->keymap[code].time=0;
                source->keymap[code].repeat=true;
            }
            break;

        case KEYSTATE_LONGPRESS_WAIT:
            if (value) {
                if (!source->keymap[code].longpress && source->keymap[code].time>=500) {
                    if (keys_get_state(KEY_VOLUMEDOWN)) {
                        // report 's'
                        keys_post_event(0x73, 1);
                        keys_post_event(0x73, 0);
                    } else if (keys_get_state(KEY_VOLUMEUP)) {
                        // report 'e'
                        keys_post_event(0x65, 1);
                        keys_post_event(0x65, 0);
                    } else {
                        // report spacebar
                        keys_post_event(32, 1);
                        keys_post_event(32, 0);
                    }

                    source->keymap[code].longpress=true;
                }
            }

            else {
                if (!source->keymap[code].longpress) {
                    // we supressed down, so report it now
                    *pvalue = 1;
                    rc = 1;
                    source->keymap[code].state = KEYSTATE_LONGPRESS_RELEASE;
                }

                else if (source->keymap[code].time>50) {
                    // we reported a longpress already
                    source->keymap[code].time=0;
                    source->keymap[code].repeat=false;
                    source->keymap[code].longpress = false;
                    source->keymap[code].state = KEYSTATE_RELEASED;
                }
            }
            break;

        case KEYSTATE_LONGPRESS_RELEASE:
            // change to released
            source->keymap[code].time=0;
            source->keymap[code].repeat=false;
            source->keymap[code].longpress = false;
            source->keymap[code].state = KEYSTATE_RELEASED;
            *pvalue = 0;
            rc = 1;
            break;

        default:
            ASSERT(0);
    }

    return rc;
}

#endif /* __DEV_NEWKEYS_H */
