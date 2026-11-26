#include "mempool.h"

#include <stdlib.h>
#include <string.h>

/*
 * Adds a copy of a transaction to the local mempool.
 * Validation must be completed before calling this function.
 * @param mempool Pointer to the mempool.
 * @param transaction Pointer to the transaction to add.
 * @return 0 on success, -1 on failure.
 */
int add_pending_transaction(mempool_t *mempool, const transaction_t *transaction) {
    if (mempool == NULL || transaction == NULL) {
        return -1;
    }

    transaction_node_t *new_transaction = calloc(1, sizeof(*new_transaction));
    if (new_transaction == NULL) {
        return -1;
    }

    if (copy_transaction(&new_transaction->transaction, transaction) != 0) {
        free(new_transaction);
        return -1;
    }

    if (mempool->head == NULL) {
        mempool->head = new_transaction;
        mempool->tail = new_transaction;
    } else {
        mempool->tail->next = new_transaction;
        mempool->tail = new_transaction;
    }

    mempool->count++;
    return 0;
}

/*
 * Checks if a transaction ID is already in the mempool.
 * @param mempool Pointer to the mempool.
 * @param txid The transaction ID to find.
 * @return 1 if found, 0 otherwise.
 */
int mempool_contains_txid(const mempool_t *mempool, const char *txid) {
    if (mempool == NULL || txid == NULL) {
        return 0;
    }

    transaction_node_t *current = mempool->head;
    while (current != NULL) {
        if (strcmp(current->transaction.txid, txid) == 0) {
            return 1;
        }
        current = current->next;
    }

    return 0;
}

/*
 * Checks whether a mempool transaction already spends an outpoint.
 * @param mempool Pointer to the mempool.
 * @param outpoint The outpoint to find.
 * @return 1 if spent by the mempool, 0 otherwise.
 */
int mempool_spends_outpoint(const mempool_t *mempool, const outpoint_t *outpoint) {
    if (mempool == NULL || outpoint == NULL) {
        return 0;
    }

    transaction_node_t *current = mempool->head;
    while (current != NULL) {
        const transaction_t *transaction = &current->transaction;

        for (size_t i = 0; i < transaction->input_count; i++) {
            const outpoint_t *current_outpoint = &transaction->inputs[i].outpoint;

            if (strcmp(current_outpoint->prev_txid, outpoint->prev_txid) == 0 &&
                current_outpoint->output_index == outpoint->output_index) {
                return 1;
            }
        }

        current = current->next;
    }

    return 0;
}

/*
 * Checks whether a transaction ID appears in an array.
 * @param transactions Pointer to the transaction array.
 * @param transaction_count Number of transactions in the array.
 * @param txid The transaction ID to find.
 * @return 1 if found, 0 otherwise.
 */
static int transactions_contain_txid(const transaction_t *transactions, size_t transaction_count, const char *txid) {
    if (transactions == NULL || txid == NULL) {
        return 0;
    }

    for (size_t i = 0; i < transaction_count; i++) {
        if (strcmp(transactions[i].txid, txid) == 0) {
            return 1;
        }
    }

    return 0;
}

/*
 * Checks whether a confirmed block transaction spends an outpoint.
 */
static int transactions_spend_outpoint(
    const transaction_t *transactions,
    size_t transaction_count,
    const outpoint_t *outpoint
) {
    for (size_t i = 0; i < transaction_count; i++) {
        if (is_coinbase_transaction(&transactions[i])) {
            continue;
        }

        for (size_t j = 0; j < transactions[i].input_count; j++) {
            const outpoint_t *confirmed = &transactions[i].inputs[j].outpoint;

            if (strcmp(confirmed->prev_txid, outpoint->prev_txid) == 0 &&
                confirmed->output_index == outpoint->output_index) {
                return 1;
            }
        }
    }

    return 0;
}

/*
 * Checks whether a pending transaction conflicts with a confirmed spend.
 */
static int transaction_conflicts_with_block(
    const transaction_t *transaction,
    const transaction_t *confirmed_transactions,
    size_t confirmed_count
) {
    for (size_t i = 0; i < transaction->input_count; i++) {
        if (transactions_spend_outpoint(
            confirmed_transactions,
            confirmed_count,
            &transaction->inputs[i].outpoint
        )) {
            return 1;
        }
    }

    return 0;
}

/*
 * Removes confirmed transactions and transactions conflicting with the block.
 * @param mempool Pointer to the mempool.
 * @param transactions Pointer to the confirmed transaction array.
 * @param transaction_count Number of confirmed transactions.
 */
void remove_confirmed_transactions(mempool_t *mempool, const transaction_t *transactions, size_t transaction_count) {
    if (mempool == NULL || (transaction_count > 0 && transactions == NULL)) {
        return;
    }

    transaction_node_t *current = mempool->head;
    transaction_node_t *previous = NULL;

    while (current != NULL) {
        transaction_node_t *next = current->next;

        if (transactions_contain_txid(transactions, transaction_count, current->transaction.txid) ||
            transaction_conflicts_with_block(
                &current->transaction,
                transactions,
                transaction_count
            )) {
            if (previous == NULL) {
                mempool->head = next;
            } else {
                previous->next = next;
            }

            if (mempool->tail == current) {
                mempool->tail = previous;
            }

            free_transaction(&current->transaction);
            free(current);
            mempool->count--;
        } else {
            previous = current;
        }

        current = next;
    }
}

/*
 * Frees all transactions and nodes in a mempool.
 * @param mempool Pointer to the mempool.
 */
void free_mempool(mempool_t *mempool) {
    if (mempool == NULL) {
        return;
    }

    transaction_node_t *current = mempool->head;

    while (current != NULL) {
        transaction_node_t *next = current->next;
        free_transaction(&current->transaction);
        free(current);
        current = next;
    }

    mempool->head = NULL;
    mempool->tail = NULL;
    mempool->count = 0;
}
