#ifndef PREFIX_H
#define PREFIX_H

#include <ctype.h>
#include <cjson/cJSON.h>
#include <stdio.h>
#include <stdlib.h>
#include <openssl/crypto.h>
#include "string.h"
#include <openssl/bn.h>
#include <openssl/core_names.h>
#include <openssl/ec.h>
#include <openssl/evp.h>
//definitions for sizes 
#define PRIVATE_KEY_SIZE 32
#define HASH_SIZE 32
#define PUBLIC_KEY_SIZE 33
#define PUBLIC_KEY_HASH_SIZE 20
#define VERSIONED_HASH_SIZE 21
#define CHECKSUM_SIZE 4
#define PAYLOAD_SIZE 25
#define ADDRESS_SIZE 35
#define JSON_BUFFER_SIZE 4096
#define ELLYPTIC_CURVE_NAME "secp256k1"

typedef struct{
    uint8_t private_key[PRIVATE_KEY_SIZE];
    uint8_t public_key[PUBLIC_KEY_SIZE];
    char address[ADDRESS_SIZE + 1]; // +1 for null terminator
} wallet_t;

int find_wallet(FILE *file,  wallet_t *keypair);
int save_wallet(FILE *file, const wallet_t *wallet);
int generate_keypair(wallet_t *wallet);
int public_key_to_address(const uint8_t public_key[33], char address[36]);
int address_matches_prefix(const char *address, const char *prefix);
int generate_vanity_wallet(wallet_t *wallet, const char *prefix);

# endif