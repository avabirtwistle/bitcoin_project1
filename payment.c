#include "payment.h"

#include <string.h>

/*
 * Creates and signs a payment using confirmed, unreserved wallet UTXOs.
 * A change output is created whenever the selected inputs exceed amount + fee.
 *
 * @param transaction Transaction receiving the payment.
 * @param utxos Current confirmed UTXO set.
 * @param mempool Current local mempool.
 * @param wallet Wallet signing the transaction.
 * @param recipient_address Address receiving the payment.
 * @param amount Positive amount to send.
 * @param fee Miner fee left unallocated by the outputs.
 * @return 0 on success, PAYMENT_INSUFFICIENT_FUNDS when funds are unavailable,
 *         or PAYMENT_ERROR on another failure.
 */
int create_payment_transaction(
    transaction_t *transaction,
    const utxo_set_t *utxos,
    const mempool_t *mempool,
    const wallet_t *wallet,
    const char *recipient_address,
    uint64_t amount,
    uint64_t fee
) {
    if (transaction == NULL || utxos == NULL || mempool == NULL || wallet == NULL ||
        amount == 0 || !is_valid_address(recipient_address) ||
        !is_valid_address(wallet->address) || fee > UINT64_MAX - amount) {
        return PAYMENT_ERROR;
    }

    memset(transaction, 0, sizeof(*transaction));

    uint64_t required_amount = amount + fee;
    uint64_t selected_amount = 0;
    size_t selected_count = 0;

    //select confirmed UTXOs that are not already reserved by the mempool
    for (size_t i = 0; i < utxos->count && selected_amount < required_amount; i++) {
        const utxo_entry_t *utxo = &utxos->entries[i];

        if (strcmp(utxo->output.recipient_address, wallet->address) != 0 ||
            mempool_spends_outpoint(mempool, &utxo->outpoint)) {
            continue;
        }

        if (UINT64_MAX - selected_amount < utxo->output.amount ||
            selected_count == SIZE_MAX) {
            return PAYMENT_ERROR;
        }

        selected_amount += utxo->output.amount;
        selected_count++;
    }

    if (selected_amount < required_amount) {
        return PAYMENT_INSUFFICIENT_FUNDS;
    }

    size_t output_count = selected_amount == required_amount ? 1 : 2;

    transaction->inputs = calloc(selected_count, sizeof(tx_input_t));
    transaction->outputs = calloc(output_count, sizeof(tx_output_t));
    if (transaction->inputs == NULL || transaction->outputs == NULL) {
        free_transaction(transaction);
        return PAYMENT_ERROR;
    }

    transaction->input_count = selected_count;
    transaction->output_count = output_count;

    size_t input_index = 0;
    uint64_t copied_amount = 0;

    for (size_t i = 0; i < utxos->count && input_index < selected_count; i++) {
        const utxo_entry_t *utxo = &utxos->entries[i];

        if (strcmp(utxo->output.recipient_address, wallet->address) != 0 ||
            mempool_spends_outpoint(mempool, &utxo->outpoint)) {
            continue;
        }

        transaction->inputs[input_index].outpoint = utxo->outpoint;
        copied_amount += utxo->output.amount;
        input_index++;

        if (copied_amount >= required_amount) {
            break;
        }
    }

    transaction->outputs[0].amount = amount;
    strcpy(transaction->outputs[0].recipient_address, recipient_address);

    if (output_count == 2) {
        transaction->outputs[1].amount = selected_amount - required_amount;
        strcpy(transaction->outputs[1].recipient_address, wallet->address);
    }

    if (input_index != selected_count || copied_amount != selected_amount ||
        sign_transaction(transaction, wallet) != 0) {
        free_transaction(transaction);
        return PAYMENT_ERROR;
    }

    return 0;
}
