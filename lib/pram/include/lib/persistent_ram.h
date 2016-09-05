/*
 * Copyright (C) 2011 Google, Inc.
 *
 * This software is licensed under the terms of the GNU General Public
 * License version 2, as published by the Free Software Foundation, and
 * may be copied, distributed, and modified under those terms.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 */

#ifndef __LINUX_PERSISTENT_RAM_H__
#define __LINUX_PERSISTENT_RAM_H__

int persistent_ram_init(addr_t start, addr_t size);

int persistent_ram_write(const void *s, unsigned int count);

int persistent_ram_old_size(void);
void *persistent_ram_old(void);
void persistent_ram_free_old(void);

#endif
