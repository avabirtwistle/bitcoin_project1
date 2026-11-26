#include "validation.h"

#include <string.h>

//helpers
static int outpoints_equal(const outpoint_t *first, const outpoint_t *second);
static int transaction_has_duplicate_input(const transaction_t *transaction, size_t input_index);
static int validate_standard_transaction(
    const transaction_t *transaction,
    const utxo_set_t *utxos,
    const mempool_t *mempool,
    uint64_t *fee,
    validation_result_t *result
);
static int block_spends_outpoint_before(
    const block_t *block,
    size_t transaction_index,
    const outpoint_t *outpoint
);
static int mempool_spends_outpoint_before(
    const mempool_t *mempool,
    const transaction_node_t *stop,
    const outpoint_t *outpoint
);
static void set_result(validation_result_t *result, validation_result_t value);

/**
 * Stores a validation result when the caller requested one.
 * @param result Pointer to the caller's validation result variable.
 * @param value Validation result to store.
 */
static void set_result(validation_result_t *result, validation_result_t value) {
    if (result != NULL) {
        *result = value;
    }
}

/**
 * Returns a short explanation for a validation result.
 * @param result Validation result to explain.
 * @return A short explanation for the validation result.
 */
const char *validation_result_message(validation_result_t result) {
    switch (result) {
        case VALIDATION_OK:
            return "valid";
        case VALIDATION_INVALID_STRUCTURE:
            return "invalid transaction structure";
        case VALIDATION_INVALID_TXID:
            return "transaction ID does not match its contents";
        case VALIDATION_INVALID_OUTPUT:
            return "output amount or recipient address is invalid";
        case VALIDATION_COINBASE_NOT_ALLOWED:
            return "coinbase transactions cannot enter the mempool";
        case VALIDATION_DUPLICATE_TRANSACTION:
            return "transaction is already present";
        case VALIDATION_INPUT_NOT_FOUND:
            return "input does not reference a confirmed unspent output";
        case VALIDATION_DUPLICATE_INPUT:
            return "an outpoint is spent more than once";
        case VALIDATION_MEMPOOL_DOUBLE_SPEND:
            return "outpoint is already reserved by the mempool";
        case VALIDATION_WRONG_PUBLIC_KEY:
            return "public key does not match the output address";
        case VALIDATION_INVALID_SIGNATURE:
            return "ECDSA signature is invalid";
        case VALIDATION_VALUE_OVERFLOW:
            return "transaction value overflow";
        case VALIDATION_INSUFFICIENT_INPUT:
            return "outputs exceed inputs";
        case VALIDATION_INVALID_COINBASE:
            return "coinbase transaction is invalid";
        case VALIDATION_EXCESSIVE_COINBASE:
            return "coinbase reward exceeds subsidy plus fees";
    }

    return "unknown validation error";
}

/**
 * Checks whether two outpoints identify the same output.
 * @param first Pointer to the first outpoint.
 * @param second Pointer to the second outpoint.
 * @return 1 if equal, 0 otherwise.
 */
static int outpoints_equal(const outpoint_t *first, const outpoint_t *second) {
    if (first == NULL || second == NULL) {
        return 0;
    }

    return strcmp(first->prev_txid, second->prev_txid) == 0 &&
           first->output_index == second->output_index;
}

/**
 * Checks whether the input at input_index duplicates an earlier input.
 * @param transaction Pointer to the transaction.
 * @param input_index Index of the input to check.
 * @return 1 if the input duplicates an earlier input, 0 otherwise.
 */
static int transaction_has_duplicate_input(const transaction_t *transaction, size_t input_index) {
    for (size_t i = 0; i < input_index; i++) {
        if (outpoints_equal(
            &transaction->inputs[i].outpoint,
            &transaction->inputs[input_index].outpoint
        )) {
            return 1;
        }
    }

    return 0;
}

/**
 * Runs the four required checks on a normal transaction.
 * Passing NULL for mempool skips only the mempool reservation check.
 * @param transaction Pointer to the transaction to validate.
 * @param utxos Pointer to the UTXO set.
 * @param mempool Pointer to the mempool, or NULL to skip the mempool check.
 * @param fee Pointer to the variable receiving the transaction fee, or NULL to skip fee calculation.
 * @param result Pointer to the variable receiving the validation result, or NULL to skip result reporting.
 * @return 1 if the transaction is valid, 0 otherwise.
 */
