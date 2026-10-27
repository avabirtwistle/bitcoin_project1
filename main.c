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

    if (generate_vanity_wallet(keypair, input.vanity_prefix) != 0) {
        fprintf(stderr, "Failed to generate vanity wallet.\n");
        free(keypair);
        return EXIT_FAILURE;
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