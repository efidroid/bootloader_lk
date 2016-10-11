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
#include <dev/keys.h>

typedef struct newkey_event_source newkey_event_source_t;
typedef int (*newkey_event_poll_t)(newkey_event_source_t *source);

typedef enum {
    KEYSTATE_RELEASED,
    KEYSTATE_PRESSED,
    KEYSTATE_LONGPRESS_RELEASE,
} newkeystate_t;

typedef struct {
    time_t time;
    bool repeat;
    bool longpress;
    newkeystate_t state;
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
int newkeys_post_event(uint16_t code, int16_t value);
int newkeys_poll(void);
void newkeys_add_source(newkey_event_source_t *source);
void newkeys_delete_all_events(void);
int newkeys_get_next_event(uint16_t *code, uint16_t *value);
int newkeys_has_event(void);
int newkeys_set_report_key(newkey_event_source_t *source, uint16_t code, uint16_t value);

#endif /* __DEV_NEWKEYS_H */
