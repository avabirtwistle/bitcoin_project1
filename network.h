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

#define MAX_PEERS 16
#define MAX_NETWORK_MESSAGE (1024 * 1024)

typedef struct {
    int listen_fd;
    int peer_fds[MAX_PEERS];
    size_t peer_count;
    int running;

    pthread_t listener_thread;
    pthread_mutex_t peers_lock;

    blockchain_t *chain;
    message_list_t *pending;
    pthread_mutex_t *state_lock;
    int difficulty;
} network_t;

int network_start(
    network_t *network,
    const char *config_path,
    blockchain_t *chain,
    message_list_t *pending,
    pthread_mutex_t *state_lock,
    int difficulty
);

typedef struct {
    char host[256];
    int port;
} peer_config_t;

typedef struct {
    char host[256];
    int port;
    peer_config_t peers[MAX_PEERS];
    size_t peer_count;
} node_config_t;

int network_broadcast_message(network_t *network, const char *message);
int network_broadcast_block(network_t *network, const block_t *block);
void network_stop(network_t *network);

#endif