#include "start_up.h"

/**
 * reads input and saves to struct
 * @param input The struct to save the input to.
 * @param argc The number of arguments.
 * @param argv The array of arguments.
 * @return The prefix entered by the user, or NULL if an error occurs.
 */
int read_input(command_input *input, int argc, char *argv[]) {
    if (argc != 7) {
        printf("Usage: %s --config file --vanity-prefix ABC --difficulty number\n",
               argv[0]);
        return 0;
    }

    char *vanity_prefix = argv[4];
    for (int i = 0; vanity_prefix[i]; i++) {
        vanity_prefix[i] = toupper(vanity_prefix[i]);
    }

    input->vanity_prefix = vanity_prefix;
    input->difficulty =  atoi(argv[6]);
    input->config_path = argv[2];

    return 1;
}

