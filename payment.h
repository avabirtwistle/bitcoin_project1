#ifndef PAYMENT_H
#define PAYMENT_H

#include "validation.h"

#define PAYMENT_ERROR -1
#define PAYMENT_INSUFFICIENT_FUNDS -2

int create_payment_transaction(
    transaction_t *transaction,
    const utxo_set_t *utxos,
    const mempool_t *mempool,
    const wallet_t *wallet,
    const char *recipient_address,
    uint64_t amount,
    uint64_t fee
);

#endif // PAYMENT_H
