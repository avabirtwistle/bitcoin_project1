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
    
    /**
     * Convert a public key to a Bitcoin address.
     * @param public_key The public key to convert.
     * @param address The buffer to store the resulting address (must be at least 36 bytes).
     * @return 0 on success, -1 on failure.
     */
    int public_key_to_address(const uint8_t public_key[PUBLIC_KEY_SIZE], char address[ADDRESS_SIZE + 1]) {
        uint8_t public_key_hash[PUBLIC_KEY_HASH_SIZE];
        uint8_t payload[PAYLOAD_SIZE];
        uint8_t first_checksum_hash[HASH_SIZE];
        uint8_t second_checksum_hash[HASH_SIZE];
        size_t bytes_written = 0;

        // Calculate RIPEMD-160(SHA-256(public key)).
        if (hash_public_key(public_key, public_key_hash) != 0) {
            return -1;
        }

        //[version byte][public-key hash]
        payload[0] = 0x00;
        memcpy(payload + 1, public_key_hash, PUBLIC_KEY_HASH_SIZE);

        // first SHA-256 hash of the 21 versioned bytes
        if (EVP_Q_digest(NULL, "SHA256", NULL, payload, VERSIONED_HASH_SIZE, first_checksum_hash, &bytes_written) != 1 || bytes_written != HASH_SIZE) {
            return -1;
        }

        // hash first result again
        if (EVP_Q_digest(NULL, "SHA256", NULL, first_checksum_hash, HASH_SIZE, second_checksum_hash, &bytes_written) != 1 || bytes_written != HASH_SIZE) {
            return -1;
        }

        // append the first four bytes of the second hash
        memcpy(payload + VERSIONED_HASH_SIZE, second_checksum_hash, CHECKSUM_SIZE);

        // convert the completed 25-byte payload to Base58
        if (base58_encode(payload, address) != 0) {
            return -1;
        }

        return 0;
    }

    /* 
     * Check if the given address matches the given prefix.
     * @param address The address to check.
     * @param prefix The prefix to match.
     * @return 1 if the address matches the prefix, 0 otherwise.
     */
    int address_matches_prefix(const char *address, const char *prefix) {
        for (int i = 0; i < 3; i++) {
            char address_character = toupper((unsigned char)address[i + 1]);
            char prefix_character = toupper((unsigned char)prefix[i]);

            if (address_character != prefix_character) {
                return 0;
            }
        }

        return 1;
    }

    /**
     * Generate a new wallet with a public key and address that matches the given prefix.
     * @param wallet The wallet to generate.
     * @param prefix The prefix to match.
     * @return 0 on success, -1 on failure.
     */
    int generate_vanity_wallet(wallet_t *wallet, const char *prefix) {
        do {
            //make new keypair
            if (generate_keypair(wallet) != 0) {
                return -1;
            }

            //create address for keypair
            if (public_key_to_address(wallet->public_key, wallet->address) != 0) {
                return -1;
            }
        } 
        // keep generating until we get a match
        while(address_matches_prefix(wallet->address, prefix) == 0);

        return 0;
    }

    /**
     * saves a new wallet with a public key and address to the file. file must be close externally
     * @param file - pointer to the file to write to 
     * @param wallet - pointer to the wallet containing the new info
     * @return 0 on success, -1 on failure.
     */
    int save_wallet(FILE *file, const wallet_t *wallet){
        char private_key_hex[(PRIVATE_KEY_SIZE << 1) + 1]; 
        char public_key_hex[(PUBLIC_KEY_SIZE << 1) + 1];

        //use the openssl inverse function to convertthe priv and public keys to hex strings
        if(OPENSSL_buf2hexstr_ex(private_key_hex, sizeof(private_key_hex), NULL, wallet->private_key, PRIVATE_KEY_SIZE, '\0') != 1) {
            fprintf(stderr, "Failed to convert private key to hex.\n");
            return -1;
        }

        //convert 33 bytes into 55 hex character for public key
        if(OPENSSL_buf2hexstr_ex(public_key_hex, sizeof(public_key_hex), NULL, wallet-> public_key, PUBLIC_KEY_SIZE, '\0')!= 1){
            return -1;
        }

        //write the public and private key to the JSON file using the right format
        if (fprintf(file, "{\n"
                            "  \"private_key\": \"%s\",\n"
                            "  \"public_key\": \"%s\",\n"
                            "  \"address\": \"%s\"\n"
                            "}\n",
                            private_key_hex,
                            public_key_hex,
                            wallet->address) < 0) {
                return -1;
            }
        return 0;
    }