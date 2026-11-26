#ifndef MEMPOOL_H
#define MEMPOOL_H

#include "transaction.h"

typedef struct transaction_node {
    transaction_t transaction;
    struct transaction_node *next;
} transaction_node_t;

typedef struct {
    transaction_node_t *head;
    transaction_node_t *tail;
    size_t count;
} mempool_t;

int add_pending_transaction(mempool_t *mempool, const transaction_t *transaction);
int mempool_contains_txid(const mempool_t *mempool, const char *txid);
int mempool_spends_outpoint(const mempool_t *mempool, const outpoint_t *outpoint);
void remove_confirmed_transactions(mempool_t *mempool, const transaction_t *transactions, size_t transaction_count);
void free_mempool(mempool_t *mempool);

#endif // MEMPOOL_H
