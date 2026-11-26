#include "transaction.h"

#include <errno.h>
#include <inttypes.h>
#include <openssl/param_build.h>
#include <string.h>

//helpers
static int get_transaction_id(const transaction_t *transaction, char txid_output[TXID_SIZE]);
static int serialize_transaction_data(const transaction_t *transaction, char **serialized_transaction, size_t *serialized_transaction_len);
static int serialize_input_for_network(FILE *stream, const tx_input_t *input);
static int serialize_output_for_network(FILE *stream, const tx_output_t *output);
static int write_transaction_for_network(FILE *stream, const transaction_t *transaction);
static int read_transaction_from_network(char **cursor, transaction_t *transaction);
static int deserialize_input_from_network(char **cursor, tx_input_t *input);
static int deserialize_output_from_network(char **cursor, tx_output_t *output);
static int read_network_field(char **cursor, const char *field_name, char *value, size_t value_size);
static int read_uint64_field(char **cursor, const char *field_name, uint64_t *value);
static int read_uint32_field(char **cursor, const char *field_name, uint32_t *value);
static int read_size_field(char **cursor, const char *field_name, size_t *value);
static int is_zeroed_txid(const char *txid);
static int decode_transaction_id(const char *txid, uint8_t digest[HASH_SIZE]);
static EVP_PKEY *create_signing_key(const wallet_t *wallet);
static EVP_PKEY *create_verification_key(const uint8_t public_key[PUBLIC_KEY_SIZE]);

/** 
 * Checks whether a transaction ID contains 64 zero characters.
 * @param txid The transaction ID to check.
 * @return 1 if zeroed, 0 otherwise.
 */
static int is_zeroed_txid(const char *txid) {
    if (txid == NULL || strlen(txid) != TXID_SIZE - 1) {
        return 0;
    }

    for (size_t i = 0; i < TXID_SIZE - 1; i++) {
        if (txid[i] != '0') {
            return 0;
        }
    }

    return 1;
}

/** 
 * Checks if a transaction is a coinbase transaction.
 * A coinbase has one zeroed outpoint and includes the block height.
 * @param transaction The transaction to check.
 * @return 1 if coinbase, 0 otherwise.
 */
int is_coinbase_transaction(const transaction_t *transaction) {
    if (transaction == NULL || transaction->input_count != 1 || transaction->inputs == NULL) {
        return 0;
    }

    const tx_input_t *input = &transaction->inputs[0];

    return is_zeroed_txid(input->outpoint.prev_txid) &&
           input->outpoint.output_index == 0 &&
           input->coinbase_height > 0;
}

/** 
 * Serializes a transaction for its transaction ID.
 * The scriptSig fields are not included.
 * @param transaction Pointer to the transaction to serialize.
 * @param serialized_transaction Pointer to the resulting string.
 * @param serialized_transaction_len Pointer to the resulting length.
 * @return 0 on success, -1 on failure.
 */
static int serialize_transaction_data(const transaction_t *transaction, char **serialized_transaction, size_t *serialized_transaction_len) {
    if (transaction == NULL || serialized_transaction == NULL || serialized_transaction_len == NULL) {
        return -1;
    }

    if ((transaction->input_count > 0 && transaction->inputs == NULL) ||
        (transaction->output_count > 0 && transaction->outputs == NULL)) {
        return -1;
    }

    *serialized_transaction = NULL;
    *serialized_transaction_len = 0;

    FILE *stream = open_memstream(serialized_transaction, serialized_transaction_len);
    if (stream == NULL) {
        return -1;
    }

    if (fprintf(stream, "input_count=%zu\n", transaction->input_count) < 0) {
        goto error;
    }

    //write only the input outpoints, not the signature or public key
    for (size_t i = 0; i < transaction->input_count; i++) {
        const tx_input_t *input = &transaction->inputs[i];

        if (fprintf(stream, "prev_txid=%s\n" "output_index=%" PRIu32 "\n", input->outpoint.prev_txid, input->outpoint.output_index) < 0) {
            goto error;
        }

        //coinbase height makes each coinbase transaction ID unique
        if (is_coinbase_transaction(transaction)) {
            if (fprintf(stream, "coinbase_height=%" PRIu64 "\n", input->coinbase_height) < 0) {
                goto error;
            }
        }
    }

    if (fprintf(stream, "output_count=%zu\n", transaction->output_count) < 0) {
        goto error;
    }

    for (size_t i = 0; i < transaction->output_count; i++) {
        const tx_output_t *output = &transaction->outputs[i];

        if (fprintf(stream, "amount=%" PRIu64 "\n" "recipient_address=%s\n", output->amount, output->recipient_address) < 0) {
            goto error;
        }
    }

    if (fclose(stream) != 0) {
        free(*serialized_transaction);
        *serialized_transaction = NULL;
        *serialized_transaction_len = 0;
        return -1;
    }

    return 0;

error:
    fclose(stream);
    free(*serialized_transaction);
    *serialized_transaction = NULL;
    *serialized_transaction_len = 0;
    return -1;
}

