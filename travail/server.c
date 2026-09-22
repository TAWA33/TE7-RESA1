#include <arpa/inet.h>
#include <netdb.h>
#include <netinet/in.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <unistd.h>

#include<poll.h>

#include "common.h"

struct client_node {
    int fd;
    struct sockaddr addr;
    struct client_node* next;
};

int add_client(struct client_node** head, int fd, struct sockaddr addr){
    struct client_node *new_node = malloc(sizeof(struct client_node));
    if (new_node == NULL) {
        return -1;
    }
    new_node->next = *head;
    new_node->fd = fd;
    new_node->addr = addr;
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

struct client_node* find_client(struct client_node* head, int fd){
    while (head != NULL) {
        if (head->fd == fd) {
            return head;
        }
        head = head->next;
    }
    return NULL;
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


void die(int ret, char* msg){
    if (ret < 0){
        perror(msg);
        exit(EXIT_FAILURE);
    }
}

int send_on_socket(int socket_fd, void* buf, int size){
    int size_sent = 0;
    int ret_value = 0;
    while(size_sent != size){
        ret_value = send(socket_fd, (char*)(buf)+size_sent, size-size_sent, 0);
        die(ret_value, "Writing on socket");
        if(ret_value == 0){
            close(socket_fd);
            return size_sent;
        }
        size_sent += ret_value;
    }
    return size_sent;
}

int recv_from_socket(int socket_fd, void* buf, int size){
    int size_read = 0;
    int ret_value = 0;
    while(size_read != size){
        ret_value = recv(socket_fd, (char*)(buf)+size_read, size-size_read, 0);
        die(ret_value, "Reading on socket");
        if(ret_value == 0){
            close(socket_fd);
            return size_read;
        }
        size_read += ret_value;
    }
    return size_read;
}

int first_fds_available(struct pollfd * fds, int size){
    for (int i = 1 ; i < size ; i++){
        if (fds[i].fd == -1){
            return i;
        }
    }
    return -1;
}

void closing_connection(struct pollfd *fds, int i, struct client_node** head) {
	remove_client(head, fds[i].fd);
    close(fds[i].fd);
    fds[i].fd = -1;
    fds[i].events = 0;
    fds[i].revents = 0;
}

int echo_server(int sockfd) {
	struct header client_header = {0};
	int size_read = recv_from_socket(sockfd, &client_header, sizeof(struct header));

	char* client_msg = NULL;
	client_msg = malloc(client_header.size * sizeof(char));
	if (client_msg == NULL) {
		fprintf(stdout, "Error Allocating memory\n");
		return 0;
	}
	
	size_read = recv_from_socket(sockfd, client_msg, client_header.size);
	if (size_read == 0) {
		fprintf(stdout, "Closing during reading Payload \n");
		free(client_msg);
		return 0;
	}
	fprintf(stdout, "Response message Sent\n");
	send_on_socket(sockfd, &client_header, sizeof(struct header));
	send_on_socket(sockfd, client_msg, client_header.size);

	if (strcmp(client_msg, QUIT) == 0){ // We already send the message /quit to alert the client to close his connection
		fprintf(stdout, "Closing using quit \n");
		free(client_msg);
		return 1;		
	}
	return 0;
	// char buff[MSG_LEN];
	
	// // Cleaning memory
	// memset(buff, 0, MSG_LEN);
	// // Receiving message
	// read_from_socket(sockfd, buff, MSG_LEN);
	// printf("Received: %s", buff);
	// // Sending message (ECHO)
	// write_on_socket(sockfd, buff, strlen(buff));
	// printf("Message sent!\n");

}

int handle_bind(char* server_port) {
	struct addrinfo hints, *result, *rp;
	int sfd;
	memset(&hints, 0, sizeof(struct addrinfo));
	hints.ai_family = AF_UNSPEC;
	hints.ai_socktype = SOCK_STREAM;
	hints.ai_flags = AI_PASSIVE;
	if (getaddrinfo(NULL, server_port, &hints, &result) != 0) {
		perror("getaddrinfo()");
		exit(EXIT_FAILURE);
	}
	for (rp = result; rp != NULL; rp = rp->ai_next) {
		sfd = socket(rp->ai_family, rp->ai_socktype,
		rp->ai_protocol);
		if (sfd == -1) {
			continue;
		}
		if (bind(sfd, rp->ai_addr, rp->ai_addrlen) == 0) {
			break;
		}
		close(sfd);
	}
	if (rp == NULL) {
		fprintf(stderr, "Could not bind\n");
		exit(EXIT_FAILURE);
	}
	freeaddrinfo(result);
	return sfd;
}

int main(int argc, char** argv) {
	if (argc != 2){
        fprintf(stderr, "Must have only 1 arg, please type: ./server <SERVER_PORT>\n");
        exit(EXIT_FAILURE);
    }
    char* server_port = argv[1];

	struct sockaddr cli;
	int sfd, connfd;
	socklen_t len;

	sfd = handle_bind(server_port); // Socket Server

	if ((listen(sfd, SOMAXCONN)) != 0) {
		perror("listen()\n");
		exit(EXIT_FAILURE);
	}

	struct client_node *head = NULL;

	struct pollfd fds[FD_SIZE_TAB];
    fds[0].fd = sfd;
    fds[0].events = POLLIN;
    fds[0].revents = 0;
    
    for (int i = 1; i< FD_SIZE_TAB; i++){
        fds[i].fd = -1;
        fds[i].events = 0;
        fds[i].revents = 0;
    }

	while(1){
        int nb_fds = poll(fds, FD_SIZE_TAB, -1);
        die(nb_fds, "Poll: ");

        for (int i = 0 ; i < FD_SIZE_TAB ; i++){
            if (i == 0 && (fds[0].revents & POLLIN)){ 
				fds[i].revents = 0;
				len = sizeof(cli);
				if ((connfd = accept(sfd, (struct sockaddr*) &cli, &len)) < 0) {
					perror("accept()\n");
					exit(EXIT_FAILURE);
				}

				int fd_number = first_fds_available(fds, FD_SIZE_TAB);
				fprintf(stdout, "client fds %d\n", fd_number);
                if (fd_number == -1) {
                    fprintf(stderr, "Too many clients => refusing %d\n", connfd);
                    continue;
                }
                add_client(&head, connfd, cli);
               

                fds[fd_number].fd = connfd;
                fds[fd_number].events = POLLIN;
                fds[fd_number].revents = 0;

			}

			if (i!=0 && (fds[i].revents & POLLIN)){
                fds[i].revents = 0;
				int quit_value = 0;
				quit_value = echo_server(fds[i].fd);
				if (quit_value){
					closing_connection(fds, i, &head);
				}
			}
		}
	}
	free_list(&head);
	close(sfd);
	return EXIT_SUCCESS;
}

