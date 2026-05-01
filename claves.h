#ifndef CLAVES_H
#define CLAVES_H

#include <stdint.h>
#include <pthread.h>

#define MAX_NAME 256

typedef struct User {
    char name[MAX_NAME];   // nombre de usuario
    char ip[16];           // IP del cliente
    int port;              // puerto donde escucha
    int status;            // 0: desconectado, 1: conectado
    struct User *next;
} User;

// Funciones que reciben Socket e IP
void handle_register(int socket);
void handle_unregister(int socket, char *client_ip);
void handle_connect(int socket, char *client_ip);
void handle_disconnect(int socket, char *client_ip);
void handle_users(int socket);

// Funciones que solo reciben Socket
void handle_send(int socket);
void handle_sendattach(int socket);
void handle_quit(int socket);

#endif