#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <pthread.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <fcntl.h>
#include <sys/sendfile.h>
#include <sys/stat.h>
#include "claves.h"
#define PORT 5000 //Puerto a utilizar

// Tenemos que almacenar tuplas con la forma <key-value1-value2-value3>, 
// y lo haremos con listas enlazadas, donde nuestro Nodo sera:
#define MAX_NAME 256

User *user_list = NULL;       // Cabecera de la lista de usuarios
//Declaramos un mutex para proteger las secciones críticas de las funciones
pthread_mutex_t mutex = PTHREAD_MUTEX_INITIALIZER;

void handle_register(int socket) {
    char name[MAX_NAME];
    if (read(socket, name, MAX_NAME) <= 0) return;
    name[MAX_NAME - 1] = '\0'; // seguridad

    uint8_t response;
    pthread_mutex_lock(&mutex);

    User *curr = user_list;
    while (curr != NULL) {
        if (strcmp(curr->name, name) == 0) {
            pthread_mutex_unlock(&mutex);
            response = 1; // usuario ya existe
            write(socket, &response, sizeof(uint8_t));
            return;
        }
        curr = curr->next;
    }

    // No existe → lo añadimos
    User *new_user = (User *)malloc(sizeof(User));
    if (new_user == NULL) {
        pthread_mutex_unlock(&mutex);
        response = 2; // error de memoria
        write(socket, &response, sizeof(uint8_t));
        return;
    }
    strncpy(new_user->name, name, MAX_NAME);
    new_user->status = 0;
    new_user->port   = 0;
    memset(new_user->ip, 0, sizeof(new_user->ip));
    new_user->next = user_list;
    user_list = new_user;

    pthread_mutex_unlock(&mutex);
    response = 0; // OK
    write(socket, &response, sizeof(uint8_t));
    printf("[REGISTER] Usuario '%s' registrado.\n", name);
}


void handle_unregister(int socket, char *client_ip) {
    char name[MAX_NAME];
    if (read(socket, name, MAX_NAME) <= 0) return;
    name[MAX_NAME - 1] = '\0';

    uint8_t response;
    pthread_mutex_lock(&mutex);

    User *curr = user_list;
    User *prev = NULL;
    while (curr != NULL && strcmp(curr->name, name) != 0) {
        prev = curr;
        curr = curr->next;
    }

    if (curr == NULL) {
        pthread_mutex_unlock(&mutex);
        response = 1; // no existe → USER_ERROR
        write(socket, &response, sizeof(uint8_t));
        return;
    }

    if (prev == NULL)
        user_list = curr->next;
    else
        prev->next = curr->next;
    free(curr);

    pthread_mutex_unlock(&mutex);
    response = 0; // OK
    write(socket, &response, sizeof(uint8_t));
    printf("[UNREGISTER] Usuario '%s' eliminado.\n", name);
}

void handle_connect(int socket, char *client_ip) {
    char name[MAX_NAME];
    int client_port;

    // Leer los datos que envía el cliente (Nombre y Puerto)
    if (read(socket, name, MAX_NAME) <= 0) return;
    if (read(socket, &client_port, sizeof(int)) <= 0) return; // Leer el puerto del cliente
    client_port = ntohl(client_port); // Convertir de red a formato local

    uint8_t response;
    // BLOQUEAR la lista para actualizar el estado de forma segura
    pthread_mutex_lock(&mutex);

    User *curr = user_list;
    while (curr != NULL && strcmp(curr->name, name) != 0) {
        curr = curr->next;
    }
    
    if (curr == NULL) {
        pthread_mutex_unlock(&mutex);
        response = 1; // no registrado → USER_ERROR
        write(socket, &response, sizeof(uint8_t));
        return;
    }
    if (curr->status == 1) {
        pthread_mutex_unlock(&mutex);
        response = 2; // ya conectado
        write(socket, &response, sizeof(uint8_t));
        return;
    }
    curr->status = 1;
    strncpy(curr->ip, client_ip, 15);
    curr->ip[15] = '\0';
    curr->port = client_port;

    pthread_mutex_unlock(&mutex);
    response = 0; 

    // Responder al cliente (según el protocolo de la práctica)
    write(socket, &response, sizeof(uint8_t));
    printf("[CONNECT] Usuario '%s' conectado desde %s:%d.\n", name, client_ip, client_port);
}

void handle_disconnect(int socket, char *client_ip) {
    char name[MAX_NAME];
    int client_port;

    // 1. Leer los datos que envía el cliente (Nombre y Puerto)
    if (read(socket, name, MAX_NAME )<= 0) return;
    if (read(socket, &client_port, sizeof(int)) <= 0) return;
    client_port = ntohl(client_port); // Convertir de red a formato local
    
    uint8_t response;
    // 2. BLOQUEAR la lista para actualizar el estado de forma segura
    pthread_mutex_lock(&mutex);
    int result = 0; // 0 = OK, 1 = Usuario existe, 2 = Error

    User *curr = user_list;
    while (curr != NULL && strcmp(curr->name, name) != 0) {
        curr = curr->next;
    }
    if (curr == NULL) {
        pthread_mutex_unlock(&mutex);
        response = 1; // no registrado → USER_ERROR
        write(socket, &response, sizeof(uint8_t));
        return;
    }
    if (curr->status == 0) {
        pthread_mutex_unlock(&mutex);
        response = 2; // ya desconectado → ERROR
        write(socket, &response, sizeof(uint8_t));
        return;
    }

    curr->status = 0;
    memset(curr->ip, 0, sizeof(curr->ip));

    pthread_mutex_unlock(&mutex);
    response = 0; // OK
    write(socket, &response, sizeof(uint8_t));
    printf("[DISCONNECT] Usuario '%s' desconectado.\n", name);
}

