#include "memory.h"
#include <sys/types.h>
#include <stdlib.h>
#include <string.h>

#define BLOCKSIZE (32 * 4096)

/* Add it to the binary tree for this cluster.  The piles are stored
following the TREE structure. */

size_t MemoryManager::Mem_remain = 30 * 1000 * 1000;

MemoryManager::inscode MemoryManager::insert_node(TREE *n, int d, TREE **tree, TREE **node)
{
        int c;
	u_char *key, *tkey;
	TREE *t;

	key = (u_char *)n + sizeof(TREE);
	n->depth = d;
	n->left = n->right = NULL;
	*node = n;
	t = *tree;
	if (t == NULL) {
		*tree = n;
		return NEW;
	}
	while (1) {
		tkey = (u_char *)t + sizeof(TREE);
		c = memcmp(key, tkey, Pilebytes);
		if (c == 0) {
			break;
		}
		if (c < 0) {
			if (t->left == NULL) {
				t->left = n;
				return NEW;
			}
			t = t->left;
		} else {
			if (t->right == NULL) {
				t->right = n;
				return NEW;
			}
			t = t->right;
		}
	}

	/* We get here if it's already in the tree.  Don't add it again.
	If the new path to this position was shorter, record the new depth
	so we can prune the original path. */

        return FOUND;
}

/* Given a cluster number, return a tree.  There are 14^4 possible
clusters, but we'll only use a few hundred of them at most.  Hash on
the cluster number, then locate its tree, creating it if necessary. */

#define TBUCKETS 499    /* a prime */

TREELIST *Treelist[TBUCKETS];

/* Clusters are also stored in a hashed array. */

void MemoryManager::init_clusters(void)
{
	memset(Treelist, 0, sizeof(Treelist));
	Block = new_block();                    /* @@@ */
}

TREELIST *MemoryManager::cluster_tree(int cluster)
{
	int bucket;
	TREELIST *tl, *last;

	/* Pick a bucket, any bucket. */

	bucket = cluster % TBUCKETS;

	/* Find the tree in this bucket with that cluster number. */

	last = NULL;
	for (tl = Treelist[bucket]; tl; tl = tl->next) {
		if (tl->cluster == cluster) {
			break;
		}
		last = tl;
	}

	/* If we didn't find it, make a new one and add it to the list. */

	if (tl == NULL) {
		tl = allocate(TREELIST);
		if (tl == NULL) {
			return NULL;
		}
		tl->tree = NULL;
		tl->cluster = cluster;
		tl->next = NULL;
		if (last == NULL) {
			Treelist[bucket] = tl;
		} else {
			last->next = tl;
		}
	}

	return tl;
}

/* Block storage.  Reduces overhead, and can be freed quickly. */

BLOCK *MemoryManager::new_block(void)
{
	BLOCK *b;

	b = allocate(BLOCK);
	if (b == NULL) {
		return NULL;
	}
	b->block = new_array(u_char, BLOCKSIZE);
	if (b->block == NULL) {
                MemoryManager::free_ptr(b);
		return NULL;
	}
	b->ptr = b->block;
	b->remain = BLOCKSIZE;
	b->next = NULL;

	return b;
}

/* Like new(), only from the current block.  Make a new block if necessary. */

u_char *MemoryManager::new_from_block(size_t s)
{
	u_char *p;
	BLOCK *b;

	b = Block;
	if (s > b->remain) {
		b = new_block();
		if (b == NULL) {
			return NULL;
		}
		b->next = Block;
		Block = b;
	}

	p = b->ptr;
	b->remain -= s;
	b->ptr += s;

	return p;
}

/* Return the previous result of new_from_block() to the block.  This
can ONLY be called once, immediately after the call to new_from_block().
That is, no other calls to new_from_block() are allowed. */

void MemoryManager::give_back_block(u_char *p)
{
	size_t s;
	BLOCK *b;

	b = Block;
	s = b->ptr - p;
	b->ptr -= s;
	b->remain += s;
}

void MemoryManager::free_blocks(void)
{
	BLOCK *b, *next;

	b = Block;
	while (b) {
		next = b->next;
                MemoryManager::free_array(b->block, BLOCKSIZE);
                MemoryManager::free_ptr(b);
		b = next;
	}
}

void MemoryManager::free_clusters(void)
{
	int i;
	TREELIST *l, *n;

	for (i = 0; i < TBUCKETS; i++) {
		l = Treelist[i];
		while (l) {
			n = l->next;
                        MemoryManager::free_ptr(l);
			l = n;
		}
	}
}

/* Allocate some space and return a pointer to it.  See new() in util.h. */

void *MemoryManager::allocate_memory(size_t s)
{
	void *x;

	if (s > Mem_remain) {
		return NULL;
	}

	if ((x = (void *)malloc(s)) == NULL) {
		return NULL;
	}

        memset( x, 0, s );
	Mem_remain -= s;
	return x;
}
