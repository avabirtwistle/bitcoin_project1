#include "block.h"

//helpers
static int get_time(char timestamp[TIMESTAMP_SIZE]);
static int get_block_hash(const block_t *block, char hash_output[BLOCK_HASH_SIZE]);
static int resize_blockchain(blockchain_t *chain);
static int serialize_block_data(const block_t *block, char **serialized_block, size_t *serialized_block_len);

static int get_time(char timestamp[TIMESTAMP_SIZE]) {
    time_t now = time(NULL);

    // current time in UTC
    struct tm *tm_info = gmtime(&now);

    // Check if localtime returned NULL
    if (tm_info == NULL) {
        return -1; // Error getting local time
    }

    // format into string YYYY-MM-DD HH:MM:SS
    if (strftime(timestamp, TIMESTAMP_SIZE, "%Y-%m-%dT%H:%M:%SZ", tm_info) == 0) {
        return -1; // Error formatting timestamp
    }

    return 0; // Success
}
/*adds a message to the pending message list*/
int add_pending_message(message_list_t *pending, const char *message) {
    message_node_t *new_message = malloc(sizeof(message_node_t));
    if (new_message == NULL) {
        return -1; // Memory allocation failed
    }

    //allocate memory for message
    new_message->message = malloc(strlen(message)+1);
    if (new_message->message == NULL) {
        free(new_message);
        return -1; // Memory allocation failed
    }

    //copy the message to the new node and init pointer
    memcpy(new_message->message, message, strlen(message) + 1);
    new_message->next = NULL;

    if (pending->head_message == NULL) { //if list empty
        pending->head_message = new_message;
        pending->tail_message = new_message;
    } else { 
        pending->tail_message->next = new_message;
        pending->tail_message = new_message;
    }
    pending->count++;// increment count of pending messages 
    return 0;
}
/*Block operations*/

