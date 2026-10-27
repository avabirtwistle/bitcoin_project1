#include "start_up.h"
#include "prefix.h"

int main(int argc, char *argv[]) {
    command_input input;
    int wallet_result = -1;

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

    free(keypair);
    return EXIT_SUCCESS;
}