/** 
 * Calculates a transaction ID without modifying the transaction.
 * @param transaction Pointer to the transaction.
 * @param txid_output Buffer for the hexadecimal transaction ID.
 * @return 0 on success, -1 on failure.
 */
static int get_transaction_id(const transaction_t *transaction, char txid_output[TXID_SIZE]) {
    char *serialized_transaction = NULL;
    size_t serialized_transaction_len = 0;
    uint8_t hash_data[HASH_SIZE];
    size_t bytes_written = 0;

    if (serialize_transaction_data(transaction, &serialized_transaction, &serialized_transaction_len) != 0) {
        return -1;
    }

    if (EVP_Q_digest(NULL, "SHA256", NULL, serialized_transaction, serialized_transaction_len, hash_data, &bytes_written) != 1 || bytes_written != HASH_SIZE) {
        free(serialized_transaction);
        return -1;
    }

    if (OPENSSL_buf2hexstr_ex(txid_output, TXID_SIZE, NULL, hash_data, HASH_SIZE, '\0') != 1) {
        free(serialized_transaction);
        return -1;
    }

    free(serialized_transaction);
    return 0;
}

/** 
 * Calculates and stores the ID of a transaction.
 * @param transaction Pointer to the transaction.
 * @return 0 on success, -1 on failure.
 */
int calculate_transaction_id(transaction_t *transaction) {
    if (transaction == NULL) {
        return -1;
    }

    return get_transaction_id(transaction, transaction->txid);
}

/** 
 * Checks whether a transaction contains its correct transaction ID.
 * @param transaction Pointer to the transaction.
 * @return 1 if the ID matches, 0 otherwise.
 */
int transaction_id_matches(const transaction_t *transaction) {
    if (transaction == NULL) {
        return 0;
    }

    char calculated_txid[TXID_SIZE];

    return get_transaction_id(transaction, calculated_txid) == 0 && strcmp(calculated_txid, transaction->txid) == 0;
}

/**
 * Decodes a hexadecimal transaction ID into its 32-byte digest.
 * @param txid Hexadecimal transaction ID.
 * @param digest Buffer receiving the digest.
 * @return 0 on success, -1 on failure.
 */
static int decode_transaction_id(const char *txid, uint8_t digest[HASH_SIZE]) {
    size_t digest_length = 0;

    if (txid == NULL || digest == NULL || strlen(txid) != TXID_SIZE - 1 ||
        OPENSSL_hexstr2buf_ex(digest, HASH_SIZE, &digest_length, txid, '\0') != 1 ||
        digest_length != HASH_SIZE) {
        return -1;
    }

    return 0;
}

/**
 * Creates an OpenSSL secp256k1 key object from a wallet.
 * @param wallet Wallet containing the private and public key.
 * @return Allocated key object, or NULL on failure.
 */
