# ECE 496C / 519C Assignment 1

This project implements a Bitcoin Legacy vanity address generator, an in-memory proof-of-work blockchain and a three-node raw TCP network.

## Setup

Install the required libraries on macOS:

```sh
brew install cjson openssl@3 readline
```

Build the program:

```sh
make
```

The saved `keypair.json` contains the pre-computed vanity address and is shared by all three local nodes.

## Run the three nodes

Open three terminals in the project directory and run one command in each terminal.

Terminal 1:

```sh
./blockchain --config node1.json --vanity-prefix AVA --difficulty 4
```

Terminal 2:

```sh
./blockchain --config node2.json --vanity-prefix AVA --difficulty 4
```

Terminal 3:

```sh
./blockchain --config node3.json --vanity-prefix AVA --difficulty 4
```

Available commands:

```text
msg <text>  Add a local pending message
mine        Mine one block and broadcast it to both peers
exit        Stop the node
```

If a configured peer is offline, the mining node prints a warning and continues running.

## Block hashing format

The `hash` field is not included in the hash input. The fields are serialized in this exact order:

1. `index`
2. `timestamp`
3. `prev_hash`
4. `data_count`
5. Each string in `data`, in its original order
6. `nonce`
7. `miner_address`

The exact formatting string is:

```text
index=<uint64>\n
timestamp=<timestamp>\n
prev_hash=<64-character hash>\n
data_count=<number of messages>\n
data=<message byte length>:<message>\n    repeated once for each message
nonce=<uint64>\n
miner_address=<address>
```

The corresponding C formatting is:

```c
"index=%" PRIu64 "\n"
"timestamp=%s\n"
"prev_hash=%s\n"
"data_count=%zu\n"

"data=%zu:%s\n"       /* repeated for every message */

"nonce=%" PRIu64 "\n"
"miner_address=%s"
```

SHA-256 is calculated over the exact serialized bytes. The resulting hexadecimal hash is stored in the block after hashing.

## Received block validation

A received block is appended only when all four checks pass:

1. Its payload is hashed again and the result equals its transmitted `hash`.
2. The recalculated hash has the required number of leading zeroes.
3. Its `prev_hash` equals the current local tip hash.
4. Its `index` equals the current local tip index plus one.

Received blocks are not forwarded because every node is directly connected to the other two nodes.

## Fork demonstration

Restart all three nodes with difficulty 6:

```sh
./blockchain --config node1.json --vanity-prefix AVA --difficulty 6
./blockchain --config node2.json --vanity-prefix AVA --difficulty 6
./blockchain --config node3.json --vanity-prefix AVA --difficulty 6
```

First add different messages so the two candidate blocks cannot be identical:

```text
Node2> msg fork block from Node2
Node3> msg fork block from Node3
```

Then enter `mine` on Node2 and Node3 at approximately the same time. Each node mines and appends its own block before processing the competing block. Each then rejects the other block because it conflicts at the same height. This demonstrates the required First-Valid-Block-Wins fork limitation. No most-work fork resolution is implemented.
