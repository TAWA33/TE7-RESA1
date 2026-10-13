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

struct file_transf_s {
    char client_receiver[NICK_LEN];
    char filepath[FILEPATH_LEN];
    int pending;
};

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
		return -1;
	}
	freeaddrinfo(result);
	return sfd;
}

int parse_ip_port(char *s, char *ip_out, size_t ip_size, char *port_out, size_t port_size)
{
    if (s[0] == '[') {// "[IPv6]:port"
        char *close = strchr(s, ']');
        if (!close || close[1] != ':'){
            return -1;
        }
        size_t hlen = (size_t)(close - s - 1);
        if (hlen == 0 || hlen >= ip_size){
            return -1;
        }
        memcpy(ip_out, s + 1, hlen);
        ip_out[hlen] = '\0';
        strncpy(port_out, close + 2, port_size - 1);
        port_out[port_size - 1] = '\0';
        return 0;
    }
    // "IPv4:port"
    char *sep = strrchr(s, ':');
    if (!sep){
        return -1;
    }
    size_t hlen = (size_t)(sep - s);
    if (hlen == 0 || hlen >= ip_size){
        return -1;
    }
    memcpy(ip_out, s, hlen);
    ip_out[hlen] = '\0';
    strncpy(port_out, sep + 1, port_size - 1);
    port_out[port_size - 1] = '\0';
    return 0;
}

void print_prompt(const char *nickname, const char *channel) {
    if (channel[0] != '\0') {
        printf("%s[%s]> ", nickname, channel);
    } else {
        printf("%s> ", nickname);
    }
    fflush(stdout);
}

int listening_socket_peer(int sockfd, struct message *client_msg, char *nickname){
    struct addrinfo hints, *result, *rp;
    int sfd;
    memset(&hints, 0, sizeof(struct addrinfo));
    hints.ai_family = AF_UNSPEC;
    hints.ai_socktype = SOCK_STREAM;
    hints.ai_flags = AI_PASSIVE;
    if (getaddrinfo(NULL, "0", &hints, &result) != 0) {
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
        if (rp->ai_family == AF_INET6) {
            int v6only = 0;
            setsockopt(sfd, IPPROTO_IPV6, IPV6_V6ONLY, &v6only, sizeof(v6only));
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

    struct sockaddr_storage actual;
    socklen_t alen = sizeof(actual);
    if (getsockname(sfd, (struct sockaddr*)&actual, &alen) < 0) {
        perror("getsockname");
        close(sfd);
        return -1;
    }
    char payload[64] = {0};
    if (actual.ss_family == AF_INET) {
        struct sockaddr_in *s = (struct sockaddr_in*)&actual;
        char ip[INET_ADDRSTRLEN] = {0};
        inet_ntop(AF_INET, &s->sin_addr, ip, sizeof(ip));
        if (strcmp(ip, "0.0.0.0") == 0){
            strcpy(ip, "127.0.0.1");
        }
        snprintf(payload, sizeof(payload), "%s:%u", ip, ntohs(s->sin_port));
    } else if (actual.ss_family == AF_INET6) {
        struct sockaddr_in6 *s = (struct sockaddr_in6*)&actual;
        char ip[INET6_ADDRSTRLEN] = {0};
        inet_ntop(AF_INET6, &s->sin6_addr, ip, sizeof(ip));
        if (strcmp(ip, "::") == 0){
            strcpy(ip, "::1");
        }
        snprintf(payload, sizeof(payload), "[%s]:%u", ip, ntohs(s->sin6_port));
    } else {
        close(sfd);
        return -1;
    }
    strncpy(client_msg->nick_sender, nickname, NICK_LEN - 1);
    client_msg->nick_sender[NICK_LEN - 1] = '\0';
    client_msg->pld_len = (int)strlen(payload) + 1;

    if ((listen(sfd, SOMAXCONN)) != 0) {
        perror("listen()\n");
        exit(EXIT_FAILURE);
    }
    send_on_socket(sockfd, client_msg, sizeof(struct message));
    send_on_socket(sockfd, payload, client_msg->pld_len);

    struct sockaddr_storage cli;
    int connfd;
    socklen_t len = sizeof(cli);
    if ((connfd = accept(sfd, (struct sockaddr*) &cli, &len)) < 0) {
        perror("accept()\n");
        exit(EXIT_FAILURE);
    }
    close(sfd);// Useless now
    return connfd;
}

int send_file_to_peer(int peer_fd, char *filepath) {
    FILE *fp = fopen(filepath, "rb");
    if (!fp){ 
        perror("fopen(send)"); 
        return -1; 
    }
    char *base = strrchr(filepath, '/');
    base = base ? base + 1 : filepath;

    char buffer[PROTO_MAX_PAYLOAD];
    size_t nread;
    long total = 0;

    while ((nread = fread(buffer, 1, sizeof(buffer), fp)) > 0) {
        struct message file_msg = {0};
        file_msg.type = FILE_SEND;
        file_msg.pld_len = (int)nread;
        strncpy(file_msg.infos, base, INFOS_LEN - 1);
        file_msg.infos[INFOS_LEN - 1] = '\0';

        send_on_socket(peer_fd, &file_msg, sizeof(struct message));
        send_on_socket(peer_fd, buffer, (int)nread);
        total += (long)nread;
    }
    fclose(fp);

    struct message end_msg = {0};
    end_msg.type = FILE_SEND;
    end_msg.pld_len = 0;
    strncpy(end_msg.infos, base, INFOS_LEN - 1);
    send_on_socket(peer_fd, &end_msg, sizeof(struct message));

    struct message ack = {0};
    int n = recv_from_socket(peer_fd, &ack, sizeof(struct message));
    if (n == (int)sizeof(struct message) && ack.type == FILE_ACK) {
        fprintf(stdout, "%s has received the file '%s' (%ld bytes).\n", ack.nick_sender[0] ? ack.nick_sender : "Receiver", base, total);
    } else {
        fprintf(stdout, "No ACK received.\n");
    }
    fflush(stdout);
    return 0;
}

int receive_file_from_peer(int peer_fd, char *base_name, char* my_nickmane, char *sender_nick) {
    char outpath[FILEPATH_LEN];
    snprintf(outpath, sizeof(outpath), "received_%s", base_name);

    FILE *fp = fopen(outpath, "wb");
    if (!fp) {
        perror("fopen(recv)"); 
        return -1;
    }
    fprintf(stdout, "Receiving file from %s...\n", sender_nick);
    fflush(stdout);

    long total = 0;
    int error = 0;
    while (1) {
        struct message file_msg;
        int n = recv_from_socket(peer_fd, &file_msg, sizeof(struct message));
        if (n != (int)sizeof(struct message)){
            error = 1;
            break;
        }
        if (file_msg.type != FILE_SEND){
            fprintf(stderr, "Invalid type of msg (must be FILE_SEND)\n");
            error = 1;
            break;
        }
        if (file_msg.pld_len < 0 || file_msg.pld_len > PROTO_MAX_PAYLOAD) {
            fprintf(stderr, "Invalid pld_len %d\n", file_msg.pld_len);
            error = 1;
            break;
        }
        if (file_msg.pld_len == 0){
            break;
        }
        char *data = malloc(file_msg.pld_len);
        if (!data){
            error = 1;
            break;
        }
        n = recv_from_socket(peer_fd, data, file_msg.pld_len);
        if (n != file_msg.pld_len) {
            free(data);
            error = 1;
            break;
        }
        fwrite(data, 1, file_msg.pld_len, fp);
        total += file_msg.pld_len;
        free(data);
    }
    fclose(fp);

    struct message ack = {0};
    ack.type = FILE_ACK;
    ack.pld_len = 0;
    strncpy(ack.infos, base_name, INFOS_LEN - 1);
    strncpy(ack.nick_sender, my_nickmane, NICK_LEN - 1);
    ack.nick_sender[NICK_LEN - 1] = '\0';
    strncpy(ack.infos, base_name, INFOS_LEN - 1);
    send_on_socket(peer_fd, &ack, sizeof(struct message));

    if (error){
        fprintf(stdout, "Transfer failed.\n");
    } else {
        fprintf(stdout, "File '%s' saved in '%s' (%ld bytes).\n", base_name, outpath, total);
    }
    fflush(stdout);
    return error ? -1 : 0;
}

int handle_file_request_received(int sockfd, struct message *server_msg, char *msg, char *nickname){
    fprintf(stdout, "%s wants you to accept the transfer of the file named '%s'. Do you accept? [Y/N]", server_msg->nick_sender, msg ? msg : "");
    fflush(stdout);

    char sender_nick[NICK_LEN];
    strncpy(sender_nick, server_msg->nick_sender, NICK_LEN - 1);
    sender_nick[NICK_LEN - 1] = '\0';

    char filepath[FILEPATH_LEN];
    strncpy(filepath, msg ? msg : "file", FILEPATH_LEN - 1);
    filepath[FILEPATH_LEN - 1] = '\0';

    char answer[8];
    if (fgets(answer, sizeof(answer), stdin) == NULL) {
        fprintf(stderr, "Error reading answer\n");
        return 0;
    }
    struct message client_msg = {0};
    strncpy(client_msg.infos, server_msg->nick_sender, NICK_LEN - 1);
    client_msg.infos[INFOS_LEN - 1] = '\0';

    if (answer[0] == 'Y' || answer[0] == 'y') {
        printf("Accepted\n");
        client_msg.type = FILE_ACCEPT;
        strncpy(client_msg.infos, server_msg->nick_sender, NICK_LEN - 1);

        int peer_fd = listening_socket_peer(sockfd, &client_msg, (char *)nickname);
        if (peer_fd >= 0) {
            char *base = strrchr(filepath, '/');
            base = base ? base + 1 : filepath;
            receive_file_from_peer(peer_fd, base, (char *)nickname, sender_nick);
            close(peer_fd);
        }
    } else {
        printf("Rejected\n");
        client_msg.type = FILE_REJECT;
        client_msg.pld_len = 0;
        if (send_on_socket(sockfd, &client_msg, sizeof(struct message)) != (int)sizeof(struct message)) {
            return 1;
        }
    }
    return 0;
}

int handle_file_accept_received(struct message *server_msg, char *msg, struct file_transf_s *fs){
    fprintf(stdout, "%s accepted file transfer.\n", server_msg->nick_sender);

    if (!fs->pending) {
        fprintf(stderr, "[Warning] FILE_ACCEPT received without FILE_REQUEST sent — ignored.\n");
        return 0;
    }
    if (strcmp(server_msg->nick_sender, fs->client_receiver) != 0) {
        fprintf(stderr, "[Warning] FILE_ACCEPT of '%s' instead of '%s' — ignored.\n",
                server_msg->nick_sender, fs->client_receiver);
        return 0;
    }
    if (!msg) {
        fprintf(stderr, "FILE_ACCEPT without peer address\n");
        return 0;
    }
    char file_ip[INET6_ADDRSTRLEN] = {0};
    char file_port[16] = {0};

    if (parse_ip_port(msg, file_ip, sizeof(file_ip), file_port, sizeof(file_port)) < 0) {
        fprintf(stderr, "Invalid peer address in FILE_ACCEPT\n");
        fs->pending = 0;
        return 0;
    }
    fprintf(stdout, "Connecting to %s:%s ...\n", file_ip, file_port);

    int peer_fd = handle_connect(file_port, file_ip);
    if (peer_fd < 0) {
        fprintf(stderr, "Could not connect to peer\n");
        fs->pending = 0;
        return 0;
    }
    fs->pending = 0;

    fprintf(stdout, "Sending file '%s' ...\n", fs->filepath);
    send_file_to_peer(peer_fd, fs->filepath);
    close(peer_fd);
    return 0;
}


void stdin_pollin(int sockfd, char* nickname, char* current_channel, struct file_transf_s* file_transf_s){
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
        if (strcmp(cmd, "/quit") == 0 && args[0] == '\0'){
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
        } else if (strcmp(cmd, "/send") == 0){ // args = "<pseudo> <file_path>"
            char *dest = args;
            char *file_path = strpbrk(args, " \t");

            if (file_path != NULL) {
                *file_path = '\0';
                file_path++;
                while (*file_path == ' ' || *file_path == '\t') file_path++;
            } else {
                file_path = args + strlen(args); // empty file_path
            }
            if (strcmp(dest, nickname) == 0) {
                fprintf(stderr, "Cannot send a file to yourself.\n");
                return;
            }
            FILE *file;
            if (!(file = fopen(file_path, "rb"))) {
                fprintf(stderr, "File doesn't exists or Access Impossible.\n");
                return;
            }
            if (file_transf_s->pending) {
                fprintf(stdout, "A transfer is already pending.\n");
                return;
            }
            fseek(file, 0, SEEK_END); // Compute file's size 
            int file_size = ftell(file);
            fseek(file, 0, SEEK_SET);
            printf("File's size : %d\n", file_size);
            fclose(file);

            client_msg.type = FILE_REQUEST;
            strncpy(client_msg.infos, dest, INFOS_LEN - 1);
            payload = file_path;
            client_msg.pld_len = strlen(file_path) + 1;

            strncpy(file_transf_s->client_receiver, dest, NICK_LEN - 1);
            file_transf_s->client_receiver[NICK_LEN - 1] = '\0';
            strncpy(file_transf_s->filepath, file_path, sizeof(file_transf_s->filepath) - 1);
            file_transf_s->filepath[sizeof(file_transf_s->filepath) - 1] = '\0';
            file_transf_s->pending = 1;
        } else if (strcmp(cmd, "/create") == 0) {
            client_msg.type = MULTICAST_CREATE;
            strncpy(client_msg.infos, args, INFOS_LEN - 1);
            client_msg.infos[INFOS_LEN - 1] = '\0';
        } else if (strcmp(cmd, "/channel_list") == 0) {
            client_msg.type = MULTICAST_LIST;
            client_msg.infos[0] = '\0';
        } else if (strcmp(cmd, "/join") == 0) {
            client_msg.type = MULTICAST_JOIN;
            strncpy(client_msg.infos, args, INFOS_LEN - 1);
            client_msg.infos[INFOS_LEN - 1] = '\0';
        } else if (strcmp(cmd, "/quit") == 0) {
            client_msg.type = MULTICAST_QUIT;
            strncpy(client_msg.infos, args, INFOS_LEN - 1);
            client_msg.infos[INFOS_LEN - 1] = '\0';
        } else { // Unknown cmd => ECHO
            client_msg.type = ECHO_SEND;
            client_msg.pld_len = strlen(buff) + 1;
            payload = buff;
        }
    } else { // No cmd : ECHO
        if (current_channel[0] != '\0') {
            client_msg.type = MULTICAST_SEND;
            strncpy(client_msg.infos, current_channel, INFOS_LEN - 1);
            client_msg.infos[INFOS_LEN - 1] = '\0';
            client_msg.pld_len = strlen(buff) + 1;
            payload = buff;
        } else {
            client_msg.type = ECHO_SEND;
            client_msg.pld_len = strlen(buff) + 1;
            payload = buff;
        }
    }
    strncpy(client_msg.nick_sender, nickname, NICK_LEN - 1);
    client_msg.infos[INFOS_LEN - 1] = '\0';

    //printf("STRUCT MSG : %d / %s / %s / %s\n", client_msg.pld_len, client_msg.nick_sender, msg_type_str[client_msg.type], client_msg.infos);

    send_on_socket(sockfd, &client_msg, sizeof(struct message));

    if (client_msg.pld_len > 0 && payload != NULL) {
        send_on_socket(sockfd, payload, client_msg.pld_len);
    }
}


int server_socket_pollin(int sockfd, char *nickname, char *current_channel, struct file_transf_s* file_transf_s) {
    struct message server_msg = {0};
    int r = recv_from_socket(sockfd, &server_msg, sizeof(struct message));

    //printf("STRUCT MSG : %d / %s / %s / %s\n", server_msg.pld_len, server_msg.nick_sender, msg_type_str[server_msg.type], server_msg.infos);
    if (r != (int)sizeof(struct message)) {
        fprintf(stdout, "Server closed the connection\n");
        return 1;
    }
    char* msg = NULL; // It's the payload
    if (server_msg.pld_len < 0 || server_msg.pld_len > PROTO_MAX_PAYLOAD){
        return 1;
    } else if (server_msg.pld_len > 0){
        msg = malloc(server_msg.pld_len + 1);
        if (msg == NULL){
		    return 0;
	    }
        r = recv_from_socket(sockfd, msg, server_msg.pld_len);
        if (r != server_msg.pld_len) {
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
            strncpy(nickname, server_msg.infos, NICK_LEN - 1);
            nickname[NICK_LEN - 1] = '\0';
            fprintf(stdout, "[Server-%s] : %s\n", msg_type_str[ECHO_SEND], msg ? msg : "");
            break;
        case UNICAST_SEND:
            fprintf(stdout, "[%s-%s] : %s\n", server_msg.nick_sender, msg_type_str[server_msg.type], msg ? msg : "");
            break;
        case BROADCAST_SEND:
            if (server_msg.nick_sender[0] != '\0')
                fprintf(stdout, "[%s-%s] : %s\n", server_msg.nick_sender, msg_type_str[server_msg.type], msg ? msg : "");
            else
                fprintf(stdout, "[Server-%s] : %s\n", msg_type_str[ECHO_SEND], msg ? msg : "");
            break;
        case FILE_REQUEST: // Client receiver
            if (handle_file_request_received(sockfd, &server_msg, msg, nickname)){
                return 1;
            }
            break;
        case FILE_REJECT:
            fprintf(stdout, "%s refused the file transfer.\n", server_msg.nick_sender);
            fflush(stdout);
            if (strcmp(server_msg.nick_sender, file_transf_s->client_receiver) == 0) {
                file_transf_s->pending = 0;
            }
            break;
        
        case FILE_ACCEPT: { // Sender Client
            if (handle_file_accept_received(&server_msg, msg, file_transf_s)){
                return 1;
            }
            break;
        }
        case MULTICAST_CREATE:
        case MULTICAST_JOIN:
            if (strcmp(server_msg.nick_sender, "SERVER") == 0) {
                strncpy(current_channel, server_msg.infos, NICK_LEN - 1);
                current_channel[NICK_LEN - 1] = '\0';
                fprintf(stdout, "%s\n", msg ? msg : "");
            }
            break;
        case MULTICAST_QUIT:
            if (strcmp(server_msg.nick_sender, "SERVER") == 0) {
                current_channel[0] = '\0';
                fprintf(stdout, "%s\n", msg ? msg : "");
            }
            break;
        case MULTICAST_SEND:
            if (strcmp(server_msg.nick_sender, "SERVER") == 0) {
                fprintf(stdout, "SERVER> %s\n", msg ? msg : "");
            } else {
                fprintf(stdout, "%s> %s\n", server_msg.nick_sender, msg ? msg : "");
            }
            break;
        case MULTICAST_LIST:
            fprintf(stdout, "Channels:\n%s\n", msg ? msg : "");
            break;
        case ECHO_SEND:
        default:
            fprintf(stdout, "[Server-%s] : %s\n", msg_type_str[ECHO_SEND], msg ? msg : "");
            break;
    }
    free(msg);
    return 0;
}


void client_loop(int socket_fd, char* nickname){
    char current_channel[NICK_LEN] = {0};
    struct pollfd fds[2]; // SIZE 2 : STDIN_FILENO / socket_fd

	fds[0].fd = STDIN_FILENO;
    fds[0].events = POLLIN;
    fds[0].revents = 0;

	fds[1].fd = socket_fd;
    fds[1].events = POLLIN;
    fds[1].revents = 0;

	printf("Enter /nick <nickname>: ");
    fflush(stdout); // To flush stdout. If not the buffer is not initialised correctly after

    struct file_transf_s file_transf_s = {0};

	while(1){
		int nb_fds = poll(fds, 2, -1);
        die(nb_fds, "Poll: ");
        if (fds[0].revents & POLLIN){ // STDIN_FILENO
			stdin_pollin(socket_fd, nickname, current_channel, &file_transf_s);
			
		} else if (fds[1].revents & POLLIN){ // socket_fd
			int server_resp = server_socket_pollin(socket_fd, nickname, current_channel, &file_transf_s);
			if (server_resp){
				break;
			}
		}
        printf("\n");
        print_prompt(nickname, current_channel);
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