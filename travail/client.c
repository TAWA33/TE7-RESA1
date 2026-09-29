#include "common.h"
#include "msg_struct.h"

#include <arpa/inet.h>
#include <netdb.h>
#include <netinet/in.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <unistd.h>
#include<poll.h>


void stdin_pollin(int sockfd, char* nickname){
	char buff[MSG_LEN];
	memset(buff, 0, MSG_LEN);

	ssize_t n = read(STDIN_FILENO, buff, MSG_LEN - 1);
	if (n <= 0){
		close(sockfd);
		exit(EXIT_SUCCESS);
	}
	buff[n-1] = '\0';

	struct message client_msg = {0};
    char *payload = NULL;
    client_msg.pld_len = 0;

    if (buff[0] == '/') {
        // First word separation
        char *cmd = buff;
        char *args = NULL;
        char *sep = strpbrk(buff, " \t");

        if (sep != NULL) {
            *sep = '\0';
            args = sep + 1;
            while (*args == ' ' || *args == '\t') args++; // Go to the first char
        } else {
            args = buff + strlen(buff); // sep point on '\0'
        }

        if (strcmp(cmd, "/quit") == 0){
            close(sockfd);
            exit(EXIT_SUCCESS);
        } else if (strcmp(cmd, "/nick") == 0) {
            client_msg.type = NICKNAME_NEW;
            strncpy(client_msg.infos, args, INFOS_LEN - 1);
            client_msg.infos[INFOS_LEN - 1] = '\0';
        } else if (strcmp(cmd, "/who") == 0) {
            client_msg.type = NICKNAME_LIST;
            client_msg.infos[0] = '\0';
        } else if (strcmp(cmd, "/whois") == 0) {
            client_msg.type = NICKNAME_INFOS;
            strncpy(client_msg.infos, args, INFOS_LEN - 1);
        } else if (strcmp(cmd, "/msgall") == 0) {
            client_msg.type = BROADCAST_SEND;
            client_msg.pld_len = strlen(args) + 1;
            payload = args;
        } else if (strcmp(cmd, "/msg") == 0) { // args = "<pseudo> <message>"
            char *dest = args;
            char *msg = strpbrk(args, " \t");

            if (msg != NULL) {
                *msg = '\0';
                msg++;
                while (*msg == ' ' || *msg == '\t') msg++;
            } else {
                msg = args + strlen(args); // empty msg
            }

            client_msg.type = UNICAST_SEND;
            strncpy(client_msg.infos, dest, INFOS_LEN - 1);
            client_msg.pld_len = strlen(msg) + 1;
            payload = msg;
        } else { // Unknown cmd => ECHO
            client_msg.type = ECHO_SEND;
            client_msg.pld_len = strlen(buff) + 1;
            payload = buff;
        }
    } else { // No cmd : ECHO
        client_msg.type = ECHO_SEND;
        client_msg.pld_len = strlen(buff) + 1;
        payload = buff;
    }
    strncpy(client_msg.nick_sender, nickname, NICK_LEN - 1);
    client_msg.infos[INFOS_LEN - 1] = '\0';
    client_msg.infos[INFOS_LEN - 1] = '\0';

    //printf("STRUCT MSG : %d / %s / %s / %s\n", client_msg.pld_len, client_msg.nick_sender, msg_type_str[client_msg.type], client_msg.infos);

    send_on_socket(sockfd, &client_msg, sizeof(struct message));

    if (client_msg.pld_len > 0 && payload != NULL) {
        send_on_socket(sockfd, payload, client_msg.pld_len);
    }
}


int server_socket_pollin(int sockfd, char *nickname) {
    struct message server_msg = {0};
    int r = recv_from_socket(sockfd, &server_msg, sizeof(struct message));

    //printf("STRUCT MSG : %d / %s / %s / %s\n", server_msg.pld_len, server_msg.nick_sender, msg_type_str[server_msg.type], server_msg.infos);
    if (r == 0) {
        fprintf(stdout, "Server closed the connection\n");
        return 1;
    }
    char* msg = NULL;
    if (server_msg.pld_len > 0){
        msg = malloc(server_msg.pld_len + 1);
        if (msg == NULL){
		return 0;
	    }
        r = recv_from_socket(sockfd, msg, server_msg.pld_len);
        if (r == 0) {
            fprintf(stdout, "Closing during reading Payload\n");
            free(msg);
            return 1;
        }
        msg[server_msg.pld_len] = '\0';
    }
    switch (server_msg.type) {
        case NICKNAME_LIST:
            fprintf(stdout, "[Server-%s] : Online users are\n%s\n", msg_type_str[ECHO_SEND], msg ? msg : "");
            break;
        case NICKNAME_INFOS:
            fprintf(stdout, "[Server-%s] : %s\n", msg_type_str[ECHO_SEND], msg ? msg : "");
            break;
        case NICKNAME_NEW:
        case UNICAST_SEND:
            fprintf(stdout, "[%s-%s] : %s\n", server_msg.nick_sender, msg_type_str[server_msg.type], msg ? msg : "");
            break;
        case BROADCAST_SEND:
            if (server_msg.nick_sender[0] != '\0')
                fprintf(stdout, "[%s-%s] : %s\n", server_msg.nick_sender, msg_type_str[server_msg.type], msg ? msg : "");
            else
                fprintf(stdout, "[Server-%s] : %s\n", msg_type_str[ECHO_SEND], msg ? msg : "");
            break;
        case ECHO_SEND:
        default:
            fprintf(stdout, "[Server-%s] : %s\n", msg_type_str[ECHO_SEND], msg ? msg : "");
            break;
    }
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

void client_loop(int socket_fd, char* nickname){
    struct pollfd fds[2]; // SIZE 2 : STDIN_FILENO / socket_fd

	fds[0].fd = STDIN_FILENO;
    fds[0].events = POLLIN;
    fds[0].revents = 0;

	fds[1].fd = socket_fd;
    fds[1].events = POLLIN;
    fds[1].revents = 0;

	printf("Enter /nick <nickname>: ");
    fflush(stdout); // To flush stdout. If not the buffer is not initialised correctly after

	while(1){
		int nb_fds = poll(fds, 2, -1);
        die(nb_fds, "Poll: ");
        if (fds[0].revents & POLLIN){ // STDIN_FILENO
			stdin_pollin(socket_fd, nickname);
			
		} else if (fds[1].revents & POLLIN){ // socket_fd
			int server_resp = server_socket_pollin(socket_fd, nickname);
			if (server_resp){
				break;
			}
		}
        printf("\n");
	}
}

int main(int argc, char** argv) {
	if (argc != 3){
        fprintf(stdout, "Must have only 2 arg, please type: ./client <SERVER_IP> <SERVER_PORT>\n");
        exit(EXIT_FAILURE);
    }
    char* server_ip = argv[1];
    char* server_port = argv[2];

	int sfd;
    char nickname[NICK_LEN] = {0};
	sfd = handle_connect(server_port, server_ip);
    if (sfd < 0) {
		exit(EXIT_FAILURE);
	}

    client_loop(sfd, nickname);	
	close(sfd);
	return EXIT_SUCCESS;
}