/** Initializes a new blockchain with a genesis block. 
 * @param chain Pointer to the blockchain structure to initialize.
 * @return 0 on success, -1 on failure.
*/
int initialize_blockchain(blockchain_t *chain) {
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

int create_genesis_block(block_t *block) {
    const char genesis_message[] = "Genesis Block";

    // Set every field and pointer in the block to zero initially.
    memset(block, 0, sizeof(block_t));

    block->index = 0;
    block->nonce = 0;

    // set the prev hash to all zero
    memset(block->prev_hash, '0', BLOCK_HASH_SIZE - 1);
    block->prev_hash[BLOCK_HASH_SIZE - 1] = '\0';

    //use assignment time stamp
    strcpy(block->timestamp, "2026-01-01T00:00:00Z");
    strcpy(block->miner_address, "GENESIS");

    // add the one message
    block->data = malloc(sizeof(char *));
    if (block->data == NULL) { // Memory allocation failed
        return -1;
    }

    block->data[0] = malloc(strlen(genesis_message) + 1);
    if (block->data[0] == NULL) { // Memory allocation failed
        free_block_data(block);
        return -1;
    }

    // copy message and null terminator
    memcpy(block->data[0], genesis_message, strlen(genesis_message) + 1);
    block->data_count = 1;

    //calculate block hash
    if (get_block_hash(block, block->hash) != 0) {
        free_block_data(block);
        return -1;
    }

    return 0;
}

static int resize_blockchain(blockchain_t *chain) {
    size_t new_capacity = chain->capacity * 2;

    //reallocate memoy for the block array ith new capacity
    block_t *new_blocks = realloc(chain->blocks, new_capacity * sizeof(block_t));
    if (new_blocks == NULL) {
        return -1; // Memory allocation failed
    }
    chain->blocks = new_blocks;
    chain->capacity = new_capacity;
    return 0;
}

int create_pending_block(block_t *block, const block_t *prev_block, const message_list_t *pending, const char *miner_address){
    if (block == NULL || prev_block == NULL || pending == NULL || miner_address == NULL || strlen(miner_address) > ADDRESS_SIZE) {
        return -1;
    }

    //initialize the block 
    memset(block, 0, sizeof(block_t)); //set everything to 0
    block->index = prev_block->index + 1;
    block->nonce = 0;
    strcpy(block->prev_hash, prev_block->hash);
    strcpy(block->miner_address, miner_address);

    if(get_time(block->timestamp) != 0) {
        return -1; // Error getting timestamp
    }

    //if the pending message list is empty return
    if (pending->count == 0) {
        return 0;
    }

    //allocate mem for the data array
    block->data = malloc(pending->count * sizeof(char *));
    if (block->data == NULL) {
        return -1; // Memory allocation failed
    }

    message_node_t *current = pending->head_message;
    while(current!=NULL){
        size_t message_length = strlen(current->message);
        block->data[block->data_count] = malloc(message_length + 1);
        if (block->data[block->data_count] == NULL) {
            free_block_data(block);
            return -1; // Memory allocation failed
        }

        //copy the message into the block
        memcpy(block->data[block->data_count], current->message, message_length + 1);
        block->data_count++;
        current = current->next;
    }
    return 0;
}
/* Serializes the data of a block into a string. 
 * @param block Pointer to the block to serialize.
 * @param serialized_block Pointer to a string that will hold the serialized data.
 * @param serialized_block_len Pointer to a size_t that will hold the length of the serialized data.
 * @return 0 on success, -1 on failure.
 */
static int serialize_block_data(const block_t *block, char **serialized_block, size_t *serialized_block_len) {
    *serialized_block = NULL;
    *serialized_block_len = 0;

    // Creates a dynamically growing memory stream
    // When closed, output receives the allocated string and
    // output_length receives its length, excluding '\0'.
    FILE *stream = open_memstream(serialized_block, serialized_block_len);
    if (stream == NULL) {
        return -1;
    }

    // print the block data to the stream, including index, timestamp, previous hash, and data count
    if (fprintf(stream,
        "index=%" PRIu64 "\n" // Use PRIu64 for printing uint64_t
        "timestamp=%s\n"
        "prev_hash=%s\n"
        "data_count=%zu\n",
        block->index,
        block->timestamp,
        block->prev_hash,
        block->data_count) < 0) {
        goto error;
    }

    //go through each message in the blck and write to stream
    for (size_t i = 0; i < block->data_count; i++) {
        size_t message_length = strlen(block->data[i]);

        if (fprintf(stream, "data=%zu:%s\n", message_length, block->data[i]) < 0) {
            goto error;
        }
    }

    //print the nonce and miner address to the stream
    if (fprintf(stream,
        "nonce=%" PRIu64 "\n"
        "miner_address=%s",
        block->nonce,
        block->miner_address) < 0) {
        goto error;
    }

    // close the stream and finishes creating output
    if (fclose(stream) != 0) {
        free(*serialized_block);
        *serialized_block = NULL;
        *serialized_block_len = 0;
        return -1;
    }

    return 0;

error: // handle stream errors, close and free memory
    fclose(stream);
    free(*serialized_block);
    *serialized_block = NULL;
    *serialized_block_len = 0;
    return -1;
}

/** Calculates the hash of a block.
 * @param block Pointer to the block to calculate the hash of.
 * @param output Buffer to store the resulting hash.
 * @return 0 on success, -1 on failure.
 */
static int get_block_hash(const block_t *block, char hash_output[BLOCK_HASH_SIZE]) {
    char *serialized_block = NULL;
    size_t serialized_block_len = 0;
    uint8_t hash_data[NUM_HASH_BYTES];
    size_t bytes_written = 0;

    // Serialize the block data into a string
    if (serialize_block_data(block, &serialized_block, &serialized_block_len) != 0) {
        return -1;
    }



    if(EVP_Q_digest(NULL, "SHA256", NULL, serialized_block, serialized_block_len, hash_data, &bytes_written) != 1 || bytes_written != NUM_HASH_BYTES) {
        free(serialized_block);
        return -1; // Error during SHA-256 hashing
    }

    if(OPENSSL_buf2hexstr_ex(hash_output, BLOCK_HASH_SIZE, NULL, hash_data, NUM_HASH_BYTES, '\0') != 1) {
        free(serialized_block);
        return -1; // Error converting hash to hexadecimal string
    }

    free(serialized_block);
    return 0; // Success
}

/** Appends a block to a blockchain.
 * @param chain Pointer to the blockchain to append to.
 * @param block Pointer to the block to append.
 * @return 0 on success, -1 on failure.
 */
int append_block_to_chain(blockchain_t *chain, block_t *block) {
    if (chain == NULL || block == NULL) {
        return -1;
    }

    // use realloc to resize block array if needed
    if (chain->count == chain->capacity) {
        if (resize_blockchain(chain) != 0) {
            return -1;
        }
    }

    //copy the new block into the block chain and increment
    chain->blocks[chain->count] = *block;
    chain->count++;

    //reset the block to zero to avoid dangling pointers and double frees
    memset(block, 0, sizeof(block_t));
    return 0;
}



/********Mining*********/
static int check_hash_difficulty(const char *hash, int difficulty) {
    if(difficulty < 0 || difficulty > BLOCK_HASH_SIZE - 1) { //if difficulty is negative or greater than the hash length
        return 0; // Invalid difficulty
    }

    //check if the first difficulty characrers of the hash are 0
    for (int i = 0; i < difficulty; i++) {
        if (hash[i] != '0') {
            return 0; // Hash does not meet the difficulty requirement
        }
    }
    return 1; // Hash meets the difficulty requirement
}

int mine_one_block(block_t *block, int difficulty){
    if(block == NULL || difficulty < 0) {
        return -1; // Invalid input
    }

    block->nonce = 0; // Start nonce at 0

    while(1){
        //the nonce is included in serialization, so we need to recalculate the hash each time
        if(get_block_hash(block, block->hash) != 0) {
            return -1; // Error calculating block hash
        }
        if(check_hash_difficulty(block->hash, difficulty)) {
            return 0; // Successfully mined the block
        }

        //ensure the nonce does not overflow
        if(block->nonce == UINT64_MAX) {
            return -1; // Nonce overflow, cannot mine further or will wrap to zero
        }
        block->nonce++;
    }
}

/***************Free Functions***************/
/** Frees the memory allocated for a message list.
 * @param list Pointer to the message list to free.
 */
void free_message_list(message_list_t *list) {
    message_node_t *current = list->head_message;

    while (current != NULL) {
        message_node_t *next = current->next;
        free(current->message); // Free the message string
        free(current);          // Free the node
        current = next; // go to the next node
    }
    list->head_message = NULL;
    list->tail_message = NULL;
    list->count = 0;
}

/** Frees the memory allocated for a block's data.
 * @param block Pointer to the block whose data to free.
 */
void free_block_data(block_t *block) {
    if (block->data != NULL) {
        for (size_t i = 0; i < block->data_count; i++) {
            free(block->data[i]); // Free each message string
        }
        free(block->data); // Free the array of message pointers
        block->data = NULL;
        block->data_count = 0;
    }
}

/** Frees the memory allocated for a blockchain.
 * @param chain Pointer to the blockchain to free.
 */
void free_blockchain(blockchain_t *chain) {
    if (chain->blocks != NULL) {
        for (size_t i = 0; i < chain->count; i++) {
            free_block_data(&chain->blocks[i]);
        }
        free(chain->blocks);
        chain->blocks = NULL;
        chain->count = 0;
        chain->capacity = 0;
    }
}
