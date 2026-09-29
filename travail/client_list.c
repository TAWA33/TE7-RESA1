#include "client_list.h"
#include <string.h>
#include <time.h>
#include <stdlib.h>
#include <stdio.h>
#include <errno.h>


int add_client(struct client_node** head, int fd, struct sockaddr_storage addr, socklen_t addr_len){
    struct client_node *new_node = malloc(sizeof(struct client_node));
    if (new_node == NULL) {
        return -1;
    }
    time_t time_now = time(NULL);
    if (time_now == (time_t)-1){
        perror("Time:");
        return -1;
    }

    new_node->next = *head;
    new_node->fd = fd;
    new_node->addr = addr;
    new_node->addr_len = addr_len;
    new_node->nickname[0] = '\0';
    new_node->connected_at = time_now;
    *head = new_node;
    return 0;
}

int remove_client(struct client_node** head, int fd){
    struct client_node* previous_node = NULL;
    struct client_node* curent_node = *head;

    while (curent_node != NULL && curent_node->fd != fd) {
        previous_node = curent_node;
        curent_node = curent_node->next;
    }

    if (curent_node == NULL) {
        return -1;  // Not found
    }

    if (previous_node == NULL) {
        *head = curent_node->next;
    } else {
        previous_node->next = curent_node->next;
    }
    free(curent_node);
    return 0;
}

struct client_node* find_client_with_fd(struct client_node* head, int fd){
    while (head != NULL) {
        if (head->fd == fd) {
            return head;
        }
        head = head->next;
    }
    return NULL;
}

struct client_node* find_client_with_name(struct client_node* head, char* nickname){
    while (head != NULL) {
        if (head->nickname[0] != '\0' && strcmp(head->nickname, nickname) == 0) {
            return head;
        }
        head = head->next;
    }
    return NULL;
}

int nickname_exists(struct client_node* head, char* nickname){
    return (find_client_with_name(head, nickname) != NULL);
}

int* connected_client_fd(struct client_node* head){
    struct client_node* c = head;
    int nb_client = 0;
    while (c != NULL) { // First step to determine the size
        nb_client++;
        c = c->next;
    }
    int *clients_fd = malloc((nb_client + 1) * sizeof(int));
    if (clients_fd == NULL){
        return NULL;
    }
    int i = 0;
    c = head;
    while (c != NULL) {
        clients_fd[i] = c->fd;
        i++;
        c = c->next;        
    }
    clients_fd[i] = -1;
    return clients_fd;
}

void free_list(struct client_node** head){
    struct client_node *current = *head;
    while (current != NULL) {
        struct client_node *next = current->next;
        free(current);
        current = next;
    }
    *head = NULL;
}