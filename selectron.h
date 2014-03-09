// Selectron prototype
//
// Patrick Walton <pcwalton@mozilla.com>
//
// Copyright (c) 2014 Mozilla Corporation

#ifndef SELECTRON_H
#define SELECTRON_H

#include <mach/mach.h>
#include <mach/mach_time.h>

#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

#define NODE_COUNT 131072
#define THREAD_COUNT 1024
#define MAX_DOM_DEPTH 10

#define ESTIMATED_PARALLEL_SPEEDUP  2.7

#define RULE_ID_MAX 25
#define NODE_ID_MAX 50
#define RULE_TAG_NAME_MAX 8
#define NODE_TAG_NAME_MAX 12

#define CSS_SELECTOR_TYPE_NONE      0
#define CSS_SELECTOR_TYPE_ID        1
#define CSS_SELECTOR_TYPE_TAG_NAME  2

#define HASH_SIZE   256

#ifdef MAX
#undef MAX
#endif
#define MAX(a,b)    ((a) > (b) ? (a) : (b))

#define LEFT_SEED   10
#define RIGHT_SEED  20

// FIXME(pcwalton): This is not really implemented properly; it should resize the table.

struct css_rule {
    int32_t type;
    int32_t value;
};

struct css_cuckoo_hash {
    int32_t left_seed;
    int32_t right_seed;
    css_rule left[HASH_SIZE];
    css_rule right[HASH_SIZE];
};

#define CSS_RULE_HASH(key, seed) \
    do {\
        uint32_t hash = 2166136261; \
        hash = hash ^ seed; \
        hash = hash * 16777619; \
        hash = hash ^ key; \
        hash = hash * 16777619; \
        return hash; \
    } while(0)

uint32_t css_rule_hash(uint32_t key, uint32_t seed) {
    CSS_RULE_HASH(key, seed);
}

void css_cuckoo_hash_reset(css_cuckoo_hash *hash) {
    for (int i = 0; i < HASH_SIZE; i++) {
        hash->left[i].type = 0;
        hash->right[i].type = 0;
    }
}

void css_cuckoo_hash_reseed(css_cuckoo_hash *hash) {
    hash->left_seed = rand();
    hash->right_seed = rand();
}

void css_cuckoo_hash_init(css_cuckoo_hash *hash) {
    css_cuckoo_hash_reset(hash);
    css_cuckoo_hash_reseed(hash);
}

void css_cuckoo_hash_rehash(css_cuckoo_hash *hash) {
    fprintf(stderr, "rehash unimplemented\n");
    abort();
}

bool css_cuckoo_hash_insert_internal(css_cuckoo_hash *hash, css_rule *rule, bool right) {
    int hashval = css_rule_hash(rule->value, right ? RIGHT_SEED : LEFT_SEED);
    int index = hashval % HASH_SIZE;
    css_rule *list = right ? hash->right : hash->left;
    if (list[index].type != 0) {
        if (!css_cuckoo_hash_insert_internal(hash, &list[index], !right))
            return false;
    }

    list[index] = *rule;
    return true;
}

void css_cuckoo_hash_insert(css_cuckoo_hash *hash, css_rule *rule) {
    if (css_cuckoo_hash_insert_internal(hash, rule, false))
        return;
    css_cuckoo_hash_reseed(hash);
    css_cuckoo_hash_rehash(hash);
    if (css_cuckoo_hash_insert_internal(hash, rule, false))
        return;
    fprintf(stderr, "rehashing failed\n");
    abort();
}

#define CSS_CUCKOO_HASH_FIND(hash, key, hashfn) \
    do {\
        int left_index = hashfn(key, LEFT_SEED) % HASH_SIZE; \
        if (hash->left[left_index].type != 0 && hash->left[left_index].value == key) \
            return &hash->left[left_index]; \
        int right_index = hashfn(key, RIGHT_SEED) % HASH_SIZE; \
        if (hash->right[right_index].type != 0 && hash->right[right_index].value == key) \
            return &hash->right[right_index]; \
        return NULL; \
    } while(0)

