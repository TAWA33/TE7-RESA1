#include "common.h"

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/socket.h>

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