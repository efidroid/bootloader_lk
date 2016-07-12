#include <err.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <list.h>
#include <cmdline.h>

typedef struct cmdline_item {
	struct list_node node;
	char* name;
	char* value;
} cmdline_item_t;

static cmdline_item_t* cmdline_get_internal(struct list_node* list, const char* name) {
	cmdline_item_t *item;
	list_for_every_entry(list, item, cmdline_item_t, node) {
		if(!strcmp(name, item->name))
			return item;
	}

	return NULL;
}

bool cmdline_has(struct list_node* list, const char* name) {
	return !!cmdline_get_internal(list, name);
}

const char* cmdline_get(struct list_node* list, const char* name) {
	cmdline_item_t* item = cmdline_get_internal(list, name);

	if(!item)
		return NULL;

	return item->value;
}

void cmdline_add(struct list_node* list, const char* name, const char* value, bool overwrite) {
	cmdline_item_t* item = cmdline_get_internal(list, name);
	if(item) {
		if(!overwrite) return;

		list_delete(&item->node);
		free(item->name);
		free(item->value);
		free(item);
	}

	item = malloc(sizeof(cmdline_item_t));
	item->name = strdup(name);
	item->value = value?strdup(value):NULL;

	list_add_tail(list, &item->node);
}

void cmdline_remove(struct list_node* list, const char* name) {
	cmdline_item_t* item = cmdline_get_internal(list, name);
	if(item) {
		list_delete(&item->node);
		free(item->name);
		free(item->value);
		free(item);
	}
}

size_t cmdline_length(struct list_node* list) {
	size_t len = 0;

	cmdline_item_t *item;
	list_for_every_entry(list, item, cmdline_item_t, node) {
		// leading space
		if(len!=0) len++;
		// name
		len+=strlen(item->name);
		// '=' and value
		if(item->value)
			len+= 1 + strlen(item->value);
	}

	// 0 terminator
	if(len>0) len++;

	return len;
}

size_t cmdline_generate(struct list_node* list, char* buf, size_t bufsize) {
	size_t len = 0;

	if(bufsize>0)
		buf[0] = 0;

	cmdline_item_t *item;
	list_for_every_entry(list, item, cmdline_item_t, node) {
		if(len!=0) buf[len++] = ' ';
		len+=strlcpy(buf+len, item->name, bufsize-len);

		if(item->value) {
			buf[len++] = '=';
			len+=strlcpy(buf+len, item->value, bufsize-len);
		}
	}

	return len;
}

static int str2nameval(const char* str, char** name, char** value) {
	char *c;
	int index;
	char* ret_name;
	char* ret_value;

	// get index of delimiter
	c = strchr(str, '=');
	if(c==NULL) {
		*name = strdup(str);
		*value = NULL;
		return -1;
	}
	index = (int)(c - str);

	// get name
	ret_name = malloc(index+1);
	memcpy(ret_name, str, index);
	ret_name[index] = 0;

	// get value
	ret_value = strdup(str+index+1);

	*name = ret_name;
	*value = ret_value;

	return 0;
}

void cmdline_addall(struct list_node* list, const char* _cmdline, bool overwrite) {
	const char* sep = " ";

	char* cmdline = strdup(_cmdline);
	if(!cmdline) return;

	char *pch = strtok(cmdline, sep);
	while (pch != NULL) {
		// parse
		char* name = NULL;
		char* value = NULL;
		str2nameval(pch, &name, &value);

		// add
		cmdline_add(list, name, value, overwrite);

		// free
		free(name);
		free(value);

		// next
		pch = strtok(NULL, sep);
	}

	free(cmdline);
}

void cmdline_addall_list(struct list_node* list_dst, struct list_node* list_src, bool overwrite) {
	cmdline_item_t *item;
	list_for_every_entry(list_src, item, cmdline_item_t, node) {
		cmdline_add(list_dst, item->name, item->value, overwrite);
	}
}

void cmdline_init(struct list_node* list)
{
	list_initialize(list);
}

void cmdline_free(struct list_node* list)
{
	while(!list_is_empty(list)) {
		cmdline_item_t* item = list_remove_tail_type(list, cmdline_item_t, node);

		free(item->name);
		free(item->value);
		free(item);
	}
}
