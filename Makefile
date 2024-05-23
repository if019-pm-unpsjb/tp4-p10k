CC=gcc
CFLAGS=-Wall -Werror -g -pthread 
BIN=./bin

PROGS=server-tftp server-chat

.PHONY: all
all: $(PROGS)

LIST=$(addprefix $(BIN)/, $(PROGS))

server-tftp: server-tftp.c 
	$(CC) -o bin/$@ $^ $(CFLAGS)

client-tftp: client-tftp.c 
	$(CC) -o bin/$@ $^ $(CFLAGS)

server-chat: server-chat.c 
	$(CC) -o bin/$@ $^ $(CFLAGS)

client-chat: client-chat.c 
	$(CC) -o bin/$@ $^ $(CFLAGS)

.PHONY: clean
clean:
	rm -f $(LIST)

zip:
	git archive --format zip --output ${USER}-TP4.zip HEAD
