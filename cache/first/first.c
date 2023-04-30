#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>

typedef struct Block {
	int valid;
	long tag;
} Block;

typedef struct Node {
	int val;
	struct Node* next;
} Node;

typedef struct LinkedList {
	Node* head;
	Node* tail;
	int size;
	int capacity;
} LinkedList;

void enqueue(LinkedList* ll, int val) {
	Node* new_node = malloc(sizeof(Node));
	new_node->val = val;
	new_node->next = NULL;
	if (ll->head == NULL && ll->tail == NULL) {
		ll->head = new_node;
		ll->tail = new_node;
	} else {
		ll->tail->next = new_node;
		ll->tail = new_node;
	}
	ll->size++;
}

int dequeue(LinkedList* ll) {
	if (ll->head == NULL) {
		return 0;
	}
	Node* old_head = ll->head;
	int pos = old_head->val;
	if (ll->head == ll->tail) {
		ll->head = NULL;
		ll->tail = NULL;
	} else {
		ll->head = ll->head->next;
	}
	free(old_head);
	ll->size--;
	return pos;
}

void remove_node(LinkedList* ll, int val) {
	Node* dummy_head = malloc(sizeof(Node));
	dummy_head->next = ll->head;
    Node* curr = dummy_head;
    while (curr->next != NULL) {
        if (curr->next->val != val) {
            curr = curr->next;
        } else {
            Node* temp = curr->next;
            curr->next = curr->next->next;
			ll->size--;
			if (temp == ll->head && temp == ll->tail) {
				ll->head = NULL;
				ll->tail = NULL;
			} else if (temp == ll->head) {
				ll->head = ll->head->next;
			} else if (temp == ll->tail) {
				ll->tail = curr;
			}
            free(temp);
			break;
        }
    }
	free(dummy_head);
}

LinkedList** init_arr_ll(int sets, int assoc) {
	LinkedList** arr_ll = malloc(sets * sizeof(LinkedList*));
	for (int i = 0; i < sets; i++) {
		arr_ll[i] = malloc(sizeof(LinkedList));
		arr_ll[i]->head = NULL;
		arr_ll[i]->tail = NULL;
		arr_ll[i]->size = 0;
		arr_ll[i]->capacity = assoc;
	}
	return arr_ll;
}

Block*** init_cache(int sets, int assoc) {
	Block*** cache = malloc(sets * sizeof(Block**));
	for (int i = 0; i < sets; i++) {
		cache[i] = malloc(assoc * sizeof(Block*));
		for (int j = 0; j < assoc; j++) {
			cache[i][j] = malloc(sizeof(Block));
		}
	}
	return cache;
}

void free_arr_ll(LinkedList** arr_ll, int sets) {
	for (int i = 0; i < sets; i++) {
		Node* curr = arr_ll[i]->head;
		while (curr != NULL) {
			Node* temp = curr->next;
			free(curr);
			curr = temp;
		}
		free(arr_ll[i]);
	}
	free(arr_ll);
}

void free_cache(Block*** cache, int sets, int assoc) {
	for (int i = 0; i < sets; i++) {
		for (int j = 0; j < assoc; j++) {
			free(cache[i][j]);
		}
		free(cache[i]);
	}
	free(cache);
}

int lookup(Block*** cache, LinkedList** arr_ll, long tag, int idx, int assoc, char* cache_policy) {
	int i = 0;
	while (i < assoc) {
		if (tag == cache[idx][i]->tag && cache[idx][i]->valid == 1) {
			if (strcmp(cache_policy, "lru") == 0) {
				remove_node(arr_ll[idx], i);
				enqueue(arr_ll[idx], i);
			}
			return i;
		}
		i++;
	}
	return -1;
}

long insert(Block*** cache, LinkedList** arr_ll, long tag, int idx) {
	long evicted_tag = -1;
	int pos;
	if (arr_ll[idx]->size == arr_ll[idx]->capacity) {
		pos = dequeue(arr_ll[idx]);
		evicted_tag = cache[idx][pos]->tag;
	} else {
		pos = arr_ll[idx]->size;
	}
	enqueue(arr_ll[idx], pos);
	cache[idx][pos]->tag = tag;
	cache[idx][pos]->valid = 1;
	return evicted_tag;
}

int mem_reads = 0, mem_writes = 0, cache_hits = 0, cache_misses = 0;

int main(int argc, char **argv) {
	int cache_size = atoi(argv[1]);
	int assoc = atoi(argv[2] + 6);
	char* cache_policy = argv[3];
	int block_size = atoi(argv[4]);
	FILE* trace_file = fopen(argv[5], "r");

	int sets = cache_size / (block_size * assoc);
	int offset_bits = log(block_size) / log(2);
	int idx_bits = log(sets) / log(2);

	Block*** cache = init_cache(sets, assoc);
	LinkedList** arr_ll = init_arr_ll(sets, assoc);

	char op[2];
	long addr;

	while (fscanf(trace_file, "%s %lx", op, &addr) != EOF) {
		long tag = addr >> (offset_bits + idx_bits);
		int idx = (addr >> offset_bits) & ((1 << idx_bits) - 1);
		// int offset = addr & ((1 << offset_bits) - 1);

		if (strcmp(op, "W") == 0) {
			mem_writes++;
		}
		int pos;
		if ((pos = lookup(cache, arr_ll, tag, idx, assoc, cache_policy)) != -1) {
			cache_hits++;
		} else {
			cache_misses++;
			mem_reads++;
			insert(cache, arr_ll, tag, idx);
		}
	}

	free_arr_ll(arr_ll, sets);
	free_cache(cache, sets, assoc);

	printf("memread:%d\n", mem_reads);
	printf("memwrite:%d\n", mem_writes);
	printf("cachehit:%d\n", cache_hits);
	printf("cachemiss:%d\n", cache_misses);

	return 0;
}