#define CSS_CUCKOO_HASH_FIND_PRECOMPUTED(hash, key, left_index, right_index) \
    do {\
        if (hash->left[left_index].type != 0 && hash->left[left_index].value == key) \
            return &hash->left[left_index]; \
        if (hash->right[right_index].type != 0 && hash->right[right_index].value == key) \
            return &hash->right[right_index]; \
        return NULL; \
    } while(0)

css_rule *css_cuckoo_hash_find(css_cuckoo_hash *hash, int32_t key) {
    CSS_CUCKOO_HASH_FIND(hash, key, css_rule_hash);
}

css_rule *css_cuckoo_hash_find_precomputed(css_cuckoo_hash *hash,
                                           int32_t key,
                                           int32_t left_index,
                                           int32_t right_index) {
    CSS_CUCKOO_HASH_FIND_PRECOMPUTED(hash, key, left_index, right_index);
}

struct css_stylesheet_source {
    css_cuckoo_hash ids;
    css_cuckoo_hash tag_names;
};

struct css_stylesheet {
    css_stylesheet_source author;
    css_stylesheet_source user_agent;
};

struct dom_node {
    struct dom_node *parent;
    struct dom_node *first_child;
    struct dom_node *last_child;
    struct dom_node *next_sibling;
    struct dom_node *prev_sibling;
    int32_t id;
    int32_t tag_name;
    int32_t applicable_declaration_count;
    struct css_rule applicable_declarations[16];
};

#define MATCH_SELECTORS_HASH(node, hash, findfn) \
    do {\
        const css_rule *__restrict__ rule = findfn(hash, node->id); \
        if (rule != NULL) \
            node->applicable_declarations[node->applicable_declaration_count++] = *rule; \
    } while(0)

#define MATCH_SELECTORS(first, stylesheet, index, findfn) \
    do {\
        dom_node *node = &first[index]; \
        node->applicable_declaration_count = 0; \
        MATCH_SELECTORS_HASH(node, &stylesheet->author.ids, findfn); \
        MATCH_SELECTORS_HASH(node, &stylesheet->author.tag_names, findfn); \
        MATCH_SELECTORS_HASH(node, &stylesheet->user_agent.ids, findfn); \
        MATCH_SELECTORS_HASH(node, &stylesheet->user_agent.tag_names, findfn); \
    } while(0)

#define MATCH_SELECTORS_HASH_PRECOMPUTED(node, hash, findfn, left_index, right_index) \
    do {\
        const css_rule *__restrict__ rule = findfn(hash, node->id, left_index, right_index); \
        if (rule != NULL) \
            node->applicable_declarations[node->applicable_declaration_count++] = *rule; \
    } while(0)

#define MATCH_SELECTORS_PRECOMPUTED(first, stylesheet, index, findfn, hashfn) \
    do {\
        dom_node *node = &first[index]; \
        node->applicable_declaration_count = 0; \
        int32_t left_id_index = hashfn(node->id, LEFT_SEED) % HASH_SIZE; \
        int32_t right_id_index = hashfn(node->id, RIGHT_SEED) % HASH_SIZE; \
        int32_t left_tag_name_index = hashfn(node->tag_name, LEFT_SEED) % HASH_SIZE; \
        int32_t right_tag_name_index = hashfn(node->tag_name, RIGHT_SEED) % HASH_SIZE; \
        MATCH_SELECTORS_HASH_PRECOMPUTED(node, \
                                         &stylesheet->author.ids, \
                                         findfn, \
                                         left_id_index, \
                                         right_id_index); \
        MATCH_SELECTORS_HASH_PRECOMPUTED(node, \
                                         &stylesheet->author.tag_names, \
                                         findfn, \
                                         left_tag_name_index, \
                                         right_tag_name_index); \
        MATCH_SELECTORS_HASH_PRECOMPUTED(node, \
                                         &stylesheet->user_agent.ids, \
                                         findfn, \
                                         left_id_index, \
                                         right_id_index); \
        MATCH_SELECTORS_HASH_PRECOMPUTED(node, \
                                         &stylesheet->user_agent.tag_names, \
                                         findfn, \
                                         left_tag_name_index, \
                                         right_tag_name_index); \
    } while(0)

