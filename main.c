#include "start_up.h"
#include "prefix.h"
#include "block.h"
#include "network.h"
#include "payment.h"
#include "utxo.h"

#include <errno.h>

/*
 * Prints the transactions currently waiting in the local mempool.
 * @param mempool Pointer to the mempool to print.
 */
static void print_mempool(const mempool_t *mempool) {
    if (mempool == NULL) {
        return;
    }

    printf("Mempool transactions: %zu\n", mempool->count);

    transaction_node_t *current = mempool->head;
    size_t index = 0;

    while (current != NULL) {
        printf("  %zu: %s\n", index, current->transaction.txid);
        current = current->next;
        index++;
    }
}

/*
 * Prints a summary of every transaction in a block.
 * @param block Pointer to the block to print.
 */
static void print_block_transactions(const block_t *block) {
    if (block == NULL) {
        return;
    }

    printf("Transactions: %zu\n", block->transaction_count);

    for (size_t i = 0; i < block->transaction_count; i++) {
        const transaction_t *transaction = &block->transactions[i];

        printf(
            "  %zu: %s%s\n",
            i,
            transaction->txid,
            is_coinbase_transaction(transaction) ? " (coinbase)" : ""
        );

        for (size_t j = 0; j < transaction->output_count; j++) {
            printf(
                "      output %zu: %" PRIu64 " -> %s\n",
                j,
                transaction->outputs[j].amount,
                transaction->outputs[j].recipient_address
            );
        }
    }
}

/*
 * Prints every confirmed UTXO locked to one address.
 * @param utxos Pointer to the confirmed UTXO set.
 * @param address Address whose UTXOs should be printed.
 */
static void print_address_utxos(const utxo_set_t *utxos, const char *address) {
    if (utxos == NULL || address == NULL) {
        return;
    }

    size_t address_utxo_count = 0;

    for (size_t i = 0; i < utxos->count; i++) {
        if (strcmp(utxos->entries[i].output.recipient_address, address) != 0) {
            continue;
        }

        printf(
            "  %s:%" PRIu32 "  amount=%" PRIu64 "\n",
            utxos->entries[i].outpoint.prev_txid,
            utxos->entries[i].outpoint.output_index,
            utxos->entries[i].output.amount
        );
        address_utxo_count++;
    }

    printf("Confirmed UTXOs: %zu\n", address_utxo_count);
}

/*
 * Loads a node's configured wallet or creates it when it does not exist.
 * Node1 keeps the Assignment 1 vanity wallet. The other nodes use normal
 * secp256k1 keypairs because no additional vanity prefix is required.
 */
static int load_or_create_wallet(
    wallet_t *wallet,
    const char *wallet_file,
    const char *vanity_prefix
) {
    if (wallet == NULL || wallet_file == NULL || vanity_prefix == NULL) {
        return -1;
    }

    FILE *file = fopen(wallet_file, "r");
    if (file != NULL) {
        int result = find_wallet(file, wallet);
        fclose(file);

        if (result == 0) {
            return 0;
        }
    }

    fprintf(stderr, "Creating wallet %s.\n", wallet_file);

    if (strcmp(wallet_file, "keypair.json") == 0) {
        if (generate_vanity_wallet(wallet, vanity_prefix) != 0) {
            return -1;
        }
    } else if (generate_keypair(wallet) != 0 ||
               public_key_to_address(wallet->public_key, wallet->address) != 0) {
        return -1;
    }

    file = fopen(wallet_file, "w");
    if (file == NULL) {
        return -1;
    }

    int save_result = save_wallet(file, wallet);
    fclose(file);
    return save_result;
}

/*
 * Parses a non-negative whole-number command argument.
 */
static int parse_amount(const char *text, uint64_t *amount) {
    if (text == NULL || amount == NULL || text[0] == '\0' || text[0] == '-') {
        return -1;
    }

    char *end = NULL;
    errno = 0;
    unsigned long long value = strtoull(text, &end, 10);

    if (errno != 0 || end == text || *end != '\0') {
        return -1;
    }

    *amount = (uint64_t)value;
    return 0;
}

/*
 * Parses: send <recipient_address> <amount> --fee <fee>
 */
static int parse_send_command(
    const char *command,
    char recipient_address[ADDRESS_SIZE + 1],
    uint64_t *amount,
    uint64_t *fee
) {
    char amount_text[32];
    char fee_text[32];
    char extra_character = '\0';

    int fields = sscanf(
        command,
        "send %35s %31s --fee %31s %c",
        recipient_address,
        amount_text,
        fee_text,
        &extra_character
    );

    if (fields != 3 || parse_amount(amount_text, amount) != 0 ||
        parse_amount(fee_text, fee) != 0 || *amount == 0) {
        return -1;
    }

    return 0;
}

/*
 * Creates, validates, stores and broadcasts one wallet payment.
 */
