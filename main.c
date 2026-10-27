#include "start_up.h"
#include "prefix.h"

int main(int argc, char *argv[]) {
    command_input input;
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

    FILE *file = open_file("keypair.json", "w+");
    if (find_wallet(file, keypair) != 0) {
        //create
        fprintf(stderr, "Failed to find wallet in the specified file.\n");
        free(keypair);
        fclose(file);
        return EXIT_FAILURE;
    }
    fclose(file);

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

    // Use the keypair as needed...

    free(keypair); // Don't forget to free the allocated memory
    return EXIT_SUCCESS;
}
