CC = gcc
CFLAGS = -Wall
#LDFLAGS=-lpthread

CLIENT = client
SERVER = server
TARGETS = $(CLIENT) $(SERVER)

COMMON_OBJ = common.o
CLIENT_OBJ = client.o $(COMMON_OBJ)
SERVER_OBJ = server.o client_list.o $(COMMON_OBJ)

all: $(TARGETS)

$(CLIENT): $(CLIENT_OBJ)
	$(CC) $^ -o $@ $(LDFLAGS)

$(SERVER): $(SERVER_OBJ)
	$(CC) $^ -o $@ $(LDFLAGS)

%.o: %.c
	$(CC) $(CFLAGS) -c $< -o $@


clean:
	rm -f $(CLIENT_OBJ) $(SERVER_OBJ) $(TARGETS) *.d

.PHONY: all clean