# AI Usage Log

Generative AI was used for targeted debugging, networking assistance and a final requirements review. The existing vanity address, blockchain, mining and message code was written before this assistance and was not generated as a complete project by AI.

Tool used: OpenAI Codex

Date used: October 2026

## Usage summary

| Prompt or request | AI output used | How it was applied |
|---|---|---|
| “Look through what I have written” and questions about serialization, deserialization and validation. | Reviewed the existing block functions and identified that the received hash needed to be transmitted and preserved for independent verification. | The existing deterministic serializer was retained. The network representation was changed to include the claimed hash, while the hash calculation continued to exclude it. |
| “Fix this file” for the partially written networking file, while keeping the same style and avoiding unnecessary features. | Helped complete the missing TCP listener, block broadcast, offline-peer warning and shutdown functions around the existing configuration and send/receive code. | Direct TCP connections were used. Received blocks are validated and appended locally but are not forwarded. |
| “Change main.” | Helped connect the completed networking functions to the existing command loop. | Added network startup, chain locking, local append before broadcast and clean shutdown. Existing message and mining commands were retained. |
| “Is it done?” followed by a request to add the identified remaining work. | Compared the repository with the assignment handout, helped prepare the three configuration files and required documentation, and assisted with three-node testing. | The build, block propagation and required fork limitation were tested locally. |

## Final review

The AI was not asked to create the entire assignment from scratch. Suggestions were applied to the student's existing code, kept in the student's preferred C style and checked against the assignment handout. The final implementation was compiled and tested using three local node processes.