static EVP_PKEY *create_signing_key(const wallet_t *wallet) {
    EVP_PKEY_CTX *context = NULL;
    EVP_PKEY *key = NULL;
    OSSL_PARAM_BLD *builder = NULL;
    OSSL_PARAM *parameters = NULL;
    BIGNUM *private_key = NULL;

    if (wallet == NULL) {
        return NULL;
    }

    //create a new secp256k1 key context and parameter builder
    context = EVP_PKEY_CTX_new_from_name(NULL, "EC", NULL);
    builder = OSSL_PARAM_BLD_new();
    private_key = BN_bin2bn(wallet->private_key, PRIVATE_KEY_SIZE, NULL);

    //check for errors and build the key parameters
    if (context == NULL || builder == NULL || private_key == NULL ||
        OSSL_PARAM_BLD_push_utf8_string(
            builder,
            OSSL_PKEY_PARAM_GROUP_NAME,
            ELLYPTIC_CURVE_NAME,
            0
        ) != 1 ||
        OSSL_PARAM_BLD_push_BN(builder, OSSL_PKEY_PARAM_PRIV_KEY, private_key) != 1 ||
        OSSL_PARAM_BLD_push_octet_string(
            builder,
            OSSL_PKEY_PARAM_PUB_KEY,
            wallet->public_key,
            PUBLIC_KEY_SIZE
        ) != 1) {
        goto cleanup;
    }

    //convert the parameter builder to an OSSL_PARAM array and create the key
    parameters = OSSL_PARAM_BLD_to_param(builder);
    if (parameters == NULL || EVP_PKEY_fromdata_init(context) != 1 ||
        EVP_PKEY_fromdata(context, &key, EVP_PKEY_KEYPAIR, parameters) != 1) {
        EVP_PKEY_free(key);
        key = NULL;
    }

cleanup:
    BN_clear_free(private_key);
    OSSL_PARAM_free(parameters);
    OSSL_PARAM_BLD_free(builder);
    EVP_PKEY_CTX_free(context);
    return key;
}

/**
 * Creates an OpenSSL secp256k1 public key object.
 * @param public_key Compressed public key bytes.
 * @return Allocated key object, or NULL on failure.
 */
static EVP_PKEY *create_verification_key(const uint8_t public_key[PUBLIC_KEY_SIZE]) {
    EVP_PKEY_CTX *context = NULL;
    EVP_PKEY *key = NULL;
    OSSL_PARAM_BLD *builder = NULL;
    OSSL_PARAM *parameters = NULL;

    if (public_key == NULL) {
        return NULL;
    }

    context = EVP_PKEY_CTX_new_from_name(NULL, "EC", NULL);
    builder = OSSL_PARAM_BLD_new();

    //check for errors and build the key parameters
    if (context == NULL || builder == NULL ||
        OSSL_PARAM_BLD_push_utf8_string(
            builder,
            OSSL_PKEY_PARAM_GROUP_NAME,
            ELLYPTIC_CURVE_NAME,
            0
        ) != 1 ||
        OSSL_PARAM_BLD_push_octet_string(
            builder,
            OSSL_PKEY_PARAM_PUB_KEY,
            public_key,
            PUBLIC_KEY_SIZE
        ) != 1) {
        goto cleanup;
    }

    //convert the parameter builder to an OSSL_PARAM array and create the key
    parameters = OSSL_PARAM_BLD_to_param(builder);
    if (parameters == NULL || EVP_PKEY_fromdata_init(context) != 1 ||
        EVP_PKEY_fromdata(context, &key, EVP_PKEY_PUBLIC_KEY, parameters) != 1) {
        EVP_PKEY_free(key);
        key = NULL;
    }

cleanup:
    OSSL_PARAM_free(parameters);
    OSSL_PARAM_BLD_free(builder);
    EVP_PKEY_CTX_free(context);
    return key;
}

/**
 * Signs a transaction ID for every normal transaction input.
 * The signature and public key are excluded from the transaction ID.
 * @param transaction Transaction to sign.
 * @param wallet Wallet controlling every selected input.
 * @return 0 on success, -1 on failure.
 */
