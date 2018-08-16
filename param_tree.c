
#include <stdlib.h>
#include <string.h>

#include "param_tree.h"



static const char spacer = '\t';
static const char node_prefix = '$';



param_tree_node_t ParamTreeMainNode;

param_tree_node_t* ParamTree_MainNode() { return &ParamTreeMainNode; }


static void* node_mem = NULL;
static param_tree_node_t* cur_node_ptr = NULL;

static char* string_end;





static char* skip_spacers(char* ptr) {

	while (ptr!=string_end && *ptr==spacer)
		ptr++;

	return ptr;
}


static char* FindNextString(char* ptr) {

	while (ptr != string_end)
		if (*ptr++ == '\0')
			break;

	return ptr;
}


static char* set_name_value(char** name, char** value, char* ptr) {

	*name = ptr;
	*value = NULL;

	while (ptr != string_end && *ptr != '\0') {
		if (*ptr == '=') {
			*ptr = '\0';
			ptr++;
			if (ptr != string_end && *ptr != '\0') {
				*value = ptr;
				break;
			}
		}
		else
			ptr++;
	}

	return ptr;
}


static void add_item(param_tree_node_t* node, param_tree_node_t* item) {

	if (node->child == NULL) {
		node->child = item;
		node->next = NULL;
	}
	else {
		param_tree_node_t* n = node->child;
		while (n->next != NULL)
			n = n->next;
		n->next = item;
	}

}


static char* NodeAddItems(param_tree_node_t* node, char* str) {

	char* ptr = str;
	int state = 0;

	while (ptr != string_end) {
		switch (state) {
		case 0:
			ptr = skip_spacers(ptr);
			if (ptr != string_end)
				state = (*ptr == node_prefix) ? 1 : 2;
			break;

		case 1:    //node
			ptr++;
			if (ptr == string_end)
				return ptr;
			if (*ptr == node_prefix)  //"$$" check
				return FindNextString(ptr);
			//------  add node ------------------
			{
				param_tree_node_t* tmp_node_ptr = cur_node_ptr++;
				tmp_node_ptr->name = ptr;
				tmp_node_ptr->value = NULL;
				tmp_node_ptr->child = NULL;
				tmp_node_ptr->next = NULL;
				add_item(node, tmp_node_ptr);

				ptr = FindNextString(ptr);
				ptr = NodeAddItems(tmp_node_ptr, ptr);
			}
			//-----------------------------------
			state = 0;
			break;

		case 2:    //item	          
			ptr = set_name_value(&cur_node_ptr->name, &cur_node_ptr->value, ptr);
			cur_node_ptr->child = NULL;
			cur_node_ptr->next = NULL;
			add_item(node, cur_node_ptr);
			cur_node_ptr++;

			ptr = FindNextString(ptr);
			state = 0;
			break;
		}

	}


	return ptr;
}


static void make_strings(char* from, char* till) {

	while (from != till) {
		if (*from == '\n')
			*from = '\0';
		from++;
	}

}



int ParamTree_Make(char* data, unsigned size) {

	unsigned str_num = 0;
	unsigned i = 0;

	//remove all utf8 characters after first occurence of characeter with code > 127
	while (i < size) {
		if ((unsigned char)data[i] > 127) {
			data[i] = '\0';
			break;
		}
		i++;
	}

	//realloc data
	if (i < size) {
		void* new_data = realloc(data, i);
		if (new_data)
			data = new_data;
		size = i;
	}

	i = 0;
	while (i < size)
		if (data[i++] == '\n')
			str_num++;


	//alloc memory for nodes
	node_mem = calloc(str_num, sizeof(param_tree_node_t));
	if (!node_mem)
		return -1;

	cur_node_ptr = node_mem;

	string_end = data + size;
	make_strings(data, string_end);

	if (!NodeAddItems(&ParamTreeMainNode, data)) {
		free(node_mem);
		return -1;
	}

	//realloc nodes memory
	unsigned new_item_num = ((char*)cur_node_ptr - (char*)node_mem) / (sizeof(param_tree_node_t));
	void* new_node_mem = realloc(node_mem, new_item_num * sizeof(param_tree_node_t));

	if (!new_node_mem)
		return 0;

	unsigned offset = (unsigned)new_node_mem - (unsigned)node_mem;   //  (!!!) ISO/IEC 9899:201x :  6.5.6 - 9 (substruction of pointers), 6.5.8, 6.5.9.    Can't compare pointers of different mallocs directly!!!
	if (offset) {

		node_mem = new_node_mem;
		ParamTreeMainNode.child = node_mem;

		param_tree_node_t* node_ptr = node_mem;
		for (i = 0; i<new_item_num; i++, node_ptr++) {
			if (node_ptr->name) ((unsigned)node_ptr->name) += offset;
			if (node_ptr->value) ((unsigned)node_ptr->value) += offset;
			if (node_ptr->child) ((unsigned)node_ptr->child) += offset;
			if (node_ptr->next) ((unsigned)node_ptr->next) += offset;
		}

	}

	return 0;
}


//  search functions and helpers  ---------------------


param_tree_node_t* ParamTree_Find(param_tree_node_t* node, char* name, enum ParamTree_SEARCH_TYPE s_type) {

	param_tree_node_t* n = node->child;

	while (n) {
		if (((s_type == PARAM_TREE_SEARCH_NODE && n->value == NULL) || (s_type == PARAM_TREE_SEARCH_ITEM && n->value != NULL) || s_type == PARAM_TREE_SEARCH_ANY)
			&& (strcmp(n->name, name) == 0) )
			break;
		n = n->next;
	}

	return n;
}

param_tree_node_t* ParamTree_Child(param_tree_node_t* node) { return node?node->child:NULL; }


param_tree_node_t* ParamTree_FirstItem(param_tree_node_t* node) {

	while (node && !node->value)
		node = node->next;

	return node;
}

param_tree_node_t* ParamTree_NextItem(param_tree_node_t* node) {

	if (node && node->value) {
		node = node->next;
		if (node && node->value)
			return node;
	}

	return NULL;
}

