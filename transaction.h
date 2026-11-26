#ifndef TRANSACTION_H
#define TRANSACTION_H

#include <stddef.h>
#include <stdint.h>

#include "prefix.h"

#define TXID_SIZE 65
#define MAX_SIGNATURE_SIZE 72

typedef struct {
    char prev_txid[TXID_SIZE];
    uint32_t output_index;
} outpoint_t;

typedef struct {
    outpoint_t outpoint;
    uint8_t signature[MAX_SIGNATURE_SIZE];
    size_t signature_length;
    uint8_t public_key[PUBLIC_KEY_SIZE];

    // Only used by a coinbase input and included in its transaction ID.
    uint64_t coinbase_height;
} tx_input_t;

typedef struct {
    uint64_t amount;
    char recipient_address[ADDRESS_SIZE + 1];
} tx_output_t;

typedef struct {
    char txid[TXID_SIZE];
    tx_input_t *inputs;
    size_t input_count;
    tx_output_t *outputs;
    size_t output_count;
} transaction_t;

int calculate_transaction_id(transaction_t *transaction);
int transaction_id_matches(const transaction_t *transaction);
int sign_transaction(transaction_t *transaction, const wallet_t *wallet);
int verify_transaction_input_signature(const transaction_t *transaction, size_t input_index);
int copy_transaction(transaction_t *destination, const transaction_t *source);
void free_transaction(transaction_t *transaction);
int is_coinbase_transaction(const transaction_t *transaction);
int create_coinbase_transaction(transaction_t *transaction, uint64_t block_height, const char *miner_address, uint64_t reward);
int serialize_transaction_for_network(const transaction_t *transaction, char **output, size_t *output_length);
int deserialize_transaction_from_network(const char *input, size_t input_length, transaction_t *transaction);

#endif // TRANSACTION_H