int sign_transaction(transaction_t *transaction, const wallet_t *wallet) {
    if (transaction == NULL || wallet == NULL || transaction->input_count == 0 ||
        transaction->inputs == NULL || is_coinbase_transaction(transaction) ||
        calculate_transaction_id(transaction) != 0) {
        return -1;
    }

    //decode the transaction ID into a 32-byte digestxx
    uint8_t digest[HASH_SIZE];
    EVP_PKEY *key = NULL;
    EVP_PKEY_CTX *context = NULL;
    int result = -1;

    if (decode_transaction_id(transaction->txid, digest) != 0) {
        return -1;
    }

    key = create_signing_key(wallet);
    if (key == NULL) {
        goto cleanup;
    }

    context = EVP_PKEY_CTX_new(key, NULL);
    if (context == NULL || EVP_PKEY_sign_init(context) != 1 ||
        EVP_PKEY_CTX_set_signature_md(context, EVP_sha256()) != 1) {
        goto cleanup;
    }

    for (size_t i = 0; i < transaction->input_count; i++) {
        size_t signature_length = MAX_SIGNATURE_SIZE;

        memset(transaction->inputs[i].signature, 0, MAX_SIGNATURE_SIZE);
        memcpy(
            transaction->inputs[i].public_key,
            wallet->public_key,
            PUBLIC_KEY_SIZE
        );

        if (EVP_PKEY_sign(
            context,
            transaction->inputs[i].signature,
            &signature_length,
            digest,
            sizeof(digest)
        ) != 1 || signature_length == 0 || signature_length > MAX_SIGNATURE_SIZE) {
            goto cleanup;
        }

        transaction->inputs[i].signature_length = signature_length;
    }

    result = 0;

cleanup:
    if (result != 0) {
        for (size_t i = 0; i < transaction->input_count; i++) {
            memset(transaction->inputs[i].signature, 0, MAX_SIGNATURE_SIZE);
            memset(transaction->inputs[i].public_key, 0, PUBLIC_KEY_SIZE);
            transaction->inputs[i].signature_length = 0;
        }
    }

    EVP_PKEY_CTX_free(context);
    EVP_PKEY_free(key);
    return result;
}

/**
 * Verifies one input's signature over the transaction ID.
 * @param transaction Transaction containing the input.
 * @param input_index Index of the input to verify.
 * @return 1 if valid, 0 otherwise.
 */
int verify_transaction_input_signature(const transaction_t *transaction, size_t input_index) {
    if (transaction == NULL || transaction->inputs == NULL ||
        input_index >= transaction->input_count ||
        transaction->inputs[input_index].signature_length == 0 ||
        transaction->inputs[input_index].signature_length > MAX_SIGNATURE_SIZE ||
        !transaction_id_matches(transaction)) {
        return 0;
    }

    uint8_t digest[HASH_SIZE];
    const tx_input_t *input = &transaction->inputs[input_index];
    EVP_PKEY *key = NULL;
    EVP_PKEY_CTX *context = NULL;
    int verified = 0;

    if (decode_transaction_id(transaction->txid, digest) != 0) {
        return 0;
    }

    key = create_verification_key(input->public_key);
    if (key == NULL) {
        return 0;
    }

    context = EVP_PKEY_CTX_new(key, NULL);

    if (context != NULL && EVP_PKEY_verify_init(context) == 1 &&
        EVP_PKEY_CTX_set_signature_md(context, EVP_sha256()) == 1 &&
        EVP_PKEY_verify(
            context,
            input->signature,
            input->signature_length,
            digest,
            sizeof(digest)
        ) == 1) {
        verified = 1;
    }

    EVP_PKEY_CTX_free(context);
    EVP_PKEY_free(key);
    return verified;
}

/**
 * Creates a deep copy of a transaction.
 * @param destination The transaction receiving the copy.
 * @param source The transaction being copied.
 * @return 0 on success, -1 on failure.
 */
int copy_transaction(transaction_t *destination, const transaction_t *source) {
    if (destination == NULL || source == NULL) {
        return -1;
    }

    if (destination == source) {
        return 0;
    }

    //check for invalid input and output counts or NULL pointers
    if ((source->input_count > 0 && source->inputs == NULL) ||
        (source->output_count > 0 && source->outputs == NULL) ||
        source->input_count > SIZE_MAX / sizeof(tx_input_t) ||
        source->output_count > SIZE_MAX / sizeof(tx_output_t)) {
        return -1;
    }

    //clear the destination transaction and copy the txid
    memset(destination, 0, sizeof(*destination));
    memcpy(destination->txid, source->txid, TXID_SIZE);
    destination->txid[TXID_SIZE - 1] = '\0';

    //copy the inputs and outputs if they exist
    if (source->input_count > 0) {
        destination->inputs = malloc(source->input_count * sizeof(tx_input_t));
        if (destination->inputs == NULL) {
            return -1;
        }

        memcpy(destination->inputs, source->inputs, source->input_count * sizeof(tx_input_t));
        destination->input_count = source->input_count;
    }

    //copy the outputs if they exist
    if (source->output_count > 0) {
        destination->outputs = malloc(source->output_count * sizeof(tx_output_t));
        if (destination->outputs == NULL) {
            free(destination->inputs);
            memset(destination, 0, sizeof(*destination));
            return -1;
        }

        memcpy(destination->outputs, source->outputs, source->output_count * sizeof(tx_output_t));
        destination->output_count = source->output_count;
    }

    return 0;
}

