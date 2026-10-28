#include "block.h"

//helpers
static int get_time(char timestamp[TIMESTAMP_SIZE]);
static int get_block_hash(const block_t *block, char hash_output[BLOCK_HASH_SIZE]);
static int resize_blockchain(blockchain_t *chain);
static int serialize_block_data(const block_t *block, char **serialized_block, size_t *serialized_block_len);

/*
 * Gets the current time and formats it as a string.
 * @param timestamp Pointer to the buffer to store the formatted timestamp.
 * @return 0 on success, -1 on failure.
 */
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
/*adds a message to the pending message list
 * @param pending Pointer to the pending message list.
 * @param message The message to add.
 * @return 0 on success, -1 on failure.
 */
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
/** Creates a new genesis block.
 * @param block Pointer to the block to create.
 * @return 0 on success, -1 on failure.
 */
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
/** Resizes the blockchain.
 * @param chain Pointer to the blockchain to resize.
 * @return 0 on success, -1 on failure.
 */
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
/** Creates a new block from the pending messages.
 * @param block Pointer to the block to create.
 * @param prev_block Pointer to the previous block.
 * @param pending Pointer to the pending message list.
 * @param miner_address The address of the miner.
 * @return 0 on success, -1 on failure.
 */
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
/** Checks if a hash meets the required difficulty.
 * @param hash The hash to check.
 * @param difficulty The required difficulty.
 * @return 1 if the hash meets the difficulty, 0 otherwise.
 */
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

/** Mines a single block.
 * @param block Pointer to the block to mine.
 * @param difficulty The difficulty to mine at.
 * @return 0 on success, -1 on failure.
 */
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

/** Serializes a block for network transmission.
 * @param block Pointer to the block to serialize.
 * @param output Pointer to the output buffer.
 * @param output_length Pointer to the output buffer length.
 * @return 0 on success, -1 on failure.
 */
int serialize_block_for_network(const block_t *block, char **output, size_t *output_length){
    if (block == NULL || output == NULL || output_length == NULL) {
        return -1; // Invalid input
    }
    *output = NULL;
    *output_length = 0;

    char *block_payload = NULL;
    size_t payload_length = 0;

    if (serialize_block_data(block, &block_payload, &payload_length) != 0) {
        return -1;
    }

    FILE *stream = open_memstream(output, output_length);
    if (stream == NULL) {
        free(block_payload);
        return -1;
    }

    //add the hash to the serialized block for sending over the network
    if (fprintf(stream, "%s\nhash=%s", block_payload, block->hash) < 0) {
        fclose(stream);
        free(block_payload);
        free(*output);
        *output = NULL;
        *output_length = 0;
        return -1;
    }

    free(block_payload);

    if (fclose(stream) != 0) {
        free(*output);
        *output = NULL;
        *output_length = 0;
        return -1;
    }

    return 0;
}

/** Deserializes a block from network transmission.
 * @param serialized_block Pointer to the serialized block.
 * @param serialized_block_len The length of the serialized block.
 * @param block Pointer to the block to populate.
 * @return 0 on success, -1 on failure.
 */
int deserialize_block_from_network(const char *serialized_block, size_t serialized_block_len, block_t *block){
    if (serialized_block == NULL || block == NULL) {
        return -1;
    }

    // Initialize the block structure to zero
    memset(block, 0, sizeof(*block));

    //allocate buffer to hold null terminated input string
    char *buffer = malloc(serialized_block_len + 1);
    if (buffer == NULL) {
        return -1;
    }

    //copy the serialized block into the buffer and null terminate it
    memcpy(buffer, serialized_block, serialized_block_len);
    buffer[serialized_block_len] = '\0';

    //parse the buffer to extract block fields
    char *cursor = buffer;
    int characters_read = 0; // count so we can move the cursor forward after each sscanf
    size_t expected_data_count = 0; //explected number of messages in the block

    //start cursor at buffer start, extract the index field
    sscanf(cursor,"index=%" SCNu64 "\n%n", &block->index, &characters_read);
    cursor += characters_read;//move cursor forward to timestamp field

    // extract timestamp
    sscanf(cursor, "timestamp=%20[^\n]\n%n", block->timestamp, &characters_read);
    cursor += characters_read; //move cursor forward to prev_hash field

    // extract previous hash
    sscanf( cursor, "prev_hash=%64[^\n]\n%n", block->prev_hash, &characters_read);
    cursor += characters_read;//move cursor forward to data_count field

    // extract data_count
    sscanf(cursor, "data_count=%zu\n%n", &expected_data_count, &characters_read);
    cursor += characters_read;

    if (expected_data_count > 0) { //check if there are messages to allocate memory for
        //allocate memory for the data array based on the expected number of messages
        block->data = malloc(expected_data_count * sizeof(char *));
        if (block->data == NULL) {
            free(buffer);
            return -1;//return error if memory allocation fails
        }
    }

    //extract each message from the serialized block
    for (size_t i = 0; i < expected_data_count; i++) {
        size_t message_length = 0;

        // extract the message length and data
        sscanf(cursor,"data=%zu:%n", &message_length,&characters_read);
        cursor += characters_read;//move forward to the start of the message data
        
        //extract the message data index i and allocate memory for it and null terminate it
        block->data[i] = malloc(message_length + 1);
        if (block->data[i] == NULL) {
            free(buffer);
            free_block_data(block);
            return -1;
        }

        // copy the message data into the allocated memory
        memcpy(block->data[i], cursor, message_length);
        block->data[i][message_length] = '\0';//null terminate the message string
        block->data_count++;//increment the data count in the block
        
        //move the cursor forward to the next message or the nonce field
        cursor += message_length;
        cursor++;
    }

    //when we reach here, we have extracted all the messages, now we need to extract the nonce and miner address
    sscanf(cursor, "nonce=%" SCNu64 "\n%n", &block->nonce, &characters_read);
    cursor += characters_read;

    //extract the miner address
    sscanf(cursor, "miner_address=%35s\n%n", block->miner_address, &characters_read);
    cursor += characters_read;

    //extract the hash sent by the peer
    sscanf(cursor, "hash=%64s", block->hash);

    free(buffer);

    return 0;
}

/** Validates the next block in the blockchain.
 * @param block Pointer to the current block.
 * @param previous Pointer to the previous block.
 * @param difficulty The difficulty to validate against.
 * @return 1 if the block is valid, 0 otherwise.
 */
int validate_next_block(const block_t *block, const block_t *previous, int difficulty){
    if (block == NULL || previous == NULL) { //if curr or prev block is null return error
        return 0;
    }

    // if the index of curr is not one greater than prev return error
    if (block->index != previous->index + 1) {
        return 0;
    }

    //check if the prev hash in current block matches the hash of the previous block
    if (strcmp(block->prev_hash, previous->hash) != 0) {
        return 0;
    }

    //calculate the hash of the current block 
    char calculated_hash[BLOCK_HASH_SIZE];
    if (get_block_hash(block, calculated_hash) != 0) {
        return 0;// Error calculating block hash
    }

    //compare the calculated hash with the hash stored in the block
    if (strcmp(calculated_hash, block->hash) != 0) {
        return 0;
    }

    //check if the hash meets the difficulty requirement
    return check_hash_difficulty(calculated_hash, difficulty);
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
