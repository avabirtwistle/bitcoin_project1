#include "start_up.h"
#include "prefix.h"
#include "block.h"

int main(int argc, char *argv[]) {
    //hold the input from the user
    command_input input;

    //flag for wallet 
    int wallet_result = -1;

    //message related things
    char message_buffer[MAX_MESSAGE_SIZE];
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

    while(1){
        printf("%s >", input.node);
        if (fgets(message_buffer, sizeof(message_buffer), stdin) == NULL) {
            break; // exit
        }

        message_buffer[strcspn(message_buffer, "\n")] = 0; // remove newline
        if (strcmp(message_buffer, "exit") == 0) {
            break; // exit
        }else if (strcmp(message_buffer, "mine") == 0) {
            // Handle mining logic here
            printf("Mining...\n");
        } else {
            // Add the message to the pending message pool
            if (add_pending_message(&pending, message_buffer) != 0) {
                fprintf(stderr, "Failed to add message to pending pool.\n");
            } else {
                printf("Message added to pending pool.\n");
            }
        }   

        //allocate memory for message given 
        size_t len = strlen(message_buffer);
        char *message = malloc(len + 1);
        if (message == NULL) {
            fprintf(stderr, "Failed to allocate memory for message.\n");
            break;
        }

        //copy from temp buffer to allocated memory
        strcpy(message, message_buffer);


        free(message); // free the allocated memory for the message
    }
    free(keypair);
    return EXIT_SUCCESS;
}