/**
 * Frees all dynamically allocated memory owned by a transaction.
 * @param transaction The transaction to free.
 */
void free_transaction(transaction_t *transaction) {
    if (transaction == NULL) {
        return;
    }

    free(transaction->inputs);
    free(transaction->outputs);
    memset(transaction, 0, sizeof(*transaction));
}

/**
 * Creates a coinbase transaction for a mined block.
 * @param transaction Pointer to the transaction to create.
 * @param block_height Height of the block containing the coinbase.
 * @param miner_address Address receiving the reward.
 * @param reward Block subsidy plus transaction fees.
 * @return 0 on success, -1 on failure.
 */
int create_coinbase_transaction(transaction_t *transaction, uint64_t block_height, const char *miner_address, uint64_t reward) {
    if (transaction == NULL || block_height == 0 || miner_address == NULL ||
        miner_address[0] == '\0' || strlen(miner_address) > ADDRESS_SIZE || reward == 0) {
        return -1;
    }

    memset(transaction, 0, sizeof(*transaction));

    //
    transaction->inputs = calloc(1, sizeof(tx_input_t));
    transaction->outputs = calloc(1, sizeof(tx_output_t));
    if (transaction->inputs == NULL || transaction->outputs == NULL) {
        free_transaction(transaction);
        return -1;
    }

    transaction->input_count = 1;
    transaction->output_count = 1;

    //set the coinbase input's outpoint to all zeros and include the block height
    memset(transaction->inputs[0].outpoint.prev_txid, '0', TXID_SIZE - 1);
    transaction->inputs[0].outpoint.prev_txid[TXID_SIZE - 1] = '\0';
    transaction->inputs[0].outpoint.output_index = 0;
    transaction->inputs[0].coinbase_height = block_height;

    transaction->outputs[0].amount = reward;
    strcpy(transaction->outputs[0].recipient_address, miner_address);

    if (calculate_transaction_id(transaction) != 0) {
        free_transaction(transaction);
        return -1;
    }

    return 0;
}

/**
 * Serializes one transaction input for network transmission.
 * @param stream Pointer to the output stream.
 * @param input Pointer to the input to serialize.
 * @return 0 on success, -1 on failure.
 */
static int serialize_input_for_network(FILE *stream, const tx_input_t *input) {
    if (stream == NULL || input == NULL || input->signature_length > MAX_SIGNATURE_SIZE) {
        return -1;
    }

    char signature_hex[(MAX_SIGNATURE_SIZE * 2) + 1];
    char public_key_hex[(PUBLIC_KEY_SIZE * 2) + 1];

    if (input->signature_length == 0) {
        signature_hex[0] = '\0';
    } else if (OPENSSL_buf2hexstr_ex(signature_hex, sizeof(signature_hex), NULL,
        input->signature, input->signature_length, '\0') != 1) {
        return -1;
    }

    if (OPENSSL_buf2hexstr_ex(public_key_hex, sizeof(public_key_hex), NULL,
        input->public_key, PUBLIC_KEY_SIZE, '\0') != 1) {
        return -1;
    }

    //write the input fields to the stream
    if (fprintf(stream,
        "prev_txid=%s\n"
        "output_index=%" PRIu32 "\n"
        "coinbase_height=%" PRIu64 "\n"
        "signature_length=%zu\n"
        "signature=%s\n"
        "public_key=%s\n",
        input->outpoint.prev_txid,
        input->outpoint.output_index,
        input->coinbase_height,
        input->signature_length,
        signature_hex,
        public_key_hex) < 0) {
        return -1;
    }

    return 0;
}