static void handle_send_command(
    const char *command,
    wallet_t *wallet,
    utxo_set_t *utxos,
    mempool_t *mempool,
    pthread_mutex_t *chain_lock,
    network_t *network
) {
    char recipient_address[ADDRESS_SIZE + 1] = {0};
    uint64_t amount = 0;
    uint64_t requested_fee = 0;

    if (parse_send_command(
        command,
        recipient_address,
        &amount,
        &requested_fee
    ) != 0 || !is_valid_address(recipient_address)) {
        fprintf(stderr, "Usage: send <recipient_address> <amount> --fee <fee>\n");
        return;
    }

    transaction_t transaction = {0};
    validation_result_t validation_result = VALIDATION_INVALID_STRUCTURE;
    uint64_t calculated_fee = 0;

    pthread_mutex_lock(chain_lock);

    int create_result = create_payment_transaction(
        &transaction,
        utxos,
        mempool,
        wallet,
        recipient_address,
        amount,
        requested_fee
    );

    if (create_result == PAYMENT_INSUFFICIENT_FUNDS) {
        pthread_mutex_unlock(chain_lock);
        fprintf(stderr, "Transaction rejected: insufficient unreserved funds.\n");
        return;
    }

    if (create_result != 0 || !validate_transaction_for_mempool(
        &transaction,
        utxos,
        mempool,
        &calculated_fee,
        &validation_result
    ) || calculated_fee != requested_fee) {
        pthread_mutex_unlock(chain_lock);
        fprintf(
            stderr,
            "Transaction rejected: %s.\n",
            validation_result_message(validation_result)
        );
        free_transaction(&transaction);
        return;
    }

    if (add_pending_transaction(mempool, &transaction) != 0) {
        pthread_mutex_unlock(chain_lock);
        fprintf(stderr, "Transaction rejected: failed to add it to the mempool.\n");
        free_transaction(&transaction);
        return;
    }

    pthread_mutex_unlock(chain_lock);

    int peers_reached = network_broadcast_transaction(network, &transaction);

    printf("Transaction accepted: %s\n", transaction.txid);
    printf("Amount: %" PRIu64 "\n", amount);
    printf("Fee: %" PRIu64 "\n", calculated_fee);
    printf("Broadcast to %d peer%s.\n",
        peers_reached < 0 ? 0 : peers_reached,
        peers_reached == 1 ? "" : "s");

    free_transaction(&transaction);
}

/*
 * Revalidates the mempool, mines a candidate and commits its UTXO update.
 */
static void handle_mine_command(
    blockchain_t *chain,
    utxo_set_t *utxos,
    mempool_t *mempool,
    pthread_mutex_t *chain_lock,
    network_t *network,
    const wallet_t *wallet,
    int difficulty
) {
    block_t new_block = {0};
    utxo_set_t updated_utxos = {0};
    validation_result_t validation_result = VALIDATION_INVALID_STRUCTURE;
    uint64_t total_fees = 0;

    //keep the state locked while creating, mining and committing the block
    pthread_mutex_lock(chain_lock);
    const block_t *previous_block = &chain->blocks[chain->count - 1];

    if (!calculate_mempool_fees(
        mempool,
        utxos,
        &total_fees,
        &validation_result
    )) {
        pthread_mutex_unlock(chain_lock);
        fprintf(
            stderr,
            "Cannot mine invalid mempool: %s.\n",
            validation_result_message(validation_result)
        );
        return;
    }

    if (create_pending_block(
        &new_block,
        previous_block,
        mempool,
        wallet->address,
        total_fees
    ) != 0) {
        pthread_mutex_unlock(chain_lock);
        fprintf(stderr, "Failed to create block.\n");
        return;
    }

    if (mine_one_block(&new_block, difficulty) != 0) {
        pthread_mutex_unlock(chain_lock);
        fprintf(stderr, "Failed to mine block.\n");
        free_block_data(&new_block);
        return;
    }

    uint64_t validated_fees = 0;
    if (validate_next_block(&new_block, previous_block, difficulty) != 1 ||
        !validate_block_transactions(
            &new_block,
            utxos,
            &validated_fees,
            &validation_result
        ) || validated_fees != total_fees) {
        pthread_mutex_unlock(chain_lock);
        fprintf(
            stderr,
            "Mined block failed validation: %s.\n",
            validation_result_message(validation_result)
        );
        free_block_data(&new_block);
        return;
    }

    if (copy_utxo_set(&updated_utxos, utxos) != 0 ||
        apply_block_to_utxo_set(&updated_utxos, &new_block) != 0) {
        pthread_mutex_unlock(chain_lock);
        fprintf(stderr, "Failed to update UTXO set.\n");
        free_utxo_set(&updated_utxos);
        free_block_data(&new_block);
        return;
    }

    if (append_block_to_chain(chain, &new_block) != 0) {
        pthread_mutex_unlock(chain_lock);
        fprintf(stderr, "Failed to append block.\n");
        free_utxo_set(&updated_utxos);
        free_block_data(&new_block);
        return;
    }

    //append_block_to_chain moved new_block into the chain
    block_t mined_block = chain->blocks[chain->count - 1];

    free_utxo_set(utxos);
    *utxos = updated_utxos;
    memset(&updated_utxos, 0, sizeof(updated_utxos));

    remove_confirmed_transactions(
        mempool,
        mined_block.transactions,
        mined_block.transaction_count
    );
    pthread_mutex_unlock(chain_lock);

    printf("Block mined successfully.\n");
    printf("Index: %" PRIu64 "\n", mined_block.index);
    printf("Timestamp: %s\n", mined_block.timestamp);
    printf("Previous hash: %s\n", mined_block.prev_hash);
    printf("Nonce: %" PRIu64 "\n", mined_block.nonce);
    printf("Miner: %s\n", mined_block.miner_address);
    printf("Hash: %s\n", mined_block.hash);
    print_block_transactions(&mined_block);

    if (network_broadcast_block(network, &mined_block) != 0) {
        fprintf(stderr, "Failed to broadcast block.\n");
    }
}

