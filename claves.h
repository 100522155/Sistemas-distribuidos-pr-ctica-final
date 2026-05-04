#ifndef CLAVES_H
#define CLAVES_H

#include <stdint.h>
#include <pthread.h>

#define MAX_NAME 256
#define MAX_MSG  256

/* Mensaje pendiente de entrega */
typedef struct Message {
    unsigned int    id;              // identificador del mensaje
    char            sender[MAX_NAME];// quien lo envió
    char            content[MAX_MSG];// contenido
    struct Message *next;
} Message;

typedef struct User {
    char name[MAX_NAME];   // nombre de usuario
    char ip[16];           // IP del cliente
    int port;              // puerto donde escucha
    int status;            // 0: desconectado, 1: conectado
    unsigned int last_msg_id;   // último id asignado a un mensaje ENVIADO por este usuario
    Message  *pending_msgs;     // lista de mensajes pendientes de entrega A este 
    struct User *next;
} User;

// Funciones que reciben Socket e IP
void handle_register(int socket);
void handle_unregister(int socket);
void handle_connect(int socket, char *client_ip);
void handle_disconnect(int socket, char *client_ip);
void handle_users(int socket);

// Funciones que solo reciben Socket
void handle_send(int socket);
void handle_sendattach(int socket);
void handle_quit(int socket);

int  send_message_to_client(const char *ip, int port,
                             const char *sender, unsigned int msg_id,
                             const char *message);
#endif