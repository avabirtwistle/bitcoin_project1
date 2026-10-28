# AI Usage Log

I used OpenAI Codex while working on this assignment. I used it to explain parts of C and OpenSSL that I did not understand, review my code, help find errors, and suggest ways to connect different parts of the program. I checked and adjusted the suggestions before using them. I also input the assignment requirements to ensure that I did not miss any required parts of the implementation. 

**Tool:** OpenAI Codex  
**Date used:** October 2026

## Example prompts
I was not familiar with the OPENSSL libraries that were needed to complete this assignment so I asked questions regarding how these worked, what inputs they took etc. I also wrote out general ideas I had and asked if they were feasible such as "should using fork or SIMD be considered for mining or is this not something to worry about". 

- **“What is a hash for block vs the address?”**  
  This helped me understand the difference between hashing a public key to create a Bitcoin address and hashing a block during mining.

- **“Should it take a pointer?”**  
  I asked this while writing the Base58 function. The response helped me understand how arrays are passed to functions in C and why the caller provides the output buffer.

- **“Can’t we just put the remainders in reverse to begin with?”**  
  This helped me understand why Base58 division produces the characters in reverse order.

- **“What is serialization?”**  
  I used the explanation to create a consistent field order for block hashing. The block’s hash field was kept out of the serialized data.

- **“What is the genesis block and how do I start it?”**  
  This helped me initialize the blockchain using the hardcoded genesis block values from the assignment.

- **“Should the pending messages just be added here?”**  
  This helped me decide when to copy messages from the pending linked list into a block’s data array. The pending messages are only cleared after mining succeeds.

- **“How do I add mine and test it in main?”**  
  I used the response to connect block creation, mining, appending, and message cleanup to the `mine` command. I tested with lower difficulties before using difficulty 4.

- **“For the final part, how should I set up communication and which library should I use?”**  
  I used the response to compare HTTP and raw TCP communication. I chose direct TCP sockets and used cJSON for sending block data.

- **“Look through what I have written.”**  
  Codex reviewed my block and networking code and pointed out mismatched declarations, missing error checks, and memory cleanup problems.

- **“Fix this file, but keep my style and do not add unnecessary things.”**  
  I used the suggested changes to finish parts of the TCP listener, block broadcasting, offline-peer warnings, and network shutdown. This also helped clear out some dead code or misnamed parts that I missed. 

- **“Is it done?”**  
  I used the response to compare my project with the assignment requirements. I checked the configuration files, README, block validation rules, and first-valid-block-wins behaviour.

## Testing

After making changes, I compiled the program with warnings enabled and tested it locally. I tested vanity address generation, pending messages, mining at different difficulties, communication between three nodes, offline-peer handling, block validation, and the expected same-height fork limitation.
