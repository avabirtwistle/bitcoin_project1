#include "network.h"

/**
 * Load the network configuration from a JSON file.
 * @param config_path The path to the configuration file.
 * @param config The configuration structure to populate.
 * @return 0 on success, -1 on failure.
 */
static int load_config(const char *config_path, node_config_t *config){
    memset(config, 0, sizeof(*config)); // Initialize the config structure to zero
    FILE *file = fopen(config_path, "r");
    if (file == NULL) {
        perror("Failed to open config");
        return -1;
    }

    char buffer[JSON_BUFFER_SIZE];

    size_t bytes_read = fread(buffer, 1, sizeof(buffer) - 1, file);

    fclose(file);
    buffer[bytes_read] = '\0';

    cJSON *root = cJSON_Parse(buffer);
    if (root == NULL) {
        fprintf(stderr, "Failed to parse config JSON.\n");
        return -1;
    }

    cJSON *node_id = cJSON_GetObjectItemCaseSensitive(root, "node_id");
    cJSON *port = cJSON_GetObjectItemCaseSensitive(root, "port");
    cJSON *peers = cJSON_GetObjectItemCaseSensitive(root, "peers");

    snprintf( // copy the node id string into the config struct
        config->node_id,
        sizeof(config->node_id),
        "%s",
        node_id->valuestring
    );

    config->port = port->valueint;

    int peer_count = cJSON_GetArraySize(peers);
    if (peer_count > MAX_PEERS) {
        cJSON_Delete(root);
        fprintf(stderr, "Too many configured peers.\n");
        return -1;
    }

    config->peer_count = (size_t)peer_count;

    // Iterate through the peers array and populate the config struct
    for (int i = 0; i < peer_count; i++) {
        cJSON *peer = cJSON_GetArrayItem(peers, i);

        // Get the host and port of each peer from the URL
        sscanf(peer->valuestring, "http://%255[^:]:%d", config->peers[i].host, &config->peers[i].port);
    }

    cJSON_Delete(root);
    return 0;
}

/** Sends all data to a socket.
 * @param socket_fd The file descriptor of the socket.
 * @param data Pointer to the data to send.
 * @param length The length of the data to send.
 * @return 0 on success, -1 on failure.
 */
static int send_all(int socket_fd, const void *data, size_t length) {
    const unsigned char *cursor = data; // Pointer to the current position in the data
    size_t total_sent = 0;

    // loop until all data sent
    while (total_sent < length) {
        ssize_t sent = send(socket_fd, cursor + total_sent, length - total_sent, 0);

        //if send was interrupted by a signal, retry sending
        if (sent < 0 && errno == EINTR) {
            continue;
        }

        //if send failed for any other reason, return error
        if (sent <= 0) {
            return -1;
        }

        //update the total sent bytes
        total_sent += (size_t)sent;
    }

    return 0;
}

/** Receives all data from a socket.
 * @param socket_fd The file descriptor of the socket.
 * @param data Pointer to the buffer to store the received data.
 * @param length The length of the data to receive.
 * @return 0 on success, -1 on failure.
 */
static int receive_all(int socket_fd, void *data, size_t length) {
    unsigned char *cursor = data;// Pointer to the current position in the data
    size_t total_received = 0;

    // loop until all data received
    while (total_received < length) {
        ssize_t received = recv(socket_fd, cursor + total_received, length - total_received, 0);

        //if recv was interrupted by a signal, retry receiving
        if (received < 0 && errno == EINTR) {
            continue;
        }

        //if recv failed for any other reason, return error
        if (received <= 0) {
            return -1;
        }

        total_received += (size_t)received;
    }

    return 0;
}

/** Receives a block from a peer.
 * @param network Pointer to the network.
 * @param peer_fd The file descriptor of the peer.
 */
static void receive_block(network_t *network, int peer_fd) {
    uint32_t network_length;
    int accepted = 0;

    // Receive the length of the serialized block from the peer
    if (receive_all(peer_fd, &network_length, sizeof(network_length)) != 0) {
        fprintf(stderr, "Warning: failed to receive block length.\n");
        return;
    }

    size_t serialized_length = ntohl(network_length); // Convert from network byte order to host byte order

    // Allocate memory for the serialized block data
    char *serialized = malloc(serialized_length + 1);
    if (serialized == NULL) {
        return;
    }

    // Receive the serialized block data from the peer
    if (receive_all(peer_fd, serialized, serialized_length) != 0) {
        fprintf(stderr, "Warning: failed to receive complete block.\n");
        free(serialized);
        return;
    }

    // Null-terminate the serialized block data
    serialized[serialized_length] = '\0';

    // Deserialize the received block data into a block_t structure
    block_t received_block = {0};

    if (deserialize_block_from_network(serialized, serialized_length, &received_block) != 0) {
        fprintf(stderr, "Rejected block: deserialization failed.\n");
        free(serialized);
        return;
    }
    free(serialized);

    pthread_mutex_lock(network->chain_lock);

    if (network->chain->count > 0) {
        const block_t *previous = &network->chain->blocks[network->chain->count - 1];

        if (validate_next_block(&received_block, previous, network->difficulty) == 1) {
            if (append_block_to_chain(network->chain, &received_block) == 0) {
                accepted = 1;
            }
        }
    }

    pthread_mutex_unlock(network->chain_lock);

    if (accepted) {
        printf("\n[%s] Accepted received block.\n> ", network->node_id);
        fflush(stdout);
    } else {
        printf("\n[%s] Rejected received block.\n> ", network->node_id);
        fflush(stdout);
        free_block_data(&received_block);
    }
}

