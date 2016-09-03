/* tools/mkbootimg/bootimg.h
**
** Copyright 2007, The Android Open Source Project
**
** Licensed under the Apache License, Version 2.0 (the "License");
** you may not use this file except in compliance with the License.
** You may obtain a copy of the License at
**
**     http://www.apache.org/licenses/LICENSE-2.0
**
** Unless required by applicable law or agreed to in writing, software
** distributed under the License is distributed on an "AS IS" BASIS,
** WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
** See the License for the specific language governing permissions and
** limitations under the License.
*/

#ifndef _LIB_CMDLINE_H_
#define _LIB_CMDLINE_H_

#include <list.h>

#define CMDLINE_ADD(str) ptr+=strlcpy((ptr), (str), cmdline_size)
#define CMDLINE_BASEBAND(type, str) \
    case (type): \
        cmdline_add("androidboot.baseband", (str)); \
        break;

bool cmdline_has(struct list_node *list, const char *name);
const char *cmdline_get(struct list_node *list, const char *name);
void cmdline_add(struct list_node *list, const char *name, const char *value, bool overwrite);
void cmdline_remove(struct list_node *list, const char *name);
size_t cmdline_length(struct list_node *list);
size_t cmdline_generate(struct list_node *list, char *buf, size_t bufsize);
void cmdline_addall(struct list_node *list, const char *cmdline, bool overwrite);
void cmdline_addall_list(struct list_node *list_dst, struct list_node *list_src, bool overwrite);
void cmdline_init(struct list_node *list);
void cmdline_free(struct list_node *list);


#endif
