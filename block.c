#include "block.h"

/*adds a message to the pending message pool*/
int add_pending_message(message_list_t *pending, const char *message) {
    message_node_t *new_message = malloc(sizeof(message_node_t));
    if (new_message == NULL) {
        return -1; // Memory allocation failed
    }

    //allocate memory for message
    new_message->message = malloc(strlen(message)+1);
    if (new_message->message == NULL) {
        free(new_message);
        return -1; // Memory allocation failed
    }

    strcpy(new_message->message, message);
    new_message->next = NULL;

    //copy the message to the new node and init pointer
    memcpy(new_message->message, message, strlen(message) + 1);
    new_message->next = NULL;

    if (pending->head_message == NULL) { //if list empty
        pending->head_message = new_message;
        pending->tail_message = new_message;
    } else { 
        pending->tail_message->next = new_message;
        pending->tail_message = new_message;
    }
    pending->count++;// increment count of pending messages 
    return 0;
}