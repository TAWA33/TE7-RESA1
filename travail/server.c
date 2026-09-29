#include "common.h"
#include "client_list.h"

#include <arpa/inet.h>
#include <netdb.h>
#include <netinet/in.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <unistd.h>
#include<poll.h>
#include <ctype.h>


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

int send_message(int sockfd, struct message *msg, char *payload) {
    if (send_on_socket(sockfd, msg, sizeof(struct message)) < 0){
        return -1;
    }
    if (msg->pld_len > 0 && payload != NULL) {
        if (send_on_socket(sockfd, (void *)payload, msg->pld_len) < 0){
            return -1;
        }
    }
    return 0;
}

void send_server_info(int sockfd, char *text) {
    struct message msg = {0};
    msg.type = ECHO_SEND;
    msg.nick_sender[0] = '\0';
    msg.pld_len = (int)strlen(text) + 1;
    send_message(sockfd, &msg, text);
}

int handle_client_message(struct client_node *head, struct client_node *me, struct message *msg, char *payload) {

    if (msg->type != NICKNAME_NEW && me->nickname[0] == '\0') {
        send_server_info(me->fd, "Please set a nickname with /nick <pseudo> first.");
        return 0;
    }
    //printf("STRUCT MSG : %d / %s / %s / %s\n", msg->pld_len, msg->nick_sender, msg_type_str[msg->type], msg->infos);

    switch (msg->type) {
        case NICKNAME_NEW: {
            char *new_nick = msg->infos;

            size_t len = strlen(new_nick);
            if (len == 0 || len >= NICK_LEN) {
                send_server_info(me->fd, "Invalid nickname: must be between 1 and 127 characters.");
                return 0;
            }
            for (size_t i = 0; i < len; i++) {
                if (!isalnum((unsigned char)new_nick[i])) {
                    send_server_info(me->fd, "Invalid nickname: letters and digits only.");
                    return 0;
                }
            }
            if (nickname_exists(head, (char *)new_nick)) {
                send_server_info(me->fd, "Nickname already taken.");
                return 0;
            }

            strncpy(me->nickname, new_nick, NICK_LEN - 1);
            me->nickname[NICK_LEN - 1] = '\0';

            char welcome[MSG_LEN];
            snprintf(welcome, sizeof(welcome), "Welcome on the chat %s", me->nickname);
            send_server_info(me->fd, welcome);
            return 0;
        }
        case ECHO_SEND: {
            struct message out = {0};
            out.type = ECHO_SEND;
            strncpy(out.nick_sender, me->nickname, NICK_LEN - 1);
            out.pld_len = msg->pld_len;
            send_message(me->fd, &out, payload ? payload : "");
            return 0;
        }
        case NICKNAME_LIST: {
            char buf[MSG_LEN] = {0};
            size_t off = 0;
            for (struct client_node *c = head; c; c = c->next) {
                if (c->nickname[0] == '\0'){
                    continue;
                }
                int w = snprintf(buf + off, sizeof(buf) - off, "\t - %s\n", c->nickname);
                if (w < 0 || (size_t) w >= sizeof(buf) - off){
                    break;
                }
                off += (size_t) w;
            }
            struct message out = {0};
            out.type = NICKNAME_LIST;
            out.pld_len = (int) strlen(buf) + 1;
            send_message(me->fd, &out, buf);
            return 0;
        }
        case NICKNAME_INFOS: {
            struct client_node *target = find_client_with_name(head, msg->infos);
            if (target == NULL) {
                send_server_info(me->fd, "Unknown user.");
                return 0;
            }

            char ip[INET6_ADDRSTRLEN] = {0};
            int port = 0;
            if (target->addr.ss_family == AF_INET) { // IPv4 case
                struct sockaddr_in *s = (struct sockaddr_in*) &target->addr;
                inet_ntop(AF_INET, &s->sin_addr, ip, sizeof(ip));
                port = ntohs(s->sin_port);
            } else if (target->addr.ss_family == AF_INET6) { // IPv6 case
                struct sockaddr_in6 *s = (struct sockaddr_in6*) &target->addr;
                inet_ntop(AF_INET6, &s->sin6_addr, ip, sizeof(ip));
                port = ntohs(s->sin6_port);
            }
            struct tm tm;
            localtime_r(&target->connected_at, &tm);
            char datebuf[64];
            strftime(datebuf, sizeof(datebuf), "%Y/%m/%d@%H:%M", &tm);

            char buf[MSG_LEN];
            snprintf(buf, sizeof(buf), "%s connected since %s with IP address %s and port number %d", target->nickname, datebuf, ip, port);

            struct message out = {0};
            out.type = NICKNAME_INFOS;
            out.pld_len = (int) strlen(buf) + 1;
            send_message(me->fd, &out, buf);
            return 0;
        }

        case BROADCAST_SEND: {
            struct message out = {0};
            out.type = BROADCAST_SEND;
            out.pld_len = msg->pld_len;
            strncpy(out.nick_sender, me->nickname, NICK_LEN - 1);
            int* clients_fd = connected_client_fd(head);
            if (clients_fd == NULL){
                return 0;
            }
            int i = 0;
            if(clients_fd[i] == -1){
                printf("No other client");
                free(clients_fd);
                return 0;
            }
            while(clients_fd[i] != -1){
                if (clients_fd[i] != me->fd){
                    send_message(clients_fd[i], &out, payload);
                    printf("Sending to %d\n", clients_fd[i]);
                }
                i++;
            }
            free(clients_fd);
            return 0;

        }

        case UNICAST_SEND: {
            struct message out = {0};
            char* dest_nickname = msg->infos;
            if (nickname_exists(head, dest_nickname)){
                out.type = UNICAST_SEND;
                out.pld_len = msg->pld_len;
                strncpy(out.nick_sender, me->nickname, NICK_LEN - 1);
                struct client_node* dest = find_client_with_name(head, dest_nickname);
                send_message(dest->fd, &out, payload ? payload : "");
            } else {
                char* error_msg = "This nickname doesn't exist.";
                out.type = ECHO_SEND;
                out.pld_len = strlen(error_msg) + 1;
                send_message(me->fd, &out, error_msg);
            }
            return 0;
        }

        default:
            fprintf(stderr, "Unhandled message type: %d\n", msg->type);
            send_server_info(me->fd, "Unsupported message type.");
            return 0;
    }
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

int recv_full_message(int sockfd, struct message *msg, char **payload_out){
    *payload_out = NULL;
    if (recv_from_socket(sockfd, msg, sizeof(struct message)) == 0){
        return 1;
    }
    if (msg->pld_len < 0 || msg->pld_len > MSG_LEN) {
        return 1;
    } 
    if (msg->pld_len == 0){
        *payload_out = NULL;
        return 0;
    }
    *payload_out = malloc(msg->pld_len * sizeof(char));
    if (*payload_out == NULL){
        return 1;
    }

    if (recv_from_socket(sockfd, *payload_out, msg->pld_len) == 0){
        free(*payload_out);
        *payload_out = NULL;
        return 1;
    }
    (*payload_out)[msg->pld_len - 1] = '\0';
    return 0;
}

void server_loop(int sfd){
    struct sockaddr_storage cli;
    int connfd;
    socklen_t len;
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
                    close(connfd);
                    continue;
                }
                add_client(&head, connfd, cli, len);
               

                fds[fd_number].fd = connfd;
                fds[fd_number].events = POLLIN;
                fds[fd_number].revents = 0;

			}

			if (i!=0 && (fds[i].revents & POLLIN)){
                fds[i].revents = 0;
				struct client_node *me = find_client_with_fd(head, fds[i].fd);
                if (me == NULL){
                    closing_connection(fds, i, &head); 
                    continue; 
                }
                struct message msg;
                char *payload = NULL;
                if (recv_full_message(fds[i].fd, &msg, &payload)) {
                    closing_connection(fds, i, &head);
                    continue;
                }
                int quit_value = handle_client_message(head, me, &msg, payload);
                free(payload);
				if (quit_value){
					closing_connection(fds, i, &head);
				}
			}
		}
	}
	free_list(&head);
}

int main(int argc, char** argv) {
	if (argc != 2){
        fprintf(stderr, "Must have only 1 arg, please type: ./server <SERVER_PORT>\n");
        exit(EXIT_FAILURE);
    }
    char* server_port = argv[1];
	int sfd;
	sfd = handle_bind(server_port); // Socket Server

	if ((listen(sfd, SOMAXCONN)) != 0) {
		perror("listen()\n");
		exit(EXIT_FAILURE);
	}
    server_loop(sfd);
	close(sfd);
	return EXIT_SUCCESS;
}

