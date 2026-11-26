#include "block.h"

#include <errno.h>
#include <string.h>

//helpers
static int get_time(char timestamp[TIMESTAMP_SIZE]);
static int get_block_hash(const block_t *block, char hash_output[BLOCK_HASH_SIZE]);
static int resize_blockchain(blockchain_t *chain);
static int serialize_block_data(const block_t *block, char **serialized_block, size_t *serialized_block_len);
static int read_network_field(char **cursor, const char *field_name, char *value, size_t value_size);
static int read_uint64_field(char **cursor, const char *field_name, uint64_t *value);
static int read_size_field(char **cursor, const char *field_name, size_t *value);
static int check_hash_difficulty(const char *hash, int difficulty);

/*
 * Gets the current time and formats it as a string.
 * @param timestamp Pointer to the buffer to store the formatted timestamp.
 * @return 0 on success, -1 on failure.
 */
static int get_time(char timestamp[TIMESTAMP_SIZE]) {
    time_t now = time(NULL);

    // current time in UTC
    struct tm *tm_info = gmtime(&now);

    // Check if gmtime returned NULL
    if (tm_info == NULL) {
        return -1; // Error getting current time
    }

    // format into string YYYY-MM-DDTHH:MM:SSZ
    if (strftime(timestamp, TIMESTAMP_SIZE, "%Y-%m-%dT%H:%M:%SZ", tm_info) == 0) {
        return -1; // Error formatting timestamp
    }

    return 0; // Success
}

/****************Block Operations****************/

/**
 * Initializes a new blockchain with a genesis block.
 * @param chain Pointer to the blockchain structure to initialize.
 * @return 0 on success, -1 on failure.
 */
int initialize_blockchain(blockchain_t *chain) {
    if (chain == NULL) {
        return -1;
    }

    chain->count = 0;
    chain->capacity = INITIAL_CHAIN_CAPACITY;

    //allocate memory for block array based on initial capacity
    chain->blocks = malloc(chain->capacity * sizeof(block_t));
    if (chain->blocks == NULL) {
        return -1;
    }

    // create the initial genesis block
    if (create_genesis_block(&chain->blocks[0]) != 0) {
        free_blockchain(chain);
        return -1;
    }

    chain->count = 1;
    return 0;
}

/**
 * Creates a new genesis block.
 * @param block Pointer to the block to create.
 * @return 0 on success, -1 on failure.
 */
int create_genesis_block(block_t *block) {
    if (block == NULL) {
        return -1;
    }

    // Set every field and pointer in the block to zero initially.
    memset(block, 0, sizeof(*block));

    block->index = 0;
    block->nonce = 0;

    // set the prev hash to all zero characters
    memset(block->prev_hash, '0', BLOCK_HASH_SIZE - 1);
    block->prev_hash[BLOCK_HASH_SIZE - 1] = '\0';

    //use assignment time stamp
    strcpy(block->timestamp, "2026-01-01T00:00:00Z");
    strcpy(block->miner_address, "GENESIS");

    //Assignment 2 genesis contains no transactions and creates no coins
    block->transactions = NULL;
    block->transaction_count = 0;

    //calculate block hash
    if (get_block_hash(block, block->hash) != 0) {
        return -1;
    }

    return 0;
}

/**
 * Resizes the blockchain.
 * @param chain Pointer to the blockchain to resize.
 * @return 0 on success, -1 on failure.
 */
static int resize_blockchain(blockchain_t *chain) {
    size_t new_capacity = chain->capacity * 2;

    //reallocate memory for the block array with new capacity
    block_t *new_blocks = realloc(chain->blocks, new_capacity * sizeof(block_t));
    if (new_blocks == NULL) {
        return -1; // Memory allocation failed
    }

    chain->blocks = new_blocks;
    chain->capacity = new_capacity;
    return 0;
}

/**
 * Creates a new block from the pending transactions.
 * @param block Pointer to the block to create.
 * @param prev_block Pointer to the previous block.
 * @param pending Pointer to the local mempool.
 * @param miner_address Address receiving the coinbase reward.
 * @param total_fees Total fees from the pending transactions.
 * @return 0 on success, -1 on failure.
 */
