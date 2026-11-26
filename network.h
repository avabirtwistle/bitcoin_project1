#ifndef NETWORK_H
#define NETWORK_H

#include <pthread.h>
#include "block.h"
#include <arpa/inet.h>
#include <stdint.h>
#include <sys/socket.h>
#include <cjson/cJSON.h>
#include <stdio.h>
#include <string.h>
#include <errno.h>
#include <signal.h>
#include <stdlib.h>
#include <unistd.h>
#include "utxo.h"
#include "validation.h"

#define MAX_PEERS 16
#define HOST_SIZE 256
#define NODE_ID_SIZE 64
#define WALLET_PATH_SIZE 256
#define NETWORK_MESSAGE_BLOCK 1
#define NETWORK_MESSAGE_TRANSACTION 2

typedef struct {
    char host[HOST_SIZE];
    int port;
} peer_config_t;

typedef struct {
    char node_id[NODE_ID_SIZE];
    char wallet_file[WALLET_PATH_SIZE];
    int port;
    peer_config_t peers[MAX_PEERS];
    size_t peer_count;
} node_config_t;

typedef struct {
    int listen_fd;
    int running;
    pthread_t listener_thread;

    char node_id[NODE_ID_SIZE];
    int port;

    peer_config_t peers[MAX_PEERS];
    size_t peer_count;

    blockchain_t *chain;
    utxo_set_t *utxos;
    mempool_t *mempool;
    pthread_mutex_t *chain_lock;
    int difficulty;
} network_t;

int load_node_config(const char *config_path, node_config_t *config);
int network_start(network_t *network, const char *config_path, blockchain_t *chain, utxo_set_t *utxos, mempool_t *mempool, pthread_mutex_t *chain_lock, int difficulty);
int network_broadcast_block(network_t *network, const block_t *block);
int network_broadcast_transaction(network_t *network, const transaction_t *transaction);
void network_stop(network_t *network);

#endif