/**
 * Serializes one transaction output for network transmission.
 * @param stream Pointer to the output stream.
 * @param output Pointer to the output to serialize.
 * @return 0 on success, -1 on failure.
 */
static int serialize_output_for_network(FILE *stream, const tx_output_t *output) {
    if (stream == NULL || output == NULL) {
        return -1;
    }

    //write the output fields to the stream
    if (fprintf(stream,
        "amount=%" PRIu64 "\n"
        "recipient_address=%s\n",
        output->amount,
        output->recipient_address) < 0) {
        return -1;
    }

    return 0;
}

/**
 * Serializes one complete transaction for network transmission.
 * @param stream Pointer to the output stream.
 * @param transaction Pointer to the transaction to serialize.
 * @return 0 on success, -1 on failure.
 */
static int write_transaction_for_network(FILE *stream, const transaction_t *transaction) {
    if (stream == NULL || transaction == NULL ||
        (transaction->input_count > 0 && transaction->inputs == NULL) ||
        (transaction->output_count > 0 && transaction->outputs == NULL)) {
        return -1;
    }

    //write the transaction ID and input count to the stream
    if (fprintf(stream,
        "txid=%s\n"
        "input_count=%zu\n",
        transaction->txid,
        transaction->input_count) < 0) {
        return -1;
    }

    //serialize each input for network transmission
    for (size_t i = 0; i < transaction->input_count; i++) {
        if (serialize_input_for_network(stream, &transaction->inputs[i]) != 0) {
            return -1;
        }
    }

    if (fprintf(stream, "output_count=%zu\n", transaction->output_count) < 0) {
        return -1;
    }

    for (size_t i = 0; i < transaction->output_count; i++) {
        if (serialize_output_for_network(stream, &transaction->outputs[i]) != 0) {
            return -1;
        }
    }

    return 0;
}

/**
 * Serializes one complete transaction for network transmission.
 * @param transaction Pointer to the transaction to serialize.
 * @param output Pointer to the output buffer.
 * @param output_length Pointer to the output buffer length.
 * @return 0 on success, -1 on failure.
 */
int serialize_transaction_for_network(const transaction_t *transaction, char **output, size_t *output_length) {
    if (transaction == NULL || output == NULL || output_length == NULL) {
        return -1;
    }

    *output = NULL;
    *output_length = 0;

    //open a memory stream to write the serialized transaction
    FILE *stream = open_memstream(output, output_length);
    if (stream == NULL) {
        return -1;
    }

    //write the transaction to the stream
    if (write_transaction_for_network(stream, transaction) != 0) {
        fclose(stream);
        free(*output);
        *output = NULL;
        *output_length = 0;
        return -1;
    }

    //close the stream and check for errors
    if (fclose(stream) != 0) {
        free(*output);
        *output = NULL;
        *output_length = 0;
        return -1;
    }

    return 0;
}

/**
 * Reads one named field from a serialized network message.
 * @param cursor Pointer to the current parse position.
 * @param field_name Name of the field to read.
 * @param value Buffer receiving the field value.
 * @param value_size Size of the value buffer.
 * @return 0 on success, -1 on failure.
 */
static int read_network_field(char **cursor, const char *field_name, char *value, size_t value_size) {
    if (cursor == NULL || *cursor == NULL || field_name == NULL || value == NULL || value_size == 0) {
        return -1;
    }

    char *line_start = *cursor;
    size_t field_length = strlen(field_name);

    //check if the line starts with the expected field name and an equals sign
    if (strncmp(line_start, field_name, field_length) != 0 || line_start[field_length] != '=') {
        return -1;
    }

    //move the cursor to the start of the value and find the end of the line
    char *value_start = line_start + field_length + 1;
    char *line_end = strchr(value_start, '\n');
    if (line_end == NULL) {
        line_end = value_start + strlen(value_start);
    }

    size_t length = (size_t)(line_end - value_start);
    if (length >= value_size) {
        return -1;
    }

    memcpy(value, value_start, length);
    value[length] = '\0';

    *cursor = (*line_end == '\n') ? line_end + 1 : line_end;
    return 0;
}

