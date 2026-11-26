# ECE 496C / 519C Assignment 2

Simple UTXO currency, mempool management, and optional Merkle-root block hashing.

## Setup

Install the required libraries on macOS:

```sh
brew install cjson openssl@3 readline
```

Build the program from the project directory:

```sh
make
```

Each node loads its own wallet from its config file:

| Node  | Config        | Wallet file      |
|-------|---------------|------------------|
| Node1 | `node1.json`  | `keypair.json`   |
| Node2 | `node2.json`  | `keypair2.json`  |
| Node3 | `node3.json`  | `keypair3.json`  |

`keypair.json` is the Assignment 1 vanity address (prefix `AVA`). Node2 and Node3 use separate keypairs (vanity prefix not required).

## Run the three nodes

Open three terminals in the project directory:

```sh
./blockchain --config node1.json --vanity-prefix AVA --difficulty 4
./blockchain --config node2.json --vanity-prefix AVA --difficulty 4
./blockchain --config node3.json --vanity-prefix AVA --difficulty 4
```

The chain starts with no coins. All coins come from coinbase rewards, so mine a block before sending.

## CLI commands

```text
mine
send <recipient_address> <amount> --fee <fee>
balance
utxos
mempool
exit
```

- `mine` — builds a block from the local mempool, prepends a coinbase (50 + fees), finds a PoW nonce, appends the block, updates the UTXO set, clears confirmed mempool txs, and broadcasts the block.
- `send <address> <amount> --fee <fee>` — selects confirmed unreserved UTXOs, creates a payment with a change output when needed, signs it, validates it, adds it to the mempool, and broadcasts it.
- `balance` — prints this wallet’s confirmed balance.
- `utxos` — lists this wallet’s confirmed UTXOs.
- `mempool` — lists pending unconfirmed transactions.
- `exit` — stops the node.

## Transaction ID serialization

`txid` is the hex-encoded SHA-256 of the transaction serialized **without** scriptSig fields (no signature, no public key).

Exact field order:

```text
input_count=<n>\n
prev_txid=<64 hex>\n
output_index=<u32>\n
coinbase_height=<u64>\n          # coinbase inputs only
...                              # repeated for each input
output_count=<n>\n
amount=<u64>\n
recipient_address=<address>\n
...                              # repeated for each output
```

Corresponding C formatting:

```c
"input_count=%zu\n"
"prev_txid=%s\n"
"output_index=%" PRIu32 "\n"
"coinbase_height=%" PRIu64 "\n"   /* coinbase only */
"output_count=%zu\n"
"amount=%" PRIu64 "\n"
"recipient_address=%s\n"
```

Fee is not a field.  
`Fee = sum(input amounts) - sum(output amounts)`, and nodes require `Fee >= 0`.

## Block hash serialization

The `hash` field is **not** included in the hash input. Fields are serialized in this order:

1. `index`
2. `timestamp`
3. `prev_hash`
4. `merkle_root` (replaces the old txid list)
5. `nonce`
6. `miner_address`

Exact format:

```text
index=<uint64>\n
timestamp=<timestamp>\n
prev_hash=<64-character hash>\n
merkle_root=<64-character hash>\n
nonce=<uint64>\n
miner_address=<address>
```

Corresponding C formatting:

```c
"index=%" PRIu64 "\n"
"timestamp=%s\n"
"prev_hash=%s\n"
"merkle_root=%s\n"
"nonce=%" PRIu64 "\n"
"miner_address=%s"
```

SHA-256 is calculated over those exact serialized bytes. The resulting hex digest is stored as the block `hash`.

## Merkle root (bonus)

Every mined block stores a `merkle_root` in the header. PoW hashes that root instead of listing every txid.

How it is computed:

- Leaves are the raw 32-byte digests decoded from each transaction’s hex `txid`, in block order.
- Parents are `SHA-256(left || right)`.
- If a level has an odd number of hashes, the last hash is duplicated.
- An empty transaction list (genesis) uses a 64-character all-zero root.

On receive, a peer recomputes the root from the block’s transactions and rejects the block if it does not match the header `merkle_root`.

## Network: broadcast and receive

Nodes talk over raw TCP using typed, length-prefixed messages.

### Transactions

1. `send` creates and signs a transaction.
2. The node validates it with the four mempool checks, then adds it locally.
3. The transaction is broadcast to configured peers.
4. A peer deserializes it, runs the same checks against its own UTXO set and mempool, then either accepts it into its mempool or rejects it and logs the reason (without crashing).

### Blocks

1. `mine` builds the block (coinbase first), computes the Merkle root, finds a valid nonce, appends the block, updates UTXOs, and removes confirmed txs from the mempool.
2. The full block is broadcast to peers.
3. A peer validates:
   - header / previous hash / index / PoW
   - Merkle root matches included transactions
   - first tx is the only coinbase; coinbase reward ≤ 50 + fees
   - every other tx passes Checks 1–4, with no double-spent outpoint in the block
4. On accept: append block, update UTXO set, remove those txs from the mempool.
5. On reject: log the reason and keep running.

If a peer is offline, the sender prints a warning and continues.

Consensus remains **First-Valid-Block-Wins**. Competing same-height blocks are not reconciled; there is no most-work reorg.

## Validation summary

Before accepting a normal transaction into the mempool or a block:

1. **Existence** — each input outpoint exists in the confirmed UTXO set (not mempool-only).
2. **Unspent** — not already spent in the UTXO set or reserved by another mempool tx; no duplicate inputs in the same tx.
3. **Authorization** — public key hashes to the locked address; ECDSA signature over the txid is valid.
4. **Conservation** — `sum(inputs) >= sum(outputs)`; all output amounts are positive.

Coinbase transactions are exempt from Checks 1–4. They use a zeroed outpoint, include the block height for uniqueness, and pay `50 + fees` to the miner address.

## Example happy path

```text
Node1> mine
Node1> balance

Node1> send <Node2_address> 20 --fee 1
Node2> mempool
Node3> mempool

Node1> mine
Node1> balance
Node1> utxos
Node2> balance
Node2> utxos
```

Then attempt a double spend of the same UTXO and a transaction with an invalid signature; both should be rejected with a logged reason.