int main(int argc, char *argv[]) {
    //hold the input from the user
    command_input input;

    //transaction mempool
    mempool_t pending = {0};

    //confirmed unspent transaction outputs
    utxo_set_t utxos = {0};

    if (!read_input(&input, argc, argv)) {
        return EXIT_FAILURE;
    }

    printf("Vanity Prefix: %s\n", input.vanity_prefix);
    printf("Difficulty: %d\n", input.difficulty);
    printf("Config Path: %s\n", input.config_path);

    node_config_t node_config = {0};
    if (load_node_config(input.config_path, &node_config) != 0) {
        return EXIT_FAILURE;
    }

    wallet_t *keypair = malloc(sizeof(wallet_t));
    if (keypair == NULL) {
        fprintf(stderr, "Failed to allocate memory for wallet.\n");
        return EXIT_FAILURE;
    }

    if (load_or_create_wallet(
        keypair,
        node_config.wallet_file,
        input.vanity_prefix
    ) != 0) {
        fprintf(stderr, "Failed to load or create wallet.\n");
        free(keypair);
        return EXIT_FAILURE;
    }

    printf("Public Key: ");
    for (int i = 0; i < PUBLIC_KEY_SIZE; i++) {
        printf("%02x", keypair->public_key[i]);
    }
    printf("\n");

    printf("Address: %s\n", keypair->address);

    blockchain_t chain = {0};
    pthread_mutex_t chain_lock = PTHREAD_MUTEX_INITIALIZER;
    network_t network;

    if (initialize_blockchain(&chain) != 0) {
        fprintf(stderr, "Failed to initialize blockchain.\n");
        pthread_mutex_destroy(&chain_lock);
        free(keypair);
        return EXIT_FAILURE;
    }

    if (network_start(
        &network,
        input.config_path,
        &chain,
        &utxos,
        &pending,
        &chain_lock,
        input.difficulty
    ) != 0) {
        fprintf(stderr, "Failed to start network.\n");
        pthread_mutex_destroy(&chain_lock);
        free_utxo_set(&utxos);
        free_blockchain(&chain);
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

        if (strcmp(command, "mempool") == 0) {
            pthread_mutex_lock(&chain_lock);
            print_mempool(&pending);
            pthread_mutex_unlock(&chain_lock);
        } else if (strcmp(command, "balance") == 0) {
            uint64_t balance = 0;

            pthread_mutex_lock(&chain_lock);
            int balance_result = get_address_balance(
                &utxos,
                keypair->address,
                &balance
            );
            pthread_mutex_unlock(&chain_lock);

            if (balance_result != 0) {
                fprintf(stderr, "Failed to calculate balance.\n");
            } else {
                printf("Confirmed balance: %" PRIu64 "\n", balance);
            }
        } else if (strcmp(command, "utxos") == 0) {
            pthread_mutex_lock(&chain_lock);
            print_address_utxos(&utxos, keypair->address);
            pthread_mutex_unlock(&chain_lock);
        } else if (strncmp(command, "send ", 5) == 0) {
            handle_send_command(
                command,
                keypair,
                &utxos,
                &pending,
                &chain_lock,
                &network
            );
        } else if (strcmp(command, "mine") == 0) {
            handle_mine_command(
                &chain,
                &utxos,
                &pending,
                &chain_lock,
                &network,
                keypair,
                input.difficulty
            );
        } else {
            printf("Commands:\n");
            printf("  mine      Mine a block with a coinbase transaction\n");
            printf("  send <address> <amount> --fee <fee>\n");
            printf("  mempool   List pending transactions\n");
            printf("  balance   Show this wallet's confirmed balance\n");
            printf("  utxos     List this wallet's confirmed UTXOs\n");
            printf("  exit      Exit the program\n");
        }

        free(command);
    }

    network_stop(&network);
    pthread_mutex_destroy(&chain_lock);
    free_mempool(&pending);
    free_utxo_set(&utxos);
    free_blockchain(&chain);
    free(keypair);
    return EXIT_SUCCESS;
}