/**
 * Reads one uint64 field from a network message.
 * @param cursor Pointer to the current parse position.
 * @param field_name Name of the field to read.
 * @param value Pointer to the uint64_t variable receiving the value.
 * @return 0 on success, -1 on failure.
 */
static int read_uint64_field(char **cursor, const char *field_name, uint64_t *value) {
    char text[32];
    char *end = NULL;

    if (value == NULL || read_network_field(cursor, field_name, text, sizeof(text)) != 0 || text[0] == '-') {
        return -1;
    }

    //parse the string as an unsigned long long and check for errors
    errno = 0;
    unsigned long long parsed = strtoull(text, &end, 10);
    if (errno != 0 || end == text || *end != '\0') {
        return -1;
    }

    *value = (uint64_t)parsed;
    return 0;
}

/**
 * Reads one uint32 field from a network message.
 * @param cursor Pointer to the current parse position.
 * @param field_name Name of the field to read.
 * @param value Pointer to the uint32_t variable receiving the value.
 * @return 0 on success, -1 on failure.
 */
static int read_uint32_field(char **cursor, const char *field_name, uint32_t *value) {
    uint64_t parsed = 0;

    if (value == NULL || read_uint64_field(cursor, field_name, &parsed) != 0 || parsed > UINT32_MAX) {
        return -1;
    }

    *value = (uint32_t)parsed;
    return 0;
}

/**
 * Reads one size_t field from a network message.
 * @param cursor Pointer to the current parse position.
 * @param field_name Name of the field to read.
 * @param value Pointer to the size_t variable receiving the value.
 * @return 0 on success, -1 on failure.
 */
static int read_size_field(char **cursor, const char *field_name, size_t *value) {
    uint64_t parsed = 0;

    if (value == NULL || read_uint64_field(cursor, field_name, &parsed) != 0 || parsed > SIZE_MAX) {
        return -1;
    }

    *value = (size_t)parsed;
    return 0;
}

/**
 * Deserializes one transaction input from a network message.
 * @param cursor Pointer to the current parse position.
 * @param input Pointer to the input to populate.
 * @return 0 on success, -1 on failure.
 */
static int deserialize_input_from_network(char **cursor, tx_input_t *input) {
    if (cursor == NULL || input == NULL) {
        return -1;
    }

    memset(input, 0, sizeof(*input));

    char signature_hex[(MAX_SIGNATURE_SIZE * 2) + 1];
    char public_key_hex[(PUBLIC_KEY_SIZE * 2) + 1];
    size_t signature_bytes = 0;
    size_t public_key_bytes = 0;

    //read the input fields from the network message and validate them
    if (read_network_field(cursor, "prev_txid", input->outpoint.prev_txid, sizeof(input->outpoint.prev_txid)) != 0 ||
        strlen(input->outpoint.prev_txid) != TXID_SIZE - 1 ||
        read_uint32_field(cursor, "output_index", &input->outpoint.output_index) != 0 ||
        read_uint64_field(cursor, "coinbase_height", &input->coinbase_height) != 0 ||
        read_size_field(cursor, "signature_length", &input->signature_length) != 0 ||
        input->signature_length > MAX_SIGNATURE_SIZE ||
        read_network_field(cursor, "signature", signature_hex, sizeof(signature_hex)) != 0 ||
        read_network_field(cursor, "public_key", public_key_hex, sizeof(public_key_hex)) != 0 ||
        strlen(signature_hex) != input->signature_length * 2 ||
        strlen(public_key_hex) != PUBLIC_KEY_SIZE * 2) {
        return -1;
    }

    //convert the hexadecimal signature and public key to binary
    if (input->signature_length > 0) {
        if (OPENSSL_hexstr2buf_ex(input->signature, sizeof(input->signature), &signature_bytes,
            signature_hex, '\0') != 1 || signature_bytes != input->signature_length) {
            return -1;
        }
    }

    //convert the hexadecimal public key to binary
    if (OPENSSL_hexstr2buf_ex(input->public_key, sizeof(input->public_key), &public_key_bytes,
        public_key_hex, '\0') != 1 || public_key_bytes != PUBLIC_KEY_SIZE) {
        return -1;
    }

    return 0;
}

