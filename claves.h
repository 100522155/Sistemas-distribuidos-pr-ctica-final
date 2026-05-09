#ifndef CLAVES_H
#define CLAVES_H

#include <stdint.h>
#include <pthread.h>

#define MAX_NAME 256
#define MAX_MSG  256
#define MAX_FILE 256

// Estructura de los mensajes pendientes de entrega 
typedef struct Message {
    unsigned int    id;                 // identificador del mensaje
    char            sender[MAX_NAME];   // nombre del usuario que envió el mensaje
    char            content[MAX_MSG];   // contenido del mensaje
    char            filename[MAX_FILE]; // nombre del fichero adjunto
    struct Message *next;               // puntero al siguiente mensaje en la lista de pendientes
} Message;

// Estructura de los usuarios registrados
typedef struct User {
    char name[MAX_NAME];       // nombre de usuario
    char ip[16];               // IP del cliente
    int port;                  // puerto donde escucha el cliente
    int status;                // 0 = desconectado, 1 = conectado
    unsigned int last_msg_id;  // último id asignado a un mensaje enviado por este usuario
    Message  *pending_msgs;    // lista de mensajes pendientes de entrega a este usuario
    struct User *next;         // puntero al siguiente usuario en la lista de usuarios registrados
} User;

// Funciones que reciben el socket y la ip del cliente
void handle_register(int socket);
void handle_unregister(int socket);
void handle_connect(int socket, char *client_ip);
void handle_disconnect(int socket, char *client_ip);
void handle_users(int socket);

// Funciones que solo reciben el socket
void handle_send(int socket);
void handle_sendattach(int socket);
void handle_quit(int socket);

// Funciones auxiliares para enviar mensajes a los clientes
int send_attach_to_client(const char *ip, int port, const char *sender, unsigned int msg_id, const char *message, const char *filename);
int send_message_to_client(const char *ip, int port, const char *sender, unsigned int msg_id, const char *message);

#endif