static int validate_standard_transaction(
    const transaction_t *transaction,
    const utxo_set_t *utxos,
    const mempool_t *mempool,
    uint64_t *fee,
    validation_result_t *result
) {
    if (transaction == NULL || utxos == NULL || transaction->input_count == 0 ||
        transaction->inputs == NULL || transaction->output_count == 0 ||
        transaction->outputs == NULL) {
        set_result(result, VALIDATION_INVALID_STRUCTURE);
        return 0;
    }

    if (is_coinbase_transaction(transaction)) {
        set_result(result, VALIDATION_COINBASE_NOT_ALLOWED);
        return 0;
    }

    if (!transaction_id_matches(transaction)) {
        set_result(result, VALIDATION_INVALID_TXID);
        return 0;
    }

    uint64_t input_total = 0;
    uint64_t output_total = 0;

    for (size_t i = 0; i < transaction->output_count; i++) {
        const tx_output_t *output = &transaction->outputs[i];

        if (output->amount == 0 || !is_valid_address(output->recipient_address)) {
            set_result(result, VALIDATION_INVALID_OUTPUT);
            return 0;
        }

        if (UINT64_MAX - output_total < output->amount) {
            set_result(result, VALIDATION_VALUE_OVERFLOW);
            return 0;
        }

        output_total += output->amount;
    }

    for (size_t i = 0; i < transaction->input_count; i++) {
        const tx_input_t *input = &transaction->inputs[i];

        if (input->coinbase_height != 0 ||
            transaction_has_duplicate_input(transaction, i)) {
            set_result(result, VALIDATION_DUPLICATE_INPUT);
            return 0;
        }

        const utxo_entry_t *utxo = find_utxo(utxos, &input->outpoint);
        if (utxo == NULL) {
            set_result(result, VALIDATION_INPUT_NOT_FOUND);
            return 0;
        }

        if (mempool != NULL && mempool_spends_outpoint(mempool, &input->outpoint)) {
            set_result(result, VALIDATION_MEMPOOL_DOUBLE_SPEND);
            return 0;
        }

        char signer_address[ADDRESS_SIZE + 1];
        if (public_key_to_address(input->public_key, signer_address) != 0 ||
            strcmp(signer_address, utxo->output.recipient_address) != 0) {
            set_result(result, VALIDATION_WRONG_PUBLIC_KEY);
            return 0;
        }

        if (!verify_transaction_input_signature(transaction, i)) {
            set_result(result, VALIDATION_INVALID_SIGNATURE);
            return 0;
        }

        if (UINT64_MAX - input_total < utxo->output.amount) {
            set_result(result, VALIDATION_VALUE_OVERFLOW);
            return 0;
        }

        input_total += utxo->output.amount;
    }

    if (input_total < output_total) {
        set_result(result, VALIDATION_INSUFFICIENT_INPUT);
        return 0;
    }

    if (fee != NULL) {
        *fee = input_total - output_total;
    }

    set_result(result, VALIDATION_OK);
    return 1;
}

/**
 * Validates a new transaction before it enters a mempool.
 * Passing NULL for mempool skips only the mempool reservation check.
 * @param transaction Pointer to the transaction to validate.
 * @param utxos Pointer to the UTXO set.
 * @param mempool Pointer to the mempool, or NULL to skip the mempool check.
 * @param fee Pointer to the variable receiving the transaction fee, or NULL to skip fee calculation.
 * @param result Pointer to the variable receiving the validation result, or NULL to skip result reporting.
 * @return 1 if the transaction is valid, 0 otherwise.
 */
int validate_transaction_for_mempool(
    const transaction_t *transaction,
    const utxo_set_t *utxos,
    const mempool_t *mempool,
    uint64_t *fee,
    validation_result_t *result
) {
    if (mempool == NULL) {
        set_result(result, VALIDATION_INVALID_STRUCTURE);
        return 0;
    }

    //check for duplicate transaction in the mempool
    if (transaction != NULL && mempool_contains_txid(mempool, transaction->txid)) {
        set_result(result, VALIDATION_DUPLICATE_TRANSACTION);
        return 0;
    }

    return validate_standard_transaction(transaction, utxos, mempool, fee, result);
}

/** 
 * Checks whether an earlier mempool transaction spends an outpoint.
 * @param mempool Pointer to the mempool.
 * @param stop Pointer to the transaction node to stop at, or NULL to check all transactions.
 * @param outpoint Pointer to the outpoint to check.
 * @return 1 if an earlier transaction spends the outpoint, 0 otherwise.
 */
static int mempool_spends_outpoint_before(
    const mempool_t *mempool,
    const transaction_node_t *stop,
    const outpoint_t *outpoint
) {
    transaction_node_t *current = mempool->head;

    while (current != NULL && current != stop) {
        for (size_t i = 0; i < current->transaction.input_count; i++) {
            if (outpoints_equal(&current->transaction.inputs[i].outpoint, outpoint)) {
                return 1;
            }
        }
        current = current->next;
    }

    return 0;
}


/**
 * Revalidates all pending transactions and totals their fees before mining.
 * Passing NULL for mempool skips only the mempool reservation check.
 * @param mempool Pointer to the mempool.
 * @param utxos Pointer to the UTXO set.
 * @param total_fees Pointer to the variable receiving the total fees.
 * @param result Pointer to the variable receiving the validation result, or NULL to skip result reporting.
 * @return 1 if all transactions are valid, 0 otherwise.
 */