void handle_users(int socket) {
    char name[MAX_NAME];
    if (recv(socket, name, MAX_NAME, MSG_WAITALL) <= 0) return;

    uint8_t response = 0;
    uint32_t count = 0;

    pthread_mutex_lock(&mutex);

    User *curr = user_list;
    while (curr != NULL) {
        if (curr->status == 1) count++;
        curr = curr->next;
    }

    uint32_t count_net = htonl(count);
    write(socket, &response, sizeof(uint8_t));
    write(socket, &count_net, sizeof(uint32_t));

    curr = user_list;
    while (curr != NULL) {
        if (curr->status == 1) {
            char buffer[512];
            int len = snprintf(buffer, sizeof(buffer), "%s %s %d\n",
                               curr->name, curr->ip, curr->port);
            write(socket, buffer, len);
        }
        curr = curr->next;
    }

    pthread_mutex_unlock(&mutex);
}

void handle_send(int socket) {
    char name[MAX_NAME];
    if (recv(socket, name, MAX_NAME, MSG_WAITALL) <= 0) return; // Leer el nombre del destinatario
    char message[1024];
    if (recv(socket, message, 1024, MSG_WAITALL) <= 0) return; // Leer el mensaje del cliente
    char sender_name[MAX_NAME];
    if (recv(socket, sender_name, MAX_NAME, MSG_WAITALL) <= 0) return; // Leer el nombre del remitente
    uint8_t response = 0;

    pthread_mutex_lock(&mutex);

    User *curr = user_list;
    while (curr != NULL) {
        if (curr->status == 1 && strcmp(curr->name, name) == 0) {
            // Usuario encontrado y conectado
            break;
        }
        curr = curr->next;
    }
    if (curr == NULL) {
        pthread_mutex_unlock(&mutex);
        response = 1; // Usuario no encontrado o no conectado
        write(socket, &response, sizeof(uint8_t));
        return;
    }

    response = send_message_to_client(curr->ip, curr->port, message, sender_name);

    // Aquí se implementaría la lógica para enviar el mensaje al cliente destino
    // Por simplicidad, asumimos que el mensaje se envía correctamente
    pthread_mutex_unlock(&mutex);
    write(socket, &response, sizeof(uint8_t)); // Enviar respuesta al cliente (0 = OK, 1 = Usuario no encontrado, 2 = Error)
}

int send_message_to_client(const char *ip, int port, const char *message, char *name) {
    // Implementation for sending message to a specific client
    int sock = socket(AF_INET, SOCK_STREAM, 0); // Crear un socket para enviar el mensaje
    if (sock < 0) {
        perror("socket");
        return 2; // Error al crear el socket
    }
    struct sockaddr_in client_addr;
    client_addr.sin_family = AF_INET;
    client_addr.sin_port = htons(port);
    if (inet_pton(AF_INET, ip, &client_addr.sin_addr) <= 0) { // Convertir la IP a formato binario
        perror("inet_pton");
        close(sock);
        return 2;

    }// Conectar al cliente destino
    if (connect(sock, (struct sockaddr *)&client_addr, sizeof(client_addr)) < 0 ) {
        perror("connect");
        close(sock);
        return 2; // Error al conectar
    }
    write(sock, name, MAX_NAME); 
    write(sock, message, 1024);
    close(sock); // Cerrar el socket después de enviar el mensaje
    return 0;
}


void handle_sendattach(int socket) {
    // TODO
}

void handle_quit(int socket) {
    char name[MAX_NAME];
    int client_port;
    if (read(socket, name, MAX_NAME )<= 0) return;
    if (read(socket, &client_port, sizeof(int)) <= 0) return;
    client_port = ntohl(client_port); // Convertir de red a formato local
    
    pthread_mutex_lock(&mutex);
    User *curr = user_list;
    while (curr != NULL && strcmp(curr->name, name) != 0) {
        curr = curr->next;
    }

    if (curr == NULL) {
        pthread_mutex_unlock(&mutex);
        uint8_t response = 1; // no existe → USER_ERROR
        write(socket, &response, sizeof(uint8_t));
        return;
    }
    curr->status = 0;
    memset(curr->ip, 0, sizeof(curr->ip));
    curr->port = 0; // Limpiar la información del usuario
    curr->next = NULL;

    pthread_mutex_unlock(&mutex);
    
    printf("[QUIT] Cliente cerró la sesión.\n");

}