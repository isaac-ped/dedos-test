#ifndef MSU_STATE_H_
#define MSU_STATE_H_
#include "msu_queue.h"

// Forward declaration to avoid circular dependency with generic_msu.h
struct generic_msu;

/** Reallocates the state associated with a given key for an MSU. */
void *msu_realloc_state(struct generic_msu *msu, struct queue_key *key, unsigned int key2, size_t size);
/** Initializes and returns the state assiciated with a given key. */
void *msu_init_state(struct generic_msu *msu, struct queue_key *key, unsigned int key2, size_t size);
/** Gets the state associated with a given key for a given MSU.*/
void *msu_get_state(struct generic_msu *msu, struct queue_key *key, unsigned int key2, size_t *size);
/** Frees the state associated with a given key for a given MSU */
int msu_free_state(struct generic_msu *msu, struct queue_key *key, unsigned int key2);

void msu_free_all_state(struct generic_msu *msu);
#endif
