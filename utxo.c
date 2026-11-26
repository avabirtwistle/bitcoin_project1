#include "utxo.h"

#include <string.h>

//helpers
static int resize_utxo_set(utxo_set_t *set);
static int add_transaction_outputs(utxo_set_t *set, const transaction_t *transaction);
static int remove_transaction_inputs(utxo_set_t *set, const transaction_t *transaction);
static int outpoints_equal(const outpoint_t *first, const outpoint_t *second);

/*
 * Checks whether two outpoints identify the same transaction output.
 */
static int outpoints_equal(const outpoint_t *first, const outpoint_t *second) {
    if (first == NULL || second == NULL) {
        return 0;
    }

    return strcmp(first->prev_txid, second->prev_txid) == 0 &&
           first->output_index == second->output_index;
}

/*
 * Resizes the UTXO entry array.
 * @param set Pointer to the UTXO set.
 * @return 0 on success, -1 on failure.
 */
static int resize_utxo_set(utxo_set_t *set) {
    if (set == NULL) {
        return -1;
    }

    size_t new_capacity = set->capacity == 0
        ? INITIAL_UTXO_CAPACITY
        : set->capacity * 2;

    if (new_capacity < set->capacity ||
        new_capacity > SIZE_MAX / sizeof(utxo_entry_t)) {
        return -1;
    }

    utxo_entry_t *new_entries = realloc(
        set->entries,
        new_capacity * sizeof(utxo_entry_t)
    );

    if (new_entries == NULL) {
        return -1;
    }

    set->entries = new_entries;
    set->capacity = new_capacity;
    return 0;
}

/*
 * Finds an unspent transaction output by its outpoint.
 * @param set Pointer to the UTXO set.
 * @param outpoint Outpoint to find.
 * @return Pointer to the UTXO, or NULL if it does not exist.
 */
const utxo_entry_t *find_utxo(const utxo_set_t *set, const outpoint_t *outpoint) {
    if (set == NULL || outpoint == NULL) {
        return NULL;
    }

    for (size_t i = 0; i < set->count; i++) {
        if (outpoints_equal(&set->entries[i].outpoint, outpoint)) {
            return &set->entries[i];
        }
    }

    return NULL;
}

/*
 * Creates an independent copy of a UTXO set.
 * @param destination UTXO set receiving the copy.
 * @param source UTXO set being copied.
 * @return 0 on success, -1 on failure.
 */
int copy_utxo_set(utxo_set_t *destination, const utxo_set_t *source) {
    if (destination == NULL || source == NULL ||
        (source->count > 0 && source->entries == NULL) ||
        source->count > SIZE_MAX / sizeof(utxo_entry_t)) {
        return -1;
    }

    memset(destination, 0, sizeof(*destination));

    if (source->count == 0) {
        return 0;
    }

    destination->entries = malloc(source->count * sizeof(utxo_entry_t));
    if (destination->entries == NULL) {
        return -1;
    }

    memcpy(
        destination->entries,
        source->entries,
        source->count * sizeof(utxo_entry_t)
    );

    destination->count = source->count;
    destination->capacity = source->count;
    return 0;
}

/*
 * Removes every input spent by a normal transaction.
 * @param set Pointer to the UTXO set.
 * @param transaction Pointer to the spending transaction.
 * @return 0 on success, -1 if an input does not exist.
 */
static int remove_transaction_inputs(utxo_set_t *set, const transaction_t *transaction) {
    if (set == NULL || transaction == NULL ||
        (transaction->input_count > 0 && transaction->inputs == NULL)) {
        return -1;
    }

    if (is_coinbase_transaction(transaction)) {
        return 0;
    }

    for (size_t i = 0; i < transaction->input_count; i++) {
        const outpoint_t *outpoint = &transaction->inputs[i].outpoint;
        size_t position = set->count;

        for (size_t j = 0; j < set->count; j++) {
            if (outpoints_equal(&set->entries[j].outpoint, outpoint)) {
                position = j;
                break;
            }
        }

        if (position == set->count) {
            return -1;
        }

        set->entries[position] = set->entries[set->count - 1];
        set->count--;
    }

    return 0;
}

/*
 * Adds all outputs created by one transaction to the UTXO set.
 * @param set Pointer to the UTXO set.
 * @param transaction Pointer to the creating transaction.
 * @return 0 on success, -1 on failure.
 */
static int add_transaction_outputs(utxo_set_t *set, const transaction_t *transaction) {
    if (set == NULL || transaction == NULL ||
        (transaction->output_count > 0 && transaction->outputs == NULL)) {
        return -1;
    }

    for (size_t i = 0; i < transaction->output_count; i++) {
        if (transaction->outputs[i].amount == 0 || i > UINT32_MAX) {
            return -1;
        }

        outpoint_t outpoint = {0};
        strcpy(outpoint.prev_txid, transaction->txid);
        outpoint.output_index = (uint32_t)i;

        if (find_utxo(set, &outpoint) != NULL) {
            return -1;
        }

        if (set->count == set->capacity && resize_utxo_set(set) != 0) {
            return -1;
        }

        set->entries[set->count].outpoint = outpoint;
        set->entries[set->count].output = transaction->outputs[i];
        set->count++;
    }

    return 0;
}

/*
 * Applies an accepted block to a UTXO set.
 *
 * Inputs are removed before any new outputs are added. This prevents a
 * transaction from spending an output created earlier in the same block.
 *
 * @param set Pointer to a temporary UTXO set copy.
 * @param block Pointer to the accepted block.
 * @return 0 on success, -1 on failure.
 */
int apply_block_to_utxo_set(utxo_set_t *set, const block_t *block) {
    if (set == NULL || block == NULL ||
        (block->transaction_count > 0 && block->transactions == NULL)) {
        return -1;
    }

    //remove every confirmed spend first
    for (size_t i = 0; i < block->transaction_count; i++) {
        if (remove_transaction_inputs(set, &block->transactions[i]) != 0) {
            return -1;
        }
    }

    //then add all newly created outputs
    for (size_t i = 0; i < block->transaction_count; i++) {
        if (add_transaction_outputs(set, &block->transactions[i]) != 0) {
            return -1;
        }
    }

    return 0;
}

/*
 * Calculates the confirmed balance belonging to one address.
 * @param set Pointer to the UTXO set.
 * @param address Address whose balance should be calculated.
 * @param balance Pointer receiving the balance.
 * @return 0 on success, -1 on overflow or invalid input.
 */
int get_address_balance(const utxo_set_t *set, const char *address, uint64_t *balance) {
    if (set == NULL || address == NULL || balance == NULL) {
        return -1;
    }

    uint64_t total = 0;

    for (size_t i = 0; i < set->count; i++) {
        if (strcmp(set->entries[i].output.recipient_address, address) != 0) {
            continue;
        }

        if (UINT64_MAX - total < set->entries[i].output.amount) {
            return -1;
        }

        total += set->entries[i].output.amount;
    }

    *balance = total;
    return 0;
}

/*
 * Frees all memory owned by a UTXO set.
 * @param set Pointer to the UTXO set.
 */
void free_utxo_set(utxo_set_t *set) {
    if (set == NULL) {
        return;
    }

    free(set->entries);
    set->entries = NULL;
    set->count = 0;
    set->capacity = 0;
}
