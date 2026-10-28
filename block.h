#ifndef BLOCK_H
#define BLOCK_H

#include <stdint.h>
#include <stdio.h>
#include "prefix.h"

#define BLOCK_HASH_SIZE 65
#define TIMESTAMP_SIZE 21
#define CHAIN_CAPACITY 10




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
    char data[MAX_MESSAGES][MAX_MESSAGE_SIZE];
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

int create_pending_block(block_t *block, const block_t *prev_block, const char *message, const char *miner_address);
int add_pending_message(message_pool_t *pending, const char *message);
void serialize_block_data(const block_t *block, char *buffer, size_t buffer_size);
#endif // BLOCK_H

