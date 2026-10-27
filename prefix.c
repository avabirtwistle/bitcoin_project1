#include "prefix.h"

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

    

