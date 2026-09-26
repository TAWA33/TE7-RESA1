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

void msg_to_server(int sockfd){
	char buff[MSG_LEN];
	memset(buff, 0, MSG_LEN);

	ssize_t n = read(STDIN_FILENO, buff, MSG_LEN - 1);
	if (n <= 0){
		close(sockfd);
		exit(EXIT_SUCCESS);
	}
	buff[n] = '\0';

	struct header client_h = {0};
    client_h.size = n + 1;
    send_on_socket(sockfd, &client_h, sizeof(struct header));
    send_on_socket(sockfd, buff, client_h.size);
    printf("Message sent!\n");


}

int msg_from_server(int sockfd) {
    struct header server_h = {0};
    int r = recv_from_socket(sockfd, &server_h, sizeof(struct header));
    if (r == 0) {
        fprintf(stdout, "Server closed the connection\n");
        return 1;
    }
    char* msg = malloc(server_h.size);
    if (msg == NULL){
		return 0;
	}

    r = recv_from_socket(sockfd, msg, server_h.size);
    if (r == 0) {
        fprintf(stdout, "Closing during reading Payload\n");
        free(msg);
        return 1;
    }
    if (strcmp(msg, QUIT) == 0) {
        fprintf(stdout, "Closing Connection OK\n");
        free(msg);
        return 1;
    }

    printf("Received: %s\n", msg);
    free(msg);
    return 0;
}

int handle_connect(char* server_port, char* server_ip) {
	struct addrinfo hints, *result, *rp;
	int sfd;
	memset(&hints, 0, sizeof(struct addrinfo));
	hints.ai_family = AF_UNSPEC;
	hints.ai_socktype = SOCK_STREAM;
	if (getaddrinfo(server_ip, server_port, &hints, &result) != 0) {
		perror("getaddrinfo()");
		exit(EXIT_FAILURE);
	}
	for (rp = result; rp != NULL; rp = rp->ai_next) {
		sfd = socket(rp->ai_family, rp->ai_socktype,rp->ai_protocol);
		if (sfd == -1) {
			continue;
		}
		if (connect(sfd, rp->ai_addr, rp->ai_addrlen) != -1) {
			break;
		}
		close(sfd);
	}
	if (rp == NULL) {
		fprintf(stderr, "Could not connect\n");
		exit(EXIT_FAILURE);
	}
	freeaddrinfo(result);
	return sfd;
}

int main(int argc, char** argv) {
	if (argc != 3){
        fprintf(stdout, "Must have only 2 arg, please type: ./client <SERVER_IP> <SERVER_PORT>\n");
        exit(EXIT_FAILURE);
    }
    char* server_ip = argv[1];
    char* server_port = argv[2];

	int sfd;
	sfd = handle_connect(server_port, server_ip);

	struct pollfd fds[2]; // SIZE 2 : STDIN_FILENO / sfd

	fds[0].fd = STDIN_FILENO;
    fds[0].events = POLLIN;
    fds[0].revents = 0;

	fds[1].fd = sfd;
    fds[1].events = POLLIN;
    fds[1].revents = 0;

	printf("Message: ");
    fflush(stdout); // To flush stdout. If not the buffer is not initialised correctly after

	while(1){
		int nb_fds = poll(fds, 2, -1);
        die(nb_fds, "Poll: ");
        //fprintf(stdout, "Nb active fd : %d\n", nb_fds);
        if (fds[0].revents & POLLIN){ // STDIN_FILENO
			msg_to_server(sfd);
			
		} else if (fds[1].revents & POLLIN){ // sfd
			int server_resp = msg_from_server(sfd);
			if (server_resp){
				break;
			}
			printf("Message: ");
			fflush(stdout);
		}
	}
	close(sfd);
	return EXIT_SUCCESS;
}

