#pragma once



typedef struct param_tree_node_tt {
	char* name;
	char* value;
	struct param_tree_node_tt* child;
	struct param_tree_node_tt* next;
} param_tree_node_t;


param_tree_node_t* ParamTree_MainNode();


int ParamTree_Make(char* data, unsigned size);

//  search functions and helpers  ---------------------


enum ParamTree_SEARCH_TYPE {
	PARAM_TREE_SEARCH_NODE = 1,
	PARAM_TREE_SEARCH_ITEM = 2,
	PARAM_TREE_SEARCH_ANY = 3
};



param_tree_node_t* ParamTree_Find(param_tree_node_t* node, char* name, enum ParamTree_SEARCH_TYPE s_type);

param_tree_node_t* ParamTree_Child(param_tree_node_t* node);

param_tree_node_t* ParamTree_FirstItem(param_tree_node_t* node);

param_tree_node_t* ParamTree_NextItem(param_tree_node_t* node);

unsigned ParamTree_ItemsNum(param_tree_node_t* node);
unsigned ParamTree_ChildNum(param_tree_node_t* node);
