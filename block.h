#ifndef BLOCK_H
#define BLOCK_H

#include <inttypes.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <time.h>

#include "mempool.h"
#include "prefix.h"
#include "transaction.h"

#define BLOCK_HASH_SIZE 65
#define TIMESTAMP_SIZE 21
#define INITIAL_CHAIN_CAPACITY 10
#define NUM_HASH_BYTES 32
#define BLOCK_SUBSIDY 50

typedef struct {
    uint64_t index;
    char prev_hash[BLOCK_HASH_SIZE];
    char merkle_root[BLOCK_HASH_SIZE];
    char timestamp[TIMESTAMP_SIZE];
    transaction_t *transactions;
    size_t transaction_count;
    uint64_t nonce;
    char miner_address[ADDRESS_SIZE + 1];
    char hash[BLOCK_HASH_SIZE];
} block_t;

typedef struct {
    block_t *blocks;
    size_t count;
    size_t capacity;
} blockchain_t;

//block create
int create_pending_block(block_t *block, const block_t *prev_block, const mempool_t *pending, const char *miner_address, uint64_t total_fees);
int create_genesis_block(block_t *block);
int mine_one_block(block_t *block, int difficulty);
int compute_merkle_root(const transaction_t *transactions, size_t transaction_count, char merkle_root[BLOCK_HASH_SIZE]);

// blockchain
int initialize_blockchain(blockchain_t *chain);
int append_block_to_chain(blockchain_t *chain, block_t *block);
void free_blockchain(blockchain_t *chain);
void free_block_data(block_t *block);

//network related block
int serialize_block_for_network(const block_t *block, char **output, size_t *output_length);
int deserialize_block_from_network(const char *input, size_t input_length, block_t *block);
int validate_next_block(const block_t *block, const block_t *previous, int difficulty);

#endif // BLOCK_H
