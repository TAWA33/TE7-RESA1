#ifndef COMMON_H
#define COMMON_H

#define MSG_LEN 1024
#define PROTO_MAX_PAYLOAD 65536
#define FD_SIZE_TAB 128 // Size of the poll table (server)
#define QUIT "/quit\n"
#define FILE_TRANSFERT_PORT "8083" 
// Static port because in a real deployment no interference of port (Same port but IP add differents). 
// In local, If 2 clients want to tranfert a file in the same time. It can cause problem (only 1 socket). 
#define FILE_TRANSFERT_ADDR "127.0.0.1"
#define FILEPATH_LEN 1024

void die(int ret, char* msg);
int send_on_socket(int socket_fd, void* buf, int size);
int recv_from_socket(int socket_fd, void* buf, int size);

#endif