#pragma once

/*
----- CHAT -----

The chat display.
*/

struct ChatMessageStruct {
    unsigned short ticks;
    char *message;
    struct ChatMessageStruct *previous;
    struct ChatMessageStruct *next;
};

typedef struct ChatMessageStruct ChatMessage;

extern ChatMessage *lastMessage;

/* sendMessage
Sends a message to the chat.

message: The message to send.
*/
void sendMessage(char *message);

/* clearMessage
Clears the given message and any previous messages.

message: Pointer to the message to clear
*/
void clearMessage(ChatMessage *message);

/* setStatusMessage
Shows a status message (wrapped over several lines) at the top of the screen
for a few seconds. The text is copied.
*/
void setStatusMessage(const char *message);

extern char statusMessage[];
extern int statusMessageTicks;

/* updateChat
Updates chat, should be called every engine tick.
*/
void updateChat();