/**
 * Deserializes one transaction output from a network message.
 * @param cursor Pointer to the current parse position.
 * @param output Pointer to the output to populate.
 * @return 0 on success, -1 on failure.
 */
static int deserialize_output_from_network(char **cursor, tx_output_t *output) {
    if (cursor == NULL || output == NULL) {
        return -1;
    }

    memset(output, 0, sizeof(*output));

    if (read_uint64_field(cursor, "amount", &output->amount) != 0 ||
        read_network_field(cursor, "recipient_address", output->recipient_address, sizeof(output->recipient_address)) != 0) {
        return -1;
    }

    return 0;
}

/**
 * Deserializes one complete transaction from a network message.
 * @param cursor Pointer to the current parse position.
 * @param transaction Pointer to the transaction to populate.
 * @return 0 on success, -1 on failure. 
 */
static int read_transaction_from_network(char **cursor, transaction_t *transaction) {
    if (cursor == NULL || transaction == NULL) {
        return -1;
    }

    memset(transaction, 0, sizeof(*transaction));

    char transmitted_txid[TXID_SIZE];
    size_t expected_input_count = 0;
    size_t expected_output_count = 0;

    //read the transaction ID and input count from the network message and validate them
    if (read_network_field(cursor, "txid", transmitted_txid, sizeof(transmitted_txid)) != 0 ||
        strlen(transmitted_txid) != TXID_SIZE - 1 ||
        read_size_field(cursor, "input_count", &expected_input_count) != 0 ||
        expected_input_count > SIZE_MAX / sizeof(tx_input_t)) {
        return -1;
    }

    //allocate memory for the inputs 
    if (expected_input_count > 0) {
        transaction->inputs = calloc(expected_input_count, sizeof(tx_input_t));
        if (transaction->inputs == NULL) {
            return -1;
        }
    }

    //deserialize each input from the network message
    for (size_t i = 0; i < expected_input_count; i++) {
        if (deserialize_input_from_network(cursor, &transaction->inputs[i]) != 0) {
            free_transaction(transaction);
            return -1;
        }
        transaction->input_count++;
    }

    //if the transaction is a coinbase, ensure it has exactly one input and that the outpoint is zeroed
    if (read_size_field(cursor, "output_count", &expected_output_count) != 0 ||
        expected_output_count > SIZE_MAX / sizeof(tx_output_t)) {
        free_transaction(transaction);
        return -1;
    }

    //allocate memory for the outputs if there are any
    if (expected_output_count > 0) {
        transaction->outputs = calloc(expected_output_count, sizeof(tx_output_t));
        if (transaction->outputs == NULL) {
            free_transaction(transaction);
            return -1;
        }
    }

    //deserialize each output from the network message
    for (size_t i = 0; i < expected_output_count; i++) {
        if (deserialize_output_from_network(cursor, &transaction->outputs[i]) != 0) {
            free_transaction(transaction);
            return -1;
        }
        transaction->output_count++;
    }

    // calculate the transaction ID and compare it to the transmitted value
    char calculated_txid[TXID_SIZE];
    if (get_transaction_id(transaction, calculated_txid) != 0 ||
        strcmp(transmitted_txid, calculated_txid) != 0) {
        free_transaction(transaction);
        return -1;
    }

    strcpy(transaction->txid, transmitted_txid);
    return 0;
}

/**
 * Deserializes one transaction from network transmission.
 * @param input Pointer to the serialized transaction.
 * @param input_length Length of the serialized transaction.
 * @param transaction Pointer to the transaction to populate.
 * @return 0 on success, -1 on failure.
 */
int deserialize_transaction_from_network(const char *input, size_t input_length, transaction_t *transaction) {
    if (input == NULL || transaction == NULL) {
        return -1;
    }

    char *buffer = malloc(input_length + 1);
    if (buffer == NULL) {
        return -1;
    }

    memcpy(buffer, input, input_length);
    buffer[input_length] = '\0';

    //parse the transaction from the buffer and check for errors
    char *cursor = buffer;
    if (read_transaction_from_network(&cursor, transaction) != 0 || *cursor != '\0') {
        free(buffer);
        free_transaction(transaction);
        return -1;
    }

    free(buffer);
    return 0;
}
