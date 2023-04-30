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

int mem_reads = 0, mem_writes = 0, l1_cache_hits = 0, l1_cache_misses = 0, l2_cache_hits = 0, l2_cache_misses = 0;

int main(int argc, char **argv) {
	int l1_cache_size = atoi(argv[1]);
	int l1_assoc = atoi(argv[2] + 6);
	char* l1_cache_policy = argv[3];
	int l1_block_size = atoi(argv[4]);
	int l2_cache_size = atoi(argv[5]);
	int l2_assoc = atoi(argv[6] + 6);
	char* l2_cache_policy = argv[7];
	int l2_block_size = l1_block_size;
	FILE* trace_file = fopen(argv[8], "r");

	int l1_sets = l1_cache_size / (l1_block_size * l1_assoc);
	int l1_offset_bits = log(l1_block_size) / log(2);
	int l1_idx_bits = log(l1_sets) / log(2);
	int l2_sets = l2_cache_size / (l2_block_size * l2_assoc);
	int l2_offset_bits = log(l2_block_size) / log(2);
	int l2_idx_bits = log(l2_sets) / log(2);

	Block*** l1_cache = init_cache(l1_sets, l1_assoc);
	LinkedList** l1_arr_ll = init_arr_ll(l1_sets, l1_assoc);
	Block*** l2_cache = init_cache(l2_sets, l2_assoc);
	LinkedList** l2_arr_ll = init_arr_ll(l2_sets, l2_assoc);

	char op[2];
	long addr;

	while (fscanf(trace_file, "%s %lx", op, &addr) != EOF) {
		long l1_tag = addr >> (l1_offset_bits + l1_idx_bits);
		int l1_idx = (addr >> l1_offset_bits) & ((1 << l1_idx_bits) - 1);
		int l1_offset = addr & ((1 << l1_offset_bits) - 1);
		long l2_tag = addr >> (l2_offset_bits + l2_idx_bits);
		int l2_idx = (addr >> l2_offset_bits) & ((1 << l2_idx_bits) - 1);
		// int l2_offset = addr & ((1 << l2_offset_bits) - 1);

		if (strcmp(op, "W") == 0) {
			mem_writes++;
		}
		int l1_pos;
		if ((l1_pos = lookup(l1_cache, l1_arr_ll, l1_tag, l1_idx, l1_assoc, l1_cache_policy)) != -1) {
			l1_cache_hits++;
		} else {
			l1_cache_misses++;
			int l2_pos;
			if ((l2_pos = lookup(l2_cache, l2_arr_ll, l2_tag, l2_idx, l2_assoc, l2_cache_policy)) != -1) {
				l2_cache_hits++;
				remove_node(l2_arr_ll[l2_idx], l2_pos);
				l2_cache[l2_idx][l2_pos]->tag = 0;
				l2_cache[l2_idx][l2_pos]->valid = 0;
			} else {
				l2_cache_misses++;
				mem_reads++;
			}
			long l1_evicted_tag = insert(l1_cache, l1_arr_ll, l1_tag, l1_idx);
			if (l1_evicted_tag != -1) {
				long evicted_addr = (l1_evicted_tag << (l1_offset_bits + l1_idx_bits)) | (l1_idx << l1_offset_bits) | l1_offset;
				long l2_evicted_tag = evicted_addr >> (l2_offset_bits + l2_idx_bits);
				int l2_evicted_idx = (evicted_addr >> l2_offset_bits) & ((1 << l2_idx_bits) - 1);
				insert(l2_cache, l2_arr_ll, l2_evicted_tag, l2_evicted_idx);
			}
		}
	}

	free_arr_ll(l1_arr_ll, l1_sets);
	free_cache(l1_cache, l1_sets, l1_assoc);
	free_arr_ll(l2_arr_ll, l2_sets);
	free_cache(l2_cache, l2_sets, l2_assoc);

	printf("memread:%d\n", mem_reads);
	printf("memwrite:%d\n", mem_writes);
	printf("l1cachehit:%d\n", l1_cache_hits);
	printf("l1cachemiss:%d\n", l1_cache_misses);
	printf("l2cachehit:%d\n", l2_cache_hits);
	printf("l2cachemiss:%d\n", l2_cache_misses);

	return 0;
}
