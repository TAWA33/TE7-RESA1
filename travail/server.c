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
#include <errno.h>
#include <signal.h>

static volatile sig_atomic_t stop = 0;

static void on_signal(int sig) {
    (void)sig;
    stop = 1;
}

static void signal_stop_server_install(void) {
    struct sigaction sa;
    memset(&sa, 0, sizeof(sa));
    sa.sa_handler = on_signal;
    sigemptyset(&sa.sa_mask);
    sa.sa_flags = 0;
    sigaction(SIGINT,  &sa, NULL);
    sigaction(SIGTERM, &sa, NULL);
}

int first_fds_available(struct pollfd * fds, int size){
    for (int i = 1 ; i < size ; i++){
        if (fds[i].fd == -1){
            return i;
        }
    }
    return -1;
}

int send_message(int sockfd, struct message *msg, char *payload) {
    if (send_on_socket(sockfd, msg, sizeof(struct message)) != (int)sizeof(struct message)){
        return -1;
    }
    if (msg->pld_len > 0 && payload != NULL) {
        if (send_on_socket(sockfd, (void *)payload, msg->pld_len) != msg->pld_len){
            return -1;
        }
    }
    return 0;
}

int send_to_fd(int fd, enum msg_type type, char *sender, char *infos, void *payload, int pld_len){
    struct message out = {0};
    out.type = type;
    out.pld_len = pld_len;
    if (sender) {
        strncpy(out.nick_sender, sender, NICK_LEN - 1);
        out.nick_sender[NICK_LEN - 1] = '\0';
    }
    if (infos) {
        strncpy(out.infos, infos, INFOS_LEN - 1);
        out.infos[INFOS_LEN - 1] = '\0';
    }
    return send_message(fd, &out, (char *)payload);
}

void send_server_info(int sockfd, char *text) {
    send_to_fd(sockfd, ECHO_SEND, NULL, NULL, text, (int)strlen(text) + 1);
}

void forward_to_nickname(struct client_node *head, struct client_node *me, enum msg_type type, char *dest_nickname, char *payload, int pld_len){
    struct client_node *dest = find_client_with_name(head, dest_nickname);
    if (dest == NULL) {
        send_server_info(me->fd, "This nickname doesn't exist.");
        return;
    }
    struct message out = {0};
    out.type = type;
    out.pld_len = (payload != NULL) ? pld_len : 0;
    strncpy(out.nick_sender, me->nickname, NICK_LEN - 1);
    send_message(dest->fd, &out, payload ? payload : "");
}

void broadcast_to_all(struct client_node *head, enum msg_type type, char *sender, void *payload, int pld_len, int exclude_fd){
    struct message out = {0};
    out.type = type;
    out.pld_len = pld_len;
    if (sender) {
        strncpy(out.nick_sender, sender, NICK_LEN - 1);
        out.nick_sender[NICK_LEN - 1] = '\0';
    }
    for (struct client_node *c = head; c; c = c->next) {
        if (c->fd != exclude_fd) {
            send_message(c->fd, &out, (char *)payload);
        }
    }
}

void send_to_channel(struct client_node *head, char *channel, enum msg_type type, char *sender, void *payload, int pld_len, int exclude_fd){
    struct message out = {0};
    out.type = type;
    out.pld_len = pld_len;

    if (sender){
        strncpy(out.nick_sender, sender, NICK_LEN - 1);
    }
    if (channel){
        strncpy(out.infos, channel, INFOS_LEN - 1);
    }

    for (struct client_node *c = head; c; c = c->next) {
        if (c->fd != exclude_fd && c->channel[0] != '\0' && strcmp(c->channel, channel) == 0) {
            send_message(c->fd, &out, (char *)payload);
        }
    }
}

void notify_channel(struct client_node *head, char *channel, char *text, int exclude_fd) {
    send_to_channel(head, channel, MULTICAST_SEND, "SERVER", text, strlen(text) + 1, exclude_fd);
}

void leave_channel(struct client_node *head, struct client_node *me) {
    if (me->channel[0] == '\0'){
        return;
    }
    if (me->nickname[0] != '\0') {
        char info[MSG_LEN];
        snprintf(info, sizeof(info), "%s has quit %s", me->nickname, me->channel);
        notify_channel(head, me->channel, info, me->fd);
    }
    me->channel[0] = '\0';
}

void join_channel(struct client_node *head, struct client_node *me, char *name) {
    leave_channel(head, me);

    strncpy(me->channel, name, NICK_LEN - 1);
    me->channel[NICK_LEN - 1] = '\0';

    char info[MSG_LEN];
    snprintf(info, sizeof(info), "%s has joined %s", me->nickname, name);
    notify_channel(head, name, info, me->fd);
}

