#include "start_up.h"
#include "prefix.h"
#include "block.h"

int main(int argc, char *argv[]) {
    //hold the input from the user
    command_input input;

    //flag for wallet 
    int wallet_result = -1;

    //message related things
    message_list_t pending = {0};

    if (!read_input(&input, argc, argv)) {
        return EXIT_FAILURE;
    }

    printf("Vanity Prefix: %s\n", input.vanity_prefix);
    printf("Difficulty: %d\n", input.difficulty);
    printf("Node: %s\n", input.node);

    wallet_t *keypair = malloc(sizeof(wallet_t));
    if (keypair == NULL) {
        fprintf(stderr, "Failed to allocate memory for wallet.\n");
        return EXIT_FAILURE;
    }

    // Try to load an existing wallet.
    FILE *file = fopen("keypair.json", "r");
    if (file != NULL) {
        wallet_result = find_wallet(file, keypair);
        fclose(file);
    }

    // Generate and save a wallet if one was not loaded.
    if (wallet_result != 0) {
        fprintf(stderr, "Creating new wallet.\n");

        if (generate_vanity_wallet(keypair, input.vanity_prefix) != 0) {
            fprintf(stderr, "Failed to generate vanity wallet.\n");
            free(keypair);
            return EXIT_FAILURE;
        }

        file = fopen("keypair.json", "w");
        if (file == NULL) {
            perror("Failed to open keypair file for writing");
            free(keypair);
            return EXIT_FAILURE;
        }

        if (save_wallet(file, keypair) != 0) {
            fprintf(stderr, "Failed to save wallet.\n");
            fclose(file);
            free(keypair);
            return EXIT_FAILURE;
        }

        fclose(file);
    }

    printf("Private Key: ");
    for (int i = 0; i < PRIVATE_KEY_SIZE; i++) {
        printf("%02x", keypair->private_key[i]);
    }
    printf("\n");

    printf("Public Key: ");
    for (int i = 0; i < PUBLIC_KEY_SIZE; i++) {
        printf("%02x", keypair->public_key[i]);
    }
    printf("\n");

    printf("Address: %s\n", keypair->address);
blockchain_t chain = {0};

if (initialize_blockchain(&chain) != 0) {
    fprintf(stderr, "Failed to initialize blockchain.\n");
    free(keypair);
    return EXIT_FAILURE;
}

printf("Genesis hash: %s\n", chain.blocks[0].hash);

while (1) {
    // readline dynamically allocates enough memory for the complete command.
    char *command = readline("> ");
    if (command == NULL) {
        break;
    }

    if (strcmp(command, "exit") == 0) {
        free(command);
        break;
    }

    if (strncmp(command, "msg ", 4) == 0) {
        const char *message = command + 4;

        if (message[0] == '\0') {
            fprintf(stderr, "Message cannot be empty.\n");
        } else if (add_pending_message(&pending, message) != 0) {
            fprintf(stderr, "Failed to add message.\n");
        } else {
            printf("Message added. Pending messages: %zu\n", pending.count);
        }
    } else if (strcmp(command, "mine") == 0) {
        block_t new_block = {0};
        const block_t *previous_block = &chain.blocks[chain.count - 1];

        // Create the candidate and copy all pending messages into it.
        if (create_pending_block(&new_block, previous_block, &pending, keypair->address) != 0) {
            fprintf(stderr, "Failed to create block.\n");
        } else {
            if (mine_one_block(&new_block, input.difficulty) != 0) {
                fprintf(stderr, "Failed to mine block.\n");
                free_block_data(&new_block);
            } else if (append_block_to_chain(&chain, &new_block) != 0) {
                fprintf(stderr, "Failed to append block.\n");
                free_block_data(&new_block);
            } else {
                // append_block_to_chain moved new_block into the chain.
                const block_t *mined_block = &chain.blocks[chain.count - 1];

                // The mined block has its own message copies.
                free_message_list(&pending);

                printf("Block mined successfully.\n");
                printf("Index: %" PRIu64 "\n", mined_block->index);
                printf("Timestamp: %s\n", mined_block->timestamp);
                printf("Previous hash: %s\n", mined_block->prev_hash);
                printf("Nonce: %" PRIu64 "\n", mined_block->nonce);
                printf("Miner: %s\n", mined_block->miner_address);
                printf("Hash: %s\n", mined_block->hash);
                printf("Messages: %zu\n", mined_block->data_count);

                for (size_t i = 0; i < mined_block->data_count; i++) {
                    printf("  %zu: %s\n", i + 1, mined_block->data[i]);
                }
            }
        }
    } else {
        printf("Commands:\n");
        printf("  msg <text>  Add a pending message\n");
        printf("  mine        Mine the pending messages\n");
        printf("  exit        Exit the program\n");
    }

    free(command);
}

free_message_list(&pending);
free_blockchain(&chain);
free(keypair);
return EXIT_SUCCESS;
}