#ifndef BLOCK_H
#define BLOCK_H

#include <stdint.h>
#include <stdio.h>
#include "prefix.h"
#include <time.h>
#include <inttypes.h>

#define BLOCK_HASH_SIZE 65
#define TIMESTAMP_SIZE 21
#define INITIAL_CHAIN_CAPACITY 10
#define NUM_HASH_BYTES 32




typedef struct message_node {
    char *message;
    struct message_node *next;
} message_node_t;

typedef struct {
    message_node_t *head_message;
    message_node_t *tail_message;
    size_t count;
} message_list_t;


typedef struct {
    uint64_t index;
    char prev_hash[65];
    char timestamp[21];
    char **data;
    size_t data_count;
    uint64_t nonce;
    char miner_address[ADDRESS_SIZE + 1];
    char hash[65];
} block_t;

typedef struct {
    block_t *blocks;
    size_t count;
    size_t capacity;
} blockchain_t;

//block create
int create_pending_block(block_t *block, const block_t *prev_block, const message_list_t *pending, const char *miner_address);
int create_genesis_block(block_t *block);
int mine_one_block(block_t *block, int difficulty);

// blockchain
int initialize_blockchain(blockchain_t *chain);
int append_block_to_chain(blockchain_t *chain, block_t *block);
void free_blockchain(blockchain_t *chain);
void free_block_data(block_t *block);


//message
int add_pending_message(message_list_t *pending, const char *message);
void free_message_list(message_list_t *list);


//network related block
int serialize_block_for_network(const block_t *block, char **output, size_t *output_length);
int deserialize_block_from_network(const char *input,size_t input_length,block_t *block);
int validate_next_block(const block_t *block,const block_t *previous, int difficulty);

#endif // BLOCK_H

