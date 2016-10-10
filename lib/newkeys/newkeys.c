/*
 * Copyright (c) 2008, Google Inc.
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

#include <err.h>
#include <bits.h>
#include <list.h>
#include <debug.h>
#include <string.h>
#include <malloc.h>
#include <dev/keys.h>

#define LOCAL_TRACE 0

// CLASSIC BITMAP
static unsigned long key_bitmap[BITMAP_NUM_WORDS(MAX_KEYS)];

// NEW EVENTS
typedef struct {
	struct list_node node;
	uint16_t code;
	uint16_t value;
} key_event_t;

static struct list_node event_queue;
static struct list_node event_sources;

void keys_init(void)
{
	memset(key_bitmap, 0, sizeof(key_bitmap));
	list_initialize(&event_queue);
	list_initialize(&event_sources);
}

int keys_post_event(uint16_t code, int16_t value)
{
	int i;

	if (code >= MAX_KEYS) {
		LTRACEF("Invalid keycode posted: %d\n", code);
		return ERR_INVALID_ARGS;
	}

	if (value)
		bitmap_set(key_bitmap, code);
	else
		bitmap_clear(key_bitmap, code);

	LTRACEF("key state change: %d %d\n", code, value);

	if(!value) return NO_ERROR;

    // EVENTS

	uint8_t seq[3];
	uint8_t seqsz = 0;

	if(code<=0xff) {
		seq[seqsz++] = code;
	}
	else {
		switch(code) {
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

	for(i=0; i<seqsz; i++) {
		// create event
		key_event_t* event = malloc(sizeof(key_event_t));
		event->code = seq[i];
		event->value = value;

		// add event
		list_add_tail(&event_queue, &event->node);
	}

	return NO_ERROR;
}

int keys_get_state(uint16_t code)
{
	if (code >= MAX_KEYS) {
		LTRACEF("Invalid keycode requested: %d\n", code);
		return ERR_INVALID_ARGS;
	}

	return bitmap_test(key_bitmap, code);
}


// EVENT FUNCTIONS

int keys_poll(void)
{
	key_event_source_t *source;
	list_for_every_entry(&event_sources, source, key_event_source_t, node) {
		// update time
		time_t current = current_time();
		source->delta = current-source->last;
		source->last = current;

		source->poll(source);
	}

	return 0;
}

void keys_add_source(key_event_source_t* source) {
	int i;

	source->last = INFINITE_TIME;

	for(i=0; i<MAX_KEYS; i++) {
		source->keymap[i].time = 0;
		source->keymap[i].repeat = false;
		source->keymap[i].longpress = false;
		source->keymap[i].state = KEYSTATE_RELEASED;
	}

	list_add_tail(&event_sources, &source->node);
}

void keys_clear_all(void)
{
	memset(key_bitmap, 0, sizeof(key_bitmap));

	while(keys_has_next()) {
		key_event_t* event = list_remove_tail_type(&event_queue, key_event_t, node);
		free(event);
	}
}

int keys_get_next(uint16_t* code, uint16_t* value)
{
	// wait for next event
	if(!keys_has_next())
		return ERR_NOT_READY;

	// get event
	key_event_t* event = list_remove_head_type(&event_queue, key_event_t, node);

	// set result code
	*code = event->code;
	*value = event->value;

	// free event
	free(event);

	return NO_ERROR;
}

int keys_has_next(void)
{
	return !list_is_empty(&event_queue);
}