int create_pending_block(block_t *block, const block_t *prev_block, const mempool_t *pending, const char *miner_address, uint64_t total_fees) {
    if (block == NULL || prev_block == NULL || pending == NULL || miner_address == NULL ||
        miner_address[0] == '\0' || strlen(miner_address) > ADDRESS_SIZE ||
        total_fees > UINT64_MAX - BLOCK_SUBSIDY || pending->count == SIZE_MAX) {
        return -1;
    }

    //initialize the block
    memset(block, 0, sizeof(*block));
    block->index = prev_block->index + 1;
    block->nonce = 0;
    strcpy(block->prev_hash, prev_block->hash);
    strcpy(block->miner_address, miner_address);

    if (get_time(block->timestamp) != 0) {
        return -1; // Error getting timestamp
    }

    //allocate one transaction for coinbase plus all pending transactions
    size_t expected_transaction_count = pending->count + 1;
    block->transactions = calloc(expected_transaction_count, sizeof(transaction_t));
    if (block->transactions == NULL) {
        return -1;
    }

    uint64_t reward = BLOCK_SUBSIDY + total_fees;
    if (create_coinbase_transaction(&block->transactions[0], block->index, miner_address, reward) != 0) {
        free_block_data(block);
        return -1;
    }
    block->transaction_count = 1;

    transaction_node_t *current = pending->head;
    while (current != NULL && block->transaction_count < expected_transaction_count) {
        if (copy_transaction(&block->transactions[block->transaction_count], &current->transaction) != 0) {
            free_block_data(block);
            return -1;
        }

        block->transaction_count++;
        current = current->next;
    }

    //the linked list count must agree with the number of nodes copied
    if (current != NULL || block->transaction_count != expected_transaction_count) {
        free_block_data(block);
        return -1;
    }

    return 0;
}

/*
 * Serializes the data of a block into a string.
 * @param block Pointer to the block to serialize.
 * @param serialized_block Pointer to a string that will hold the data.
 * @param serialized_block_len Pointer to the serialized data length.
 * @return 0 on success, -1 on failure.
 */
static int serialize_block_data(const block_t *block, char **serialized_block, size_t *serialized_block_len) {
    if (block == NULL || serialized_block == NULL || serialized_block_len == NULL ||
        (block->transaction_count > 0 && block->transactions == NULL)) {
        return -1;
    }

    *serialized_block = NULL;
    *serialized_block_len = 0;

    FILE *stream = open_memstream(serialized_block, serialized_block_len);
    if (stream == NULL) {
        return -1;
    }

    if (fprintf(stream,
        "index=%" PRIu64 "\n"
        "timestamp=%s\n"
        "prev_hash=%s\n",
        block->index,
        block->timestamp,
        block->prev_hash) < 0) {
        goto error;
    }

    //write transaction IDs in their exact block order
    for (size_t i = 0; i < block->transaction_count; i++) {
        if (fprintf(stream, "txid=%s\n", block->transactions[i].txid) < 0) {
            goto error;
        }
    }

    if (fprintf(stream,
        "nonce=%" PRIu64 "\n"
        "miner_address=%s",
        block->nonce,
        block->miner_address) < 0) {
        goto error;
    }

    if (fclose(stream) != 0) {
        free(*serialized_block);
        *serialized_block = NULL;
        *serialized_block_len = 0;
        return -1;
    }

    return 0;

error:
    fclose(stream);
    free(*serialized_block);
    *serialized_block = NULL;
    *serialized_block_len = 0;
    return -1;
}

/**
 * Calculates the hash of a block.
 * @param block Pointer to the block to calculate the hash of.
 * @param hash_output Buffer to store the resulting hash.
 * @return 0 on success, -1 on failure.
 */
