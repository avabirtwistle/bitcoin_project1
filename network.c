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

    cJSON *host = cJSON_GetObjectItemCaseSensitive(root, "host");
    cJSON *port =cJSON_GetObjectItemCaseSensitive(root, "port");
    cJSON *peers = cJSON_GetObjectItemCaseSensitive(root, "peers");

    snprintf( // copy the host string into the config struct
        config->host,
        sizeof(config->host),
        "%s",
        host->valuestring
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

        // Get the host and port of each peer
        cJSON *peer_host = cJSON_GetObjectItemCaseSensitive(peer, "host");
        cJSON *peer_port = cJSON_GetObjectItemCaseSensitive(peer, "port");

        snprintf( // copy the peer host string into the config struct
            config->peers[i].host,
            sizeof(config->peers[i].host),
            "%s",
            peer_host->valuestring
        );
        // copy the peer port into the config struct
        config->peers[i].port = peer_port->valueint;
    }

    cJSON_Delete(root);
    return 0;
}