void closing_connection(struct pollfd *fds, int i, struct client_node** head) {
    struct client_node *me = find_client_with_fd(*head, fds[i].fd);
    if (me != NULL && me->channel[0] != '\0' && me->nickname[0] != '\0') {
        leave_channel(*head, me);
    }
    remove_client(head, fds[i].fd);
    close(fds[i].fd);
    fds[i].fd = -1;
    fds[i].events = 0;
    fds[i].revents = 0;
}

int is_valid_nickname(char* nickname) {
    size_t len = strlen(nickname);
    if (len == 0 || len >= NICK_LEN){
        return 0;
    }
    for (size_t i = 0; i < len; i++) {
        if (!isalnum((unsigned char)nickname[i])){
            return 0;
        }
    }
    return 1;
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
            if (strcmp(new_nick, "SERVER") == 0) {
                send_server_info(me->fd, "This nickname is reserved.");
                return 0;
            }
            if (!is_valid_nickname(new_nick)) {
                send_server_info(me->fd, "Invalid nickname: letters and digits only, max 127.");
                return 0;
            }
            struct client_node *other = find_client_with_name(head, new_nick);
            if (other != NULL && other != me) {
                send_server_info(me->fd, "Nickname already taken.");
                return 0;
            }
            strncpy(me->nickname, new_nick, NICK_LEN - 1);
            me->nickname[NICK_LEN - 1] = '\0';

            char welcome[MSG_LEN];
            snprintf(welcome, sizeof(welcome), "Welcome on the chat %s", me->nickname);
            send_to_fd(me->fd, NICKNAME_NEW, NULL, me->nickname, welcome, (int)strlen(welcome) + 1);
            return 0;
        }
        case ECHO_SEND: {
            send_to_fd(me->fd, ECHO_SEND, me->nickname, NULL, payload, msg->pld_len);
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
            broadcast_to_all(head, BROADCAST_SEND, me->nickname, payload, msg->pld_len, me->fd);
            return 0;
        }
        case UNICAST_SEND: {
            forward_to_nickname(head, me, UNICAST_SEND, msg->infos, payload, msg->pld_len);
            return 0;
        }
        case FILE_REQUEST: {
            forward_to_nickname(head, me, FILE_REQUEST, msg->infos, payload, msg->pld_len);
            return 0;
        }

        case FILE_REJECT: {
            forward_to_nickname(head, me, FILE_REJECT, msg->infos, NULL, 0);
            return 0;
        }
        case FILE_ACCEPT: {
            forward_to_nickname(head, me, FILE_ACCEPT, msg->infos, payload, msg->pld_len);
            return 0;
        }
        case MULTICAST_CREATE: {
            char *name = msg->infos;
            if (!is_valid_nickname(name)) {
                send_server_info(me->fd, "Invalid channel name: letters and digits only.");
                return 0;
            }
            if (channel_exists(head, name)) {
                send_server_info(me->fd, "Channel already exists.");
                return 0;
            }
            if (me->channel[0] != '\0') {
                leave_channel(head, me);
            }
            strncpy(me->channel, name, NICK_LEN - 1);
            me->channel[NICK_LEN - 1] = '\0';

            char text[MSG_LEN];
            snprintf(text, sizeof(text), "You have created and joined channel %s", name);
            send_to_fd(me->fd, MULTICAST_CREATE, "SERVER", name, text, (int)strlen(text) + 1);
            return 0;
        }

        case MULTICAST_LIST: {
            char buf[MSG_LEN] = {0};
            size_t off = 0;
            for (struct client_node *c = head; c; c = c->next) {
                if (c->channel[0] == '\0'){
                    continue;
                }
                int seen = 0;
                for (struct client_node *d = head; d != c; d = d->next) {
                    if (d->channel[0] != '\0' && strcmp(d->channel, c->channel) == 0) {
                        seen = 1; 
                        break;
                    }
                }
                if (seen){
                    continue;
                }
                int w = snprintf(buf + off, sizeof(buf) - off, "\t - %s\n", c->channel);
                if (w < 0 || (size_t)w >= sizeof(buf) - off){
                    break;
                }
                off += (size_t)w;
            }
            struct message out = {0};
            out.type = MULTICAST_LIST;
            out.pld_len = (int)strlen(buf) + 1;
            send_message(me->fd, &out, buf);
            return 0;
        }

        case MULTICAST_JOIN: {
            char *name = msg->infos;
            if (!is_valid_nickname(name)) {
                send_server_info(me->fd, "Invalid channel name.");
                return 0;
            }
            if (!channel_exists(head, name)) {
                send_server_info(me->fd, "Channel doesn't exist.");
                return 0;
            }
            if (me->channel[0] != '\0' && strcmp(me->channel, name) == 0) {
                send_server_info(me->fd, "You are already in this channel.");
                return 0;
            }

            join_channel(head, me, name);

            char text[MSG_LEN];
            snprintf(text, sizeof(text), "You have joined %s", name);
            send_to_fd(me->fd, MULTICAST_JOIN, "SERVER", name, text, (int)strlen(text) + 1);
            return 0;
        }

        case MULTICAST_SEND: {
            if (me->channel[0] == '\0' || strcmp(me->channel, msg->infos) != 0) {
                send_server_info(me->fd, "You are not in this channel.");
                return 0;
            }
            send_to_channel(head, me->channel, MULTICAST_SEND, me->nickname, payload, msg->pld_len, me->fd);
            return 0;
        }

        case MULTICAST_QUIT: {
            char *name = msg->infos;
            if (me->channel[0] == '\0' || strcmp(me->channel, name) != 0) {
                send_server_info(me->fd, "You are not in this channel.");
                return 0;
            }
            int count = 0;
            for (struct client_node *c = head; c; c = c->next) {
                if (c->channel[0] != '\0' && strcmp(c->channel, name) == 0) count++;
            }
            char text[MSG_LEN];
            if (count <= 1) {
                snprintf(text, sizeof(text), "You were the last user in this channel, %s has been destroyed", name);
            } else {
                char info[MSG_LEN];
                snprintf(info, sizeof(info), "%s has quit %s", me->nickname, name);
                notify_channel(head, name, info, me->fd);
                snprintf(text, sizeof(text), "You have quit %s", name);
            }
            me->channel[0] = '\0';

            send_to_fd(me->fd, MULTICAST_QUIT, "SERVER", name, text, (int)strlen(text) + 1);
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
		sfd = socket(rp->ai_family, rp->ai_socktype, rp->ai_protocol);
		if (sfd == -1) {
			continue;
		}
        int yes = 1;
        setsockopt(sfd, SOL_SOCKET, SO_REUSEADDR, &yes, sizeof(yes));
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
    if (recv_from_socket(sockfd, msg, sizeof(struct message)) != (int)sizeof(struct message)){
        return 1;
    }
    if (msg->pld_len < 0 || msg->pld_len > PROTO_MAX_PAYLOAD) {
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
    if (recv_from_socket(sockfd, *payload_out, msg->pld_len) != msg->pld_len){
        free(*payload_out);
        *payload_out = NULL;
        return 1;
    }
    //(*payload_out)[msg->pld_len - 1] = '\0';
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
	while(!stop){
        int nb_fds = poll(fds, FD_SIZE_TAB, -1);
        if (nb_fds < 0) {
            if (errno == EINTR){
                continue;
            }
            die(nb_fds, "Poll: ");
        }

        for (int i = 0 ; i < FD_SIZE_TAB ; i++){
            if (i == 0 && (fds[0].revents & POLLIN)){ 
				fds[i].revents = 0;
				len = sizeof(cli);
				if ((connfd = accept(sfd, (struct sockaddr*) &cli, &len)) < 0) {
					perror("accept()\n");
					exit(EXIT_FAILURE);
				}
				int fd_number = first_fds_available(fds, FD_SIZE_TAB);
                if (fd_number == -1) {
                    fprintf(stderr, "Too many clients => refusing %d\n", connfd);
                    close(connfd);
                    continue;
                }
                int ret_value = add_client(&head, connfd, cli, len);
                if (ret_value == -1){
                    close(connfd);
                    continue;
                } else {
                    fds[fd_number].fd = connfd;
                    fds[fd_number].events = POLLIN;
                    fds[fd_number].revents = 0;
                }
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
            if (i != 0 && fds[i].fd != -1 && (fds[i].revents & (POLLERR | POLLHUP | POLLNVAL))) {
                closing_connection(fds, i, &head);
            }
		}
	}
    for (int i = 1; i < FD_SIZE_TAB; i++) { // Cannot use closing_connection function. Only close, free (async signal safe)
        if (fds[i].fd >= 0) {
            close(fds[i].fd);
            fds[i].fd = -1;
        }
    }
	struct client_node *current = head; // same for free_list function. We need async signal safe functions only
    while (current != NULL) {
        struct client_node *next = current->next;
        free(current);
        current = next;
    }
}

int main(int argc, char** argv) {
	if (argc != 2){
        fprintf(stderr, "Must have only 1 arg, please type: ./server <SERVER_PORT>\n");
        exit(EXIT_FAILURE);
    }
    signal_stop_server_install();
    char* server_port = argv[1];
	int sfd;
	sfd = handle_bind(server_port);

	if ((listen(sfd, SOMAXCONN)) != 0) {
		perror("listen()\n");
		exit(EXIT_FAILURE);
	}
    server_loop(sfd);
	close(sfd);
    fprintf(stdout, "\nShutdown signal received. Closing all connections.\n");
	return EXIT_SUCCESS;
}

