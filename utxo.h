#ifndef UTXO_H
#define UTXO_H

#include "block.h"

#define INITIAL_UTXO_CAPACITY 16

typedef struct {
    outpoint_t outpoint;
    tx_output_t output;
} utxo_entry_t;

typedef struct {
    utxo_entry_t *entries;
    size_t count;
    size_t capacity;
} utxo_set_t;

const utxo_entry_t *find_utxo(const utxo_set_t *set, const outpoint_t *outpoint);
int copy_utxo_set(utxo_set_t *destination, const utxo_set_t *source);
int apply_block_to_utxo_set(utxo_set_t *set, const block_t *block);
int get_address_balance(const utxo_set_t *set, const char *address, uint64_t *balance);
void free_utxo_set(utxo_set_t *set);

#endif // UTXO_H