int calculate_mempool_fees(
    const mempool_t *mempool,
    const utxo_set_t *utxos,
    uint64_t *total_fees,
    validation_result_t *result
) {
    if (mempool == NULL || utxos == NULL || total_fees == NULL) {
        set_result(result, VALIDATION_INVALID_STRUCTURE);
        return 0;
    }

    uint64_t fees = 0;
    transaction_node_t *current = mempool->head;

    while (current != NULL) {
        uint64_t transaction_fee = 0;

        if (!validate_standard_transaction(
            &current->transaction,
            utxos,
            NULL,
            &transaction_fee,
            result
        )) {
            return 0;
        }

        for (size_t i = 0; i < current->transaction.input_count; i++) {
            if (mempool_spends_outpoint_before(
                mempool,
                current,
                &current->transaction.inputs[i].outpoint
            )) {
                set_result(result, VALIDATION_MEMPOOL_DOUBLE_SPEND);
                return 0;
            }
        }

        if (UINT64_MAX - fees < transaction_fee) {
            set_result(result, VALIDATION_VALUE_OVERFLOW);
            return 0;
        }

        fees += transaction_fee;
        current = current->next;
    }

    *total_fees = fees;
    set_result(result, VALIDATION_OK);
    return 1;
}

/**
 * Checks whether an earlier transaction in a block spends an outpoint.
 * @param block Pointer to the block.
 * @param transaction_index Index of the transaction to check.
 * @param outpoint Pointer to the outpoint to check.
 * @return 1 if an earlier transaction spends the outpoint, 0 otherwise.    
 */
static int block_spends_outpoint_before(
    const block_t *block,
    size_t transaction_index,
    const outpoint_t *outpoint
) {
    for (size_t i = 1; i < transaction_index; i++) {
        for (size_t j = 0; j < block->transactions[i].input_count; j++) {
            if (outpoints_equal(&block->transactions[i].inputs[j].outpoint, outpoint)) {
                return 1;
            }
        }
    }

    return 0;
}

/**
 * Validates coinbase rules and every normal transaction in a block.
 * Passing NULL for total_fees skips fee calculation.
 * @param block Pointer to the block.
 * @param utxos Pointer to the UTXO set.
 * @param total_fees Pointer to the variable receiving the total fees, or NULL to skip fee calculation.
 * @param result Pointer to the variable receiving the validation result, or NULL to skip result reporting.
 * @return 1 if all transactions are valid, 0 otherwise.
 */
int validate_block_transactions(
    const block_t *block,
    const utxo_set_t *utxos,
    uint64_t *total_fees,
    validation_result_t *result
) {
    if (block == NULL || utxos == NULL || block->transaction_count == 0 ||
        block->transactions == NULL) {
        set_result(result, VALIDATION_INVALID_STRUCTURE);
        return 0;
    }

    const transaction_t *coinbase = &block->transactions[0];
    if (!is_coinbase_transaction(coinbase) || !transaction_id_matches(coinbase) ||
        coinbase->inputs[0].coinbase_height != block->index ||
        coinbase->inputs[0].signature_length != 0 ||
        coinbase->output_count != 1 || coinbase->outputs == NULL ||
        coinbase->outputs[0].amount == 0 ||
        strcmp(coinbase->outputs[0].recipient_address, block->miner_address) != 0 ||
        !is_valid_address(block->miner_address)) {
        set_result(result, VALIDATION_INVALID_COINBASE);
        return 0;
    }

    uint64_t fees = 0;

    for (size_t i = 1; i < block->transaction_count; i++) {
        const transaction_t *transaction = &block->transactions[i];
        uint64_t transaction_fee = 0;

        if (!validate_standard_transaction(
            transaction,
            utxos,
            NULL,
            &transaction_fee,
            result
        )) {
            return 0;
        }

        for (size_t j = 0; j < transaction->input_count; j++) {
            if (block_spends_outpoint_before(
                block,
                i,
                &transaction->inputs[j].outpoint
            )) {
                set_result(result, VALIDATION_DUPLICATE_INPUT);
                return 0;
            }
        }

        for (size_t j = 1; j < i; j++) {
            if (strcmp(transaction->txid, block->transactions[j].txid) == 0) {
                set_result(result, VALIDATION_DUPLICATE_TRANSACTION);
                return 0;
            }
        }

        if (UINT64_MAX - fees < transaction_fee) {
            set_result(result, VALIDATION_VALUE_OVERFLOW);
            return 0;
        }

        fees += transaction_fee;
    }

    if (fees > UINT64_MAX - BLOCK_SUBSIDY ||
        coinbase->outputs[0].amount > BLOCK_SUBSIDY + fees) {
        set_result(result, VALIDATION_EXCESSIVE_COINBASE);
        return 0;
    }

    if (total_fees != NULL) {
        *total_fees = fees;
    }

    set_result(result, VALIDATION_OK);
    return 1;
}
