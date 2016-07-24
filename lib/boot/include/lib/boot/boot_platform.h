/*
 * Copyright 2016, The EFIDroid Project
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
*/

#ifndef LIB_BOOT_PLATFORM_H
#define LIB_BOOT_PLATFORM_H

#include <stdint.h>
#include <limits.h>
#include <assert.h>
#include <stdio.h>

#define LIBBOOT_FMT_UINTN "lu"
#define LIBBOOT_FMT_UINT32 "u"
#define LIBBOOT_FMT_ADDR "lx"
#define LIBBOOT_FMT_INT "d"

#define LIBBOOT_ASSERT assert
#define LIBBOOT_OFFSETOF(StrucName, Member)  offsetof(StrucName, Member)

#define LOGV(fmt, ...) dprintf(SPEW, fmt, ##__VA_ARGS__)
#define LOGE(fmt, ...) dprintf(CRITICAL, fmt, ##__VA_ARGS__)
#define LOGI(fmt, ...) dprintf(INFO, fmt, ##__VA_ARGS__)

typedef uintptr_t boot_uintn_t;
typedef intptr_t  boot_intn_t;
typedef uint8_t   boot_uint8_t;
typedef uint16_t  boot_uint16_t;
typedef uint32_t  boot_uint32_t;
typedef uint64_t  boot_uint64_t;

#endif // LIB_BOOT_PLATFORM_H
