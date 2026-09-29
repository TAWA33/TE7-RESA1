#ifndef COMMON_H
#define COMMON_H

#define MSG_LEN 1024
#define FD_SIZE_TAB 128 // Size of the poll table (server)
#define QUIT "/quit\n"
//#define SERV_PORT "8080" // In parameter bc it's an argument
//#define SERV_ADDR "127.0.0.1"

struct header{
    int size;
    char username[128];
    int type;
};

struct info{
    short s;
    long l;
};

void die(int ret, char* msg);
int send_on_socket(int socket_fd, void* buf, int size);
int recv_from_socket(int socket_fd, void* buf, int size);

#endif