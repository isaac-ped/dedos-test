#ifndef COMMUNICATION_H
#define COMMUNICATION_H

#include <unistd.h>
#include <stdbool.h>
#include <netinet/ip.h>

int read_payload(int fd, size_t size, void *buff);
ssize_t send_to_endpoint(int fd, void *data, size_t data_len);
int init_bound_socket(int port);
int init_listening_socket(int port);
int init_connected_socket(struct sockaddr_in *addr);

#endif