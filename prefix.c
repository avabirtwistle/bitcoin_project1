#include "prefix.h"

//helpers 
static int hash_public_key(const uint8_t public_key[PUBLIC_KEY_SIZE], uint8_t public_key_hash[PUBLIC_KEY_HASH_SIZE]);
static int base58_encode(const uint8_t *input, char *output);
    /**
     * Finds a wallet in the specified file.
     * @param filename The name of the file to search.
     * @param keypair A pointer to the wallet structure to populate.
     * @return 0 if successful, -1 if error or if cant be found.
     */
    int find_wallet(FILE *file, wallet_t *keypair) {
        char buffer[JSON_BUFFER_SIZE];

        // Read the entire file into the buffer
        size_t n = fread(buffer, 1, sizeof(buffer) - 1, file);

        // Check for read errors or if the file is too large
        if (ferror(file) || (n == sizeof(buffer) - 1 && fgetc(file) != EOF)) {
            fclose(file);
            return -1;
        }

        // Null-terminate the buffer to ensure it's a valid string
        buffer[n] = '\0';

        // Parse the JSON data from the buffer
        cJSON *root = cJSON_Parse(buffer);
        if (root == NULL) {
            return -1;
        }

        //extracxt the objects from the json file
        cJSON *private_key = cJSON_GetObjectItemCaseSensitive(root, "private_key");
        cJSON *public_key = cJSON_GetObjectItemCaseSensitive(root, "public_key");
        cJSON *address = cJSON_GetObjectItemCaseSensitive(root, "address");

        // Validate that the extracted items are strings
        if (!cJSON_IsString(private_key) || !cJSON_IsString(public_key) || !cJSON_IsString(address)) { 
            cJSON_Delete(root);
            return -1;
        }

        // Convert hex strings to byte arrays and copy the address string
        OPENSSL_hexstr2buf_ex(keypair->private_key, PRIVATE_KEY_SIZE, NULL,private_key->valuestring, '\0');;
        OPENSSL_hexstr2buf_ex(keypair->public_key, PUBLIC_KEY_SIZE, NULL, public_key->valuestring, '\0');
        strncpy(keypair->address, address->valuestring, ADDRESS_SIZE);

        cJSON_Delete(root);
        return 0; // Success
    }

    int generate_keypair(wallet_t *wallet) {
        EVP_PKEY *key_pair = NULL;
        BIGNUM *private_key_integer = NULL; //priate key as integer
        size_t actual_public_key_len = 0; // number of bytes written 
        int result = -1; // Default to failure

        //generate private and public keypair
        key_pair = EVP_EC_gen(ELLYPTIC_CURVE_NAME);
        if(key_pair == NULL) { //return null if fails
            fprintf(stderr, "Failed to generate EC key pair.\n");
            goto cleanup;
        }

        //try to compress
        if (EVP_PKEY_set_utf8_string_param(key_pair, OSSL_PKEY_PARAM_EC_POINT_CONVERSION_FORMAT, "compressed") != 1) {
            fprintf(stderr, "Failed to set EC point conversion format.\n");
            goto cleanup;
        }
        
        // extract the private key number
        if (EVP_PKEY_get_bn_param(key_pair, OSSL_PKEY_PARAM_PRIV_KEY, &private_key_integer) != 1) {
            fprintf(stderr, "Failed to get private key as BIGNUM.\n");
           goto cleanup;
        }

        if (BN_bn2binpad(private_key_integer, wallet->private_key, PRIVATE_KEY_SIZE) != PRIVATE_KEY_SIZE) {
            fprintf(stderr, "Failed to convert private key to binary.\n");
            goto cleanup;
        }

        if (EVP_PKEY_get_octet_string_param(key_pair, OSSL_PKEY_PARAM_PUB_KEY, wallet->public_key, PUBLIC_KEY_SIZE, &actual_public_key_len) != 1) {
            goto cleanup;
        }

        if (actual_public_key_len != PUBLIC_KEY_SIZE) {
            goto cleanup;
        }

        result = 0;

        cleanup:
            //erase the private key then release its memory
            //can call when private key int is null
            BN_clear_free(private_key_integer);

            //release openssl keypair object
            EVP_PKEY_free(key_pair);
            return result;
    }

    int hash_public_key(const uint8_t public_key[PUBLIC_KEY_SIZE], uint8_t public_key_hash[PUBLIC_KEY_HASH_SIZE]) {
        uint8_t sha256_hash[HASH_SIZE]; //where to put hash result
        size_t bytes_written = 0;

        //hash the public key using SHA-256, return 1 for success 0 for error
        if(EVP_Q_digest(NULL, "SHA256", NULL, public_key, PUBLIC_KEY_SIZE, sha256_hash, &bytes_written) != 1 || bytes_written != HASH_SIZE) {
            return -1; // Error during SHA-256 hashing
        }

        //check the output length
        if(bytes_written != sizeof(sha256_hash)) {
            return -1; // Unexpected output length
        }

        //hash the result of the SHA-256 hash using RIPEMD-160
        if(EVP_Q_digest(NULL, "RIPEMD160", NULL, sha256_hash, sizeof(sha256_hash), public_key_hash, &bytes_written) != 1 || bytes_written != PUBLIC_KEY_HASH_SIZE) {
            return -1; // Error during RIPEMD-160 hashing
        }

        return 0; // Success
    }
    

    int base58_encode(const uint8_t *input, char *output) {
        // Base58 alphabet
        const char alphabet[] = "123456789ABCDEFGHJKLMNPQRSTUVWXYZabcdefghijkmnopqrstuvwxyz";
        size_t leading_zeroes = 0;
        size_t position = ADDRESS_SIZE;

        //convert the input to a BIGNUM
        BIGNUM *number = BN_bin2bn(input, PAYLOAD_SIZE, NULL);
        if (number == NULL) {
            return -1;
        }

        //count the leading 0 
        while (leading_zeroes < PAYLOAD_SIZE && input[leading_zeroes] == 0x00) {
            leading_zeroes++;
        }

        // put new line at end
        output[position] = '\0';
        // repeated division by 58 to convert to base58, stops when we reach 0's
        while (!BN_is_zero(number)) { // repeated division by 58 
            BN_ULONG remainder = BN_div_word(number, 58);
            if (remainder == (BN_ULONG)-1) {
                BN_free(number);
                return -1;
            }

            position--;//right to left
            output[position] = alphabet[remainder]; // map remainder to Base58 character
        }

        // start at the index of the first non-zero character and add leading '1's for each leading zero byte in the input
        while (leading_zeroes > 0) {
            position--;
            output[position] = '1';
            leading_zeroes--;
        }

        //complete address is put at begining
        memmove(output, output + position, ADDRESS_SIZE - position + 1);
        BN_free(number);
            return 0;
    }

    
    int public_key_to_address(const uint8_t public_key[PUBLIC_KEY_SIZE], char address[ADDRESS_SIZE + 1]) {
        uint8_t public_key_hash[PUBLIC_KEY_HASH_SIZE];
        uint8_t versioned_hash[VERSIONED_HASH_SIZE];
        uint8_t checksum[CHECKSUM_SIZE];

        // Hash the public key to get the public key hash
        if (hash_public_key(public_key, public_key_hash) != 0) {
            return -1; // Error during hashing
        }

        // Prepend version byte (0x00 for Bitcoin mainnet)
        versioned_hash[0] = 0x00;
        memcpy(versioned_hash + 1, public_key_hash, PUBLIC_KEY_HASH_SIZE);

        // Calculate checksum (first 4 bytes of double SHA-256)
        if (EVP_Q_digest(NULL, "SHA256", NULL, versioned_hash, VERSIONED_HASH_SIZE, checksum, NULL) != 1) {
            return -1; // Error during first SHA-256 hashing
        }
        if (EVP_Q_digest(NULL, "SHA256", NULL, checksum, HASH_SIZE, checksum, NULL) != 1) {
            return -1; // Error during second SHA-256 hashing
        }

        // Append the first 4 bytes of the checksum to the versioned hash
        memcpy(versioned_hash + VERSIONED_HASH_SIZE, checksum, CHECKSUM_SIZE);

        // Encode the result in Base58
        if (base58_encode(versioned_hash, address) != 0) {
            return -1; // Error during Base58 encoding
        }

        return 0; // Success

    }