static int get_block_hash(const block_t *block, char hash_output[BLOCK_HASH_SIZE]) {
    char *serialized_block = NULL;
    size_t serialized_block_len = 0;
    uint8_t hash_data[NUM_HASH_BYTES];
    size_t bytes_written = 0;

    if (serialize_block_data(block, &serialized_block, &serialized_block_len) != 0) {
        return -1;
    }

    if (EVP_Q_digest(NULL, "SHA256", NULL, serialized_block, serialized_block_len, hash_data, &bytes_written) != 1 ||
        bytes_written != NUM_HASH_BYTES) {
        free(serialized_block);
        return -1;
    }

    if (OPENSSL_buf2hexstr_ex(hash_output, BLOCK_HASH_SIZE, NULL, hash_data, NUM_HASH_BYTES, '\0') != 1) {
        free(serialized_block);
        return -1;
    }

    free(serialized_block);
    return 0;
}

/**
 * Appends a block to a blockchain.
 * Ownership of the block's transactions is moved to the chain.
 * @param chain Pointer to the blockchain to append to.
 * @param block Pointer to the block to append.
 * @return 0 on success, -1 on failure.
 */
int append_block_to_chain(blockchain_t *chain, block_t *block) {
    if (chain == NULL || block == NULL) {
        return -1;
    }

    if (chain->count == chain->capacity) {
        if (resize_blockchain(chain) != 0) {
            return -1;
        }
    }

    chain->blocks[chain->count] = *block;
    chain->count++;

    //reset the block to avoid dangling pointers and double frees
    memset(block, 0, sizeof(*block));
    return 0;
}

/********Mining*********/

/**
 * Checks if a hash meets the required difficulty.
 * @param hash The hash to check.
 * @param difficulty The required difficulty.
 * @return 1 if the hash meets the difficulty, 0 otherwise.
 */
static int check_hash_difficulty(const char *hash, int difficulty) {
    if (hash == NULL || difficulty < 0 || difficulty > BLOCK_HASH_SIZE - 1) {
        return 0;
    }

    for (int i = 0; i < difficulty; i++) {
        if (hash[i] != '0') {
            return 0;
        }
    }

    return 1;
}

/**
 * Mines a single block.
 * @param block Pointer to the block to mine.
 * @param difficulty The difficulty to mine at.
 * @return 0 on success, -1 on failure.
 */
int mine_one_block(block_t *block, int difficulty) {
    if (block == NULL || difficulty < 0) {
        return -1;
    }

    block->nonce = 0;

    while (1) {
        if (get_block_hash(block, block->hash) != 0) {
            return -1;
        }

        if (check_hash_difficulty(block->hash, difficulty)) {
            return 0;
        }

        if (block->nonce == UINT64_MAX) {
            return -1;
        }

        block->nonce++;
    }
}

/****************Network Serialization****************/

/**
 * Serializes a block for network transmission.
 * Complete transactions are length-prefixed and written in block order.
 * @param block Pointer to the block to serialize.
 * @param output Pointer to the output buffer.
 * @param output_length Pointer to the output buffer length.
 * @return 0 on success, -1 on failure.
 */
int serialize_block_for_network(const block_t *block, char **output, size_t *output_length) {
    if (block == NULL || output == NULL || output_length == NULL ||
        (block->transaction_count > 0 && block->transactions == NULL)) {
        return -1;
    }

    *output = NULL;
    *output_length = 0;

    FILE *stream = open_memstream(output, output_length);
    if (stream == NULL) {
        return -1;
    }

    if (fprintf(stream,
        "index=%" PRIu64 "\n"
        "timestamp=%s\n"
        "prev_hash=%s\n"
        "transaction_count=%zu\n",
        block->index,
        block->timestamp,
        block->prev_hash,
        block->transaction_count) < 0) {
        goto error;
    }

    for (size_t i = 0; i < block->transaction_count; i++) {
        char *serialized_transaction = NULL;
        size_t serialized_transaction_len = 0;

        if (serialize_transaction_for_network(&block->transactions[i],
            &serialized_transaction, &serialized_transaction_len) != 0) {
            goto error;
        }

        if (fprintf(stream, "transaction_length=%zu\n", serialized_transaction_len) < 0 ||
            fwrite(serialized_transaction, 1, serialized_transaction_len, stream) != serialized_transaction_len) {
            free(serialized_transaction);
            goto error;
        }

        free(serialized_transaction);
    }

    if (fprintf(stream,
        "nonce=%" PRIu64 "\n"
        "miner_address=%s\n"
        "hash=%s",
        block->nonce,
        block->miner_address,
        block->hash) < 0) {
        goto error;
    }

    if (fclose(stream) != 0) {
        free(*output);
        *output = NULL;
        *output_length = 0;
        return -1;
    }

    return 0;

error:
    fclose(stream);
    free(*output);
    *output = NULL;
    *output_length = 0;
    return -1;
}

