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

#define MAX_PEERS 16
#define HOST_SIZE 256
#define NODE_ID_SIZE 64

typedef struct {
    char host[HOST_SIZE];
    int port;
} peer_config_t;

typedef struct {
    char node_id[NODE_ID_SIZE];
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
    pthread_mutex_t *chain_lock;
    int difficulty;
} network_t;

int network_start(network_t *network, const char *config_path, blockchain_t *chain, pthread_mutex_t *chain_lock, int difficulty);
int network_broadcast_block(network_t *network, const block_t *block);
void network_stop(network_t *network);

#endif