/** Listens for incoming blocks from peers.
 * @param argument The network structure.
 * @return NULL.
 */
static void *listen_for_blocks(void *argument){
    network_t *network = argument;

    while (network->running) {
        int peer_fd = accept(network->listen_fd, NULL, NULL);

        if (peer_fd < 0) {
            if (network->running) {
                perror("Warning: failed to accept peer");
            }
            continue;
        }

        receive_block(network, peer_fd);
        close(peer_fd);
    }

    return NULL;
}

/** Starts the network.
 * @param network Pointer to the network.
 * @param config_path The path to the configuration file.
 * @param chain Pointer to the blockchain.
 * @param chain_lock Pointer to the blockchain lock.
 * @param difficulty The difficulty of the network.
 * @return 0 on success, -1 on failure.
 */
int network_start(network_t *network, const char *config_path, blockchain_t *chain, pthread_mutex_t *chain_lock, int difficulty){
    node_config_t config;

    if (load_config(config_path, &config) != 0) {
        return -1;
    }

    memset(network, 0, sizeof(*network));
    network->listen_fd = -1;
    network->port = config.port;
    network->peer_count = config.peer_count;
    network->chain = chain;
    network->chain_lock = chain_lock;
    network->difficulty = difficulty;

    snprintf(network->node_id, sizeof(network->node_id), "%s", config.node_id);

    for (size_t i = 0; i < config.peer_count; i++) {
        network->peers[i] = config.peers[i];
    }

    //prevent the program from closing if a peer disconnects while sending
    signal(SIGPIPE, SIG_IGN);

    network->listen_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (network->listen_fd < 0) {
        perror("Failed to create network socket");
        return -1;
    }

    int reuse_address = 1;
    setsockopt(network->listen_fd, SOL_SOCKET, SO_REUSEADDR, &reuse_address, sizeof(reuse_address));

    struct sockaddr_in address = {0};
    address.sin_family = AF_INET;
    address.sin_addr.s_addr = htonl(INADDR_ANY);
    address.sin_port = htons((uint16_t)network->port);

    if (bind(network->listen_fd, (struct sockaddr *)&address, sizeof(address)) != 0) {
        perror("Failed to bind network socket");
        close(network->listen_fd);
        return -1;
    }

    if (listen(network->listen_fd, MAX_PEERS) != 0) {
        perror("Failed to listen for peers");
        close(network->listen_fd);
        return -1;
    }

    network->running = 1;

    if (pthread_create(&network->listener_thread, NULL, listen_for_blocks, network) != 0) {
        fprintf(stderr, "Failed to create network listener thread.\n");
        close(network->listen_fd);
        network->running = 0;
        return -1;
    }

    printf("%s listening on port %d\n", network->node_id, network->port);
    return 0;
}

/** Broadcasts a block to all peers in the network.
 * @param network Pointer to the network.
 * @param block Pointer to the block to broadcast.
 * @return 0 on success, -1 on failure.
 */
int network_broadcast_block(network_t *network, const block_t *block){
    char *serialized = NULL;
    size_t serialized_length = 0;

    if (serialize_block_for_network(block, &serialized, &serialized_length) != 0) {
        return -1;
    }

    uint32_t network_length = htonl((uint32_t)serialized_length);

    //connect to each configured peer and send the mined block
    for (size_t i = 0; i < network->peer_count; i++) {
        int peer_fd = socket(AF_INET, SOCK_STREAM, 0);
        if (peer_fd < 0) {
            fprintf(stderr, "Warning: failed to create socket for peer.\n");
            continue;
        }

        struct sockaddr_in peer_address = {0};
        peer_address.sin_family = AF_INET;
        peer_address.sin_port = htons((uint16_t)network->peers[i].port);
        inet_pton(AF_INET, network->peers[i].host, &peer_address.sin_addr);

        if (connect(peer_fd, (struct sockaddr *)&peer_address, sizeof(peer_address)) != 0) {
            fprintf(stderr, "Warning: peer %s:%d is offline.\n", network->peers[i].host, network->peers[i].port);
            close(peer_fd);
            continue;
        }

        if (send_all(peer_fd, &network_length, sizeof(network_length)) != 0 ||
            send_all(peer_fd, serialized, serialized_length) != 0) {
            fprintf(stderr, "Warning: failed to send block to %s:%d.\n", network->peers[i].host, network->peers[i].port);
        }

        close(peer_fd);
    }

    free(serialized);
    return 0;
}

/** Stops the network.
 * @param network Pointer to the network.
 */
void network_stop(network_t *network){
    if (network == NULL || !network->running) {
        return;
    }

    network->running = 0;
    shutdown(network->listen_fd, SHUT_RDWR);
    close(network->listen_fd);
    pthread_join(network->listener_thread, NULL);
}