/****************Network Deserialization****************/

/*
 * Reads one named field from a serialized network message.
 * @param cursor Pointer to the current parse position.
 * @param field_name Name of the field to read.
 * @param value Buffer receiving the field value.
 * @param value_size Size of the value buffer.
 * @return 0 on success, -1 on failure.
 */
static int read_network_field(char **cursor, const char *field_name, char *value, size_t value_size) {
    if (cursor == NULL || *cursor == NULL || field_name == NULL || value == NULL || value_size == 0) {
        return -1;
    }

    char *line_start = *cursor;
    size_t field_length = strlen(field_name);

    if (strncmp(line_start, field_name, field_length) != 0 || line_start[field_length] != '=') {
        return -1;
    }

    char *value_start = line_start + field_length + 1;
    char *line_end = strchr(value_start, '\n');
    if (line_end == NULL) {
        line_end = value_start + strlen(value_start);
    }

    size_t length = (size_t)(line_end - value_start);
    if (length >= value_size) {
        return -1;
    }

    memcpy(value, value_start, length);
    value[length] = '\0';

    *cursor = (*line_end == '\n') ? line_end + 1 : line_end;
    return 0;
}

/*
 * Reads one uint64 field from a network message.
 */
static int read_uint64_field(char **cursor, const char *field_name, uint64_t *value) {
    char text[32];
    char *end = NULL;

    if (value == NULL || read_network_field(cursor, field_name, text, sizeof(text)) != 0 || text[0] == '-') {
        return -1;
    }

    errno = 0;
    unsigned long long parsed = strtoull(text, &end, 10);
    if (errno != 0 || end == text || *end != '\0') {
        return -1;
    }

    *value = (uint64_t)parsed;
    return 0;
}

/*
 * Reads one size_t field from a network message.
 */
static int read_size_field(char **cursor, const char *field_name, size_t *value) {
    uint64_t parsed = 0;

    if (value == NULL || read_uint64_field(cursor, field_name, &parsed) != 0 || parsed > SIZE_MAX) {
        return -1;
    }

    *value = (size_t)parsed;
    return 0;
}

/**
 * Deserializes a block from network transmission.
 * @param serialized_block Pointer to the serialized block.
 * @param serialized_block_len The length of the serialized block.
 * @param block Pointer to the block to populate.
 * @return 0 on success, -1 on failure.
 */
