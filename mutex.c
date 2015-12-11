/*
 * Copyright (c) 2008-2009 Travis Geiselbrecht
 *
 * Permission is hereby granted, free of charge, to any person obtaining
 * a copy of this software and associated documentation files
 * (the "Software"), to deal in the Software without restriction,
 * including without limitation the rights to use, copy, modify, merge,
 * publish, distribute, sublicense, and/or sell copies of the Software,
 * and to permit persons to whom the Software is furnished to do so,
 * subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be
 * included in all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
 * EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
 * MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.
 * IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY
 * CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT,
 * TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE
 * SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
 */

/**
 * @file
 * @brief  Mutex functions
 *
 * @defgroup mutex Mutex
 * @{
 */

#include <debug.h>
#include <err.h>
#include <kernel/mutex.h>
#include <kernel/thread.h>

#if DEBUGLEVEL > 1
#define MUTEX_CHECK 1
#endif

/**
 * @brief  Initialize a mutex_t
 */
void mutex_init(mutex_t *m)
{
#if MUTEX_CHECK
//	ASSERT(m->magic != MUTEX_MAGIC);
#endif

	m->magic = MUTEX_MAGIC;
	m->count = 0;
	m->holder = 0;
}

/**
 * @brief  Destroy a mutex_t
 *
 * This function frees any resources that were allocated
 * in mutex_init().  The mutex_t object itself is not freed.
 */
void mutex_destroy(mutex_t *m)
{
#if MUTEX_CHECK
	ASSERT(m->magic == MUTEX_MAGIC);
#endif

	m->magic = 0;
	m->count = 0;
    
    if(m->count-- > 0)
    	exit_critical_section();
}

/**
 * @brief  Acquire a mutex; wait if needed.
 *
 * This function waits for a mutex to become available.  It
 * may wait forever if the mutex never becomes free.
 *
 * @return  NO_ERROR on success, other values on error
 */
status_t mutex_acquire(mutex_t *m)
{
	status_t ret = NO_ERROR;

	enter_critical_section();

#if MUTEX_CHECK
	ASSERT(m->magic == MUTEX_MAGIC);
#endif

	m->count++;

	return ret;
}

/**
 * @brief  Release mutex
 */
status_t mutex_release(mutex_t *m)
{
#if MUTEX_CHECK
	ASSERT(m->magic == MUTEX_MAGIC);
#endif

	m->count--;

	exit_critical_section();

	return NO_ERROR;
}

