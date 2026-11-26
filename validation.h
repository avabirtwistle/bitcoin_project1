#ifndef VALIDATION_H
#define VALIDATION_H

#include "utxo.h"

typedef enum {
    VALIDATION_OK = 0,
    VALIDATION_INVALID_STRUCTURE,
    VALIDATION_INVALID_TXID,
    VALIDATION_INVALID_OUTPUT,
    VALIDATION_COINBASE_NOT_ALLOWED,
    VALIDATION_DUPLICATE_TRANSACTION,
    VALIDATION_INPUT_NOT_FOUND,
    VALIDATION_DUPLICATE_INPUT,
    VALIDATION_MEMPOOL_DOUBLE_SPEND,
    VALIDATION_WRONG_PUBLIC_KEY,
    VALIDATION_INVALID_SIGNATURE,
    VALIDATION_VALUE_OVERFLOW,
    VALIDATION_INSUFFICIENT_INPUT,
    VALIDATION_INVALID_COINBASE,
    VALIDATION_EXCESSIVE_COINBASE
} validation_result_t;

const char *validation_result_message(validation_result_t result);
int validate_transaction_for_mempool(
    const transaction_t *transaction,
    const utxo_set_t *utxos,
    const mempool_t *mempool,
    uint64_t *fee,
    validation_result_t *result
);
int calculate_mempool_fees(
    const mempool_t *mempool,
    const utxo_set_t *utxos,
    uint64_t *total_fees,
    validation_result_t *result
);
int validate_block_transactions(
    const block_t *block,
    const utxo_set_t *utxos,
    uint64_t *total_fees,
    validation_result_t *result
);

#endif // VALIDATION_H