void match_selectors(dom_node *first, css_stylesheet *stylesheet, int32_t index) {
#if 0
    MATCH_SELECTORS(first, stylesheet, index, css_cuckoo_hash_find);
#endif
    MATCH_SELECTORS_PRECOMPUTED(first,
                                stylesheet,
                                index,
                                css_cuckoo_hash_find_precomputed,
                                css_rule_hash);
}

void create_stylesheet(css_stylesheet *stylesheet) {
    css_cuckoo_hash_init(&stylesheet->author.ids);
    css_cuckoo_hash_init(&stylesheet->author.tag_names);
    css_cuckoo_hash_init(&stylesheet->user_agent.ids);
    css_cuckoo_hash_init(&stylesheet->user_agent.tag_names);

    for (int i = 0; i < RULE_ID_MAX; i++) {
        css_rule rule = { CSS_SELECTOR_TYPE_ID, i };
        css_cuckoo_hash_insert(&stylesheet->author.ids, &rule);
    }
    for (int i = 0; i < RULE_ID_MAX; i++) {
        css_rule rule = { CSS_SELECTOR_TYPE_ID, i };
        css_cuckoo_hash_insert(&stylesheet->user_agent.ids, &rule);
    }
    for (int i = 0; i < RULE_TAG_NAME_MAX; i++) {
        css_rule rule = { CSS_SELECTOR_TYPE_TAG_NAME, i };
        css_cuckoo_hash_insert(&stylesheet->author.tag_names, &rule);
    }
    for (int i = 0; i < RULE_TAG_NAME_MAX; i++) {
        css_rule rule = { CSS_SELECTOR_TYPE_TAG_NAME, i };
        css_cuckoo_hash_insert(&stylesheet->user_agent.tag_names, &rule);
    }
}

void create_dom(dom_node *dest, dom_node *parent, int *global_count, int depth) {
    if (*global_count == NODE_COUNT)
        return;
    if (depth == MAX_DOM_DEPTH)
        return;

    dom_node *node = &dest[(*global_count)++];
    node->id = rand() % NODE_ID_MAX;
    node->tag_name = rand() % NODE_TAG_NAME_MAX;
    node->applicable_declaration_count = 0;

    node->first_child = node->last_child = node->next_sibling = NULL;
    if ((node->parent = parent) != NULL) {
        if (node->parent->last_child != NULL) {
            node->prev_sibling = node->parent->last_child;
            node->prev_sibling->next_sibling = node->parent->last_child = node;
        } else {
            node->parent->first_child = node->parent->last_child = node;
            node->prev_sibling = NULL;
        }
    }

    int child_count = rand() % (NODE_COUNT / 100);
    for (int i = 0; i < child_count; i++)
        create_dom(dest, node, global_count, depth + 1);
}

void munge_dom_pointers(dom_node *node, ptrdiff_t offset) {
    for (int i = 0; i < NODE_COUNT; i++) {
        node->parent = (dom_node *)((ptrdiff_t)node->parent + offset);
        node->first_child = (dom_node *)((ptrdiff_t)node->first_child + offset);
        node->last_child = (dom_node *)((ptrdiff_t)node->last_child + offset);
        node->next_sibling = (dom_node *)((ptrdiff_t)node->next_sibling + offset);
        node->prev_sibling = (dom_node *)((ptrdiff_t)node->prev_sibling + offset);
    }
}

void check_dom(dom_node *node) {
    for (int i = 0; i < 20; i++) {
        printf("%d -> %d\n", node[i].id, node[i].applicable_declaration_count);
    }
}

// Frame tree

struct frame {
    struct dom_node *node;
    int32_t type;
};

void create_frame(struct dom_node *first, int i) {
    struct frame *frame = (struct frame *)malloc(sizeof(struct frame));
    frame->node = &first[i];
    frame->type = 0;
}

#endif

