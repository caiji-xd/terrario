#include <stdlib.h>
#include <string.h>

#include <gint/defs/util.h>

#include "chat.h"
#include "defs.h"
#include "menu.h"

ChatMessage *lastMessage = NULL;

char statusMessage[64];
int statusMessageTicks = 0;

void setStatusMessage(const char *message)
{
	strncpy(statusMessage, message, sizeof(statusMessage) - 1);
	statusMessage[sizeof(statusMessage) - 1] = '\0';
	statusMessageTicks = STATUS_MESSAGE_TICKS;
}

void sendMessage(char *message)
{
	ChatMessage *newMessage = malloc(sizeof(ChatMessage));
	allocCheck(newMessage);
	ChatMessage *testMessage;
	int depth = 0;
	int length = min(strlen(message), MESSAGE_MAX_LENGTH);

	*newMessage = (ChatMessage){
		.previous = lastMessage
	};
	newMessage->message = malloc(length + 1);
	allocCheck(newMessage->message);
	newMessage->message[length] = '\0';
	strncpy(newMessage->message, message, length);
	if(lastMessage != NULL) lastMessage->next = newMessage;
	lastMessage = newMessage;

	testMessage = lastMessage;
	while(depth < MAX_MESSAGES_DISPLAYED)
	{
		testMessage = testMessage->previous;
		if(testMessage == NULL) return;
		depth++;
	}
	if(depth == MAX_MESSAGES_DISPLAYED) clearMessage(testMessage);
}

void clearMessage(ChatMessage *message)
{
	if(message->previous != NULL) clearMessage(message->previous);
	if(message->next != NULL) message->next->previous = NULL;
	free(message->message);
	free(message);
}

void updateChat()
{
	ChatMessage *currMessage = lastMessage;

	if(statusMessageTicks > 0) statusMessageTicks--;

	while(currMessage != NULL)
	{
		currMessage->ticks++;
		if(currMessage->ticks == CHAT_MESSAGE_TICKS)
		{
			clearMessage(currMessage);
			if(currMessage == lastMessage) lastMessage = NULL;
			break;
		}
		else currMessage = currMessage->previous;
	}
}