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

#ifndef __DEV_KEYS_H
#define __DEV_KEYS_H

#include <sys/types.h>
#include <list.h>
#include <platform.h>

/* these are just the ascii values for the chars */
#define KEY_0		0x30
#define KEY_1		0x31
#define KEY_2		0x32
#define KEY_3		0x33
#define KEY_4		0x34
#define KEY_5		0x35
#define KEY_6		0x36
#define KEY_7		0x37
#define KEY_8		0x38
#define KEY_9		0x39

#define KEY_A		0x61

#define KEY_ESC		0x100
#define KEY_F1		0x101
#define KEY_F2		0x102
#define KEY_F3		0x103
#define KEY_F4		0x104
#define KEY_F5		0x105
#define KEY_F6		0x106
#define KEY_F7		0x107
#define KEY_F8		0x108
#define KEY_F9		0x109

#define KEY_LEFT	0x110
#define KEY_RIGHT	0x111
#define KEY_UP		0x112
#define KEY_DOWN	0x113
#define KEY_CENTER	0x114

#define KEY_VOLUMEUP	0x115
#define KEY_VOLUMEDOWN	0x116
#define KEY_MUTE	0x117
#define KEY_SOUND	0x118

#define KEY_SOFT1	0x11a
#define KEY_SOFT2	0x11b
#define KEY_STAR	0x11c
#define KEY_SHARP	0x11d
#define KEY_MAIL	0x11e

#define KEY_SEND	0x120
#define KEY_CLEAR	0x121
#define KEY_HOME	0x122
#define KEY_BACK	0x123
#define KEY_MENU	0x124

#define MAX_KEYS	0x1ff

typedef struct key_event_source key_event_source_t;
typedef int (*key_event_poll_t)(key_event_source_t* source);

typedef enum {
    KEYSTATE_RELEASED,
    KEYSTATE_PRESSED,
    KEYSTATE_LONGPRESS_WAIT,
    KEYSTATE_LONGPRESS_RELEASE,
} keystate_t;

typedef struct {
	time_t time;
	bool repeat;
	bool longpress;
	keystate_t state;

	bool enable_longpress;
} keymap_t;

struct key_event_source {
	struct list_node node;
	void* pdata;

	time_t last;
	time_t delta;
	key_event_poll_t poll;
	keymap_t keymap[MAX_KEYS];
};

void keys_init(void);
int keys_poll(void);
void keys_add_source(key_event_source_t* source);
int keys_post_event(uint16_t code, int16_t value);
int keys_get_state(uint16_t code);
void keys_clear_all(void);
int keys_get_next(uint16_t* code, uint16_t* value);
int keys_has_next(void);

static inline int keys_set_report_key(key_event_source_t* source, uint16_t code, uint16_t* pvalue) {
	int rc = 0;
	uint16_t value = *pvalue;

	// update key time
	source->keymap[code].time+=source->delta;

	switch(source->keymap[code].state) {
		case KEYSTATE_RELEASED:
			if(value) {
				// change to pressed
				if(source->keymap[code].enable_longpress) {
					source->keymap[code].time=0;
					source->keymap[code].state = KEYSTATE_LONGPRESS_WAIT;
				}
				else {
					*pvalue = 1;
					rc = 1;
					source->keymap[code].time=0;
					source->keymap[code].state = KEYSTATE_PRESSED;
				}
			}
			break;

		case KEYSTATE_PRESSED:
			if(!value && source->keymap[code].time>50) {
				// change to released
				source->keymap[code].time=0;
				source->keymap[code].repeat=false;
				source->keymap[code].longpress = false;
				source->keymap[code].state = KEYSTATE_RELEASED;
				*pvalue = 0;
				rc = 1;
			}

			// key repeat
			else if((source->keymap[code].repeat && source->keymap[code].time>=200) || source->keymap[code].time>=500) {
				*pvalue = 1;
				rc = 1;
				source->keymap[code].time=0;
				source->keymap[code].repeat=true;
			}
			break;

		case KEYSTATE_LONGPRESS_WAIT:
			if(value) {
				if(!source->keymap[code].longpress && source->keymap[code].time>=500) {
					if(keys_get_state(KEY_VOLUMEDOWN)) {
						// report 's'
						keys_post_event(0x73, 1);
						keys_post_event(0x73, 0);
					}
					else if(keys_get_state(KEY_VOLUMEUP)) {
						// report 'e'
						keys_post_event(0x65, 1);
						keys_post_event(0x65, 0);
					}
					else {
						// report spacebar
						keys_post_event(32, 1);
						keys_post_event(32, 0);
					}

					source->keymap[code].longpress=true;
				}
			}

			else {
				if(!source->keymap[code].longpress) {
					// we supressed down, so report it now
					*pvalue = 1;
					rc = 1;
					source->keymap[code].state = KEYSTATE_LONGPRESS_RELEASE;
				}

				else if(source->keymap[code].time>50) {
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

#endif /* __DEV_KEYS_H */