int deserialize_block_from_network(const char *serialized_block, size_t serialized_block_len, block_t *block) {
    if (serialized_block == NULL || block == NULL || serialized_block_len == SIZE_MAX) {
        return -1;
    }

    memset(block, 0, sizeof(*block));

    char *buffer = malloc(serialized_block_len + 1);
    if (buffer == NULL) {
        return -1;
    }

    memcpy(buffer, serialized_block, serialized_block_len);
    buffer[serialized_block_len] = '\0';

    char *cursor = buffer;
    char *buffer_end = buffer + serialized_block_len;
    size_t expected_transaction_count = 0;

    if (read_uint64_field(&cursor, "index", &block->index) != 0 ||
        read_network_field(&cursor, "timestamp", block->timestamp, sizeof(block->timestamp)) != 0 ||
        read_network_field(&cursor, "prev_hash", block->prev_hash, sizeof(block->prev_hash)) != 0 ||
        read_size_field(&cursor, "transaction_count", &expected_transaction_count) != 0 ||
        expected_transaction_count > SIZE_MAX / sizeof(transaction_t)) {
        goto error;
    }

    if (expected_transaction_count > 0) {
        block->transactions = calloc(expected_transaction_count, sizeof(transaction_t));
        if (block->transactions == NULL) {
            goto error;
        }
    }

    for (size_t i = 0; i < expected_transaction_count; i++) {
        size_t transaction_length = 0;

        if (read_size_field(&cursor, "transaction_length", &transaction_length) != 0 ||
            transaction_length > (size_t)(buffer_end - cursor) ||
            deserialize_transaction_from_network(cursor, transaction_length, &block->transactions[i]) != 0) {
            goto error;
        }

        cursor += transaction_length;
        block->transaction_count++;
    }

    if (read_uint64_field(&cursor, "nonce", &block->nonce) != 0 ||
        read_network_field(&cursor, "miner_address", block->miner_address, sizeof(block->miner_address)) != 0 ||
        read_network_field(&cursor, "hash", block->hash, sizeof(block->hash)) != 0 ||
        cursor != buffer_end) {
        goto error;
    }

    free(buffer);
    return 0;

error:
    free(buffer);
    free_block_data(block);
    memset(block, 0, sizeof(*block));
    return -1;
}

/**
 * Validates the next block's structure, hash and proof of work.
 * Full UTXO, signature, fee and reward validation is performed by the
 * full node validation layer before this function's result is accepted.
 * @param block Pointer to the current block.
 * @param previous Pointer to the previous block.
 * @param difficulty The difficulty to validate against.
 * @return 1 if the block structure is valid, 0 otherwise.
 */
int validate_next_block(const block_t *block, const block_t *previous, int difficulty) {
    if (block == NULL || previous == NULL || block->transaction_count == 0 || block->transactions == NULL) {
        return 0;
    }

    if (block->index != previous->index + 1 ||
        strcmp(block->prev_hash, previous->hash) != 0) {
        return 0;
    }

    char calculated_hash[BLOCK_HASH_SIZE];
    if (get_block_hash(block, calculated_hash) != 0 ||
        strcmp(calculated_hash, block->hash) != 0 ||
        !check_hash_difficulty(calculated_hash, difficulty)) {
        return 0;
    }

    //the first transaction must be the only coinbase in the block
    if (!is_coinbase_transaction(&block->transactions[0]) ||
        block->transactions[0].inputs[0].coinbase_height != block->index) {
        return 0;
    }

    for (size_t i = 0; i < block->transaction_count; i++) {
        const transaction_t *transaction = &block->transactions[i];

        if (!transaction_id_matches(transaction) ||
            transaction->output_count == 0 ||
            transaction->outputs == NULL) {
            return 0;
        }

        if (i > 0 && (is_coinbase_transaction(transaction) ||
            transaction->input_count == 0 || transaction->inputs == NULL)) {
            return 0;
        }

        for (size_t j = 0; j < transaction->output_count; j++) {
            if (transaction->outputs[j].amount == 0) {
                return 0;
            }
        }
    }

    return 1;
}

/***************Free Functions***************/

/**
 * Frees the memory allocated for a block's transactions.
 * @param block Pointer to the block whose transactions to free.
 */
void free_block_data(block_t *block) {
    if (block == NULL) {
        return;
    }

    if (block->transactions != NULL) {
        for (size_t i = 0; i < block->transaction_count; i++) {
            free_transaction(&block->transactions[i]);
        }

        free(block->transactions);
    }

    block->transactions = NULL;
    block->transaction_count = 0;
}

/**
 * Frees the memory allocated for a blockchain.
 * @param chain Pointer to the blockchain to free.
 */
void free_blockchain(blockchain_t *chain) {
    if (chain == NULL) {
        return;
    }

    if (chain->blocks != NULL) {
        for (size_t i = 0; i < chain->count; i++) {
            free_block_data(&chain->blocks[i]);
        }

        free(chain->blocks);
    }

    chain->blocks = NULL;
    chain->count = 0;
    chain->capacity = 0;
}
