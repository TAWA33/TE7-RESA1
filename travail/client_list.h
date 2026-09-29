#ifndef CLIENT_LIST_H
#define CLIENT_LIST_H

#include <sys/socket.h>
#include <time.h>
#include "msg_struct.h"

struct client_node {
    int fd;
    struct sockaddr_storage addr;
    socklen_t addr_len;
    char nickname[NICK_LEN];
    time_t connected_at;
    struct client_node* next;
};

int add_client(struct client_node** head, int fd, struct sockaddr_storage addr, socklen_t addr_len);
int remove_client(struct client_node** head, int fd);
struct client_node* find_client_with_fd(struct client_node* head, int fd);
struct client_node* find_client_with_name(struct client_node* head, char* nickname);
int nickname_exists(struct client_node* head, char* nickname);
int* connected_client_fd(struct client_node* head);
void free_list(struct client_node** head);

#endif