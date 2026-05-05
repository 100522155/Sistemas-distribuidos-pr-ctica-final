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
#include <arpa/inet.h>
#include "claves.h"

#define PORT 5000
#define MAX_NAME 256

User *user_list = NULL;
pthread_mutex_t mutex = PTHREAD_MUTEX_INITIALIZER;

// Lee una cadena terminada en '\0' desde el socket. Devuelve la longitud (sin contar '\0') o -1 en error.
static int read_str(int sock, char *buf, int maxlen) { // read_str lee el byte desde el sock y guarda en el buffer un byte a la vez hasta encontrar un '\0' o alcanzar maxlen-1. Devuelve la longitud de la cadena leída (sin contar el '\0') o -1 en caso de error.
    int i = 0;
    while (i < maxlen - 1) {
        ssize_t r = read(sock, &buf[i], 1);
        if (r <= 0) { buf[i] = '\0'; return -1; }
        if (buf[i] == '\0') return i;
        i++;
    }
    buf[i] = '\0';
    return i;
} // se lee de byte en byte porque el protocolo define que cada cadena va terminada en '\0', y no se sabe de antemano su longitud. 
// El maxlen-1 es para asegurarse de dejar espacio para el '\0' final.

void handle_register(int socket) {
    char name[MAX_NAME];
    if (read_str(socket, name, MAX_NAME) < 0) return;

    uint8_t response;
    pthread_mutex_lock(&mutex);

    User *curr = user_list;
    while (curr != NULL) {
        if (strcmp(curr->name, name) == 0) {
            pthread_mutex_unlock(&mutex);
            response = 1;
            write(socket, &response, 1); // Usuario ya existe
            return;
        }
        curr = curr->next;
    }

    User *new_user = (User *)malloc(sizeof(User));
    if (new_user == NULL) {
        pthread_mutex_unlock(&mutex);
        response = 2;
        write(socket, &response, 1); // Error al crear el usuario
        return;
    }
    //Se ponen todos los nuevos campos a cero o a valores por defecto.
    strncpy(new_user->name, name, MAX_NAME);
    new_user->status       = 0;
    new_user->port         = 0;
    new_user->last_msg_id  = 0;
    new_user->pending_msgs = NULL;
    memset(new_user->ip, 0, sizeof(new_user->ip));
    new_user->next = user_list;
    user_list      = new_user;

    pthread_mutex_unlock(&mutex);
    response = 0;
    write(socket, &response, 1);
    printf("[REGISTER] Usuario '%s' registrado.\n", name);
}

void handle_unregister(int socket) {
    char name[MAX_NAME];
    if (read_str(socket, name, MAX_NAME) < 0) return;

    uint8_t response;
    pthread_mutex_lock(&mutex);

    // Buscar el usuario en la lista y eliminarlo
    User *curr = user_list, *prev = NULL;
    while (curr != NULL && strcmp(curr->name, name) != 0) {
        prev = curr;
        curr = curr->next;
    }

    // Si no se encuentra el usuario, responder con error
    if (curr == NULL) {
        pthread_mutex_unlock(&mutex);
        response = 1;
        write(socket, &response, 1);
        return;
    }

    // Al usuario posterior a curr se le asigna el siguiente de curr, y si curr es el primero de la lista, se actualiza user_list para que apunte al siguiente.
    if (prev == NULL) user_list  = curr->next; // Si curr es el primer usuario, se actualiza user_list para que apunte al siguiente.
    else              prev->next = curr->next; // Si curr no es el primero, se salta curr en la lista enlazada.

    //Guardar mensajes pendientes ANTES de free(curr) 
    Message *msg = curr->pending_msgs; 
    free(curr); //Liberar memoria del usuario eliminado

    while (msg != NULL) { //Liberar memoria de los mensajes pendientes del usuario eliminado
        Message *nxt = msg->next;
        free(msg);
        msg = nxt;
    }

    pthread_mutex_unlock(&mutex);
    response = 0;
    write(socket, &response, 1);
    printf("[UNREGISTER] Usuario '%s' eliminado.\n", name);
}

//Conexion de un usuario, se actualiza su estado a conectado y se le entregan los mensajes pendientes.
void handle_connect(int socket, char *client_ip) {
    char name[MAX_NAME]; //Nombre del usuario que se conecta
    char port_str[16]; //Puerto del cliente que se conecta, llega como string para simplificar el protocolo. Se convertirá a int con atoi.

    /* Puerto llega como string, no como int binario */
    if (read_str(socket, name,     MAX_NAME)        < 0) return; //leer hasta '\0' y guardar el nombre
    if (read_str(socket, port_str, sizeof(port_str)) < 0) return; //leer hasta '\0' y guardar el puerto como string
    int client_port = atoi(port_str);

    uint8_t response;
    pthread_mutex_lock(&mutex);

    //Buscar el usuario en la lista de usuarios registrados
    User *curr = user_list;
    while (curr != NULL && strcmp(curr->name, name) != 0)
        curr = curr->next;

    // Si no se encuentra el usuario, responder con error. Si el usuario ya está conectado, responder con otro error.
    if (curr == NULL) {
        pthread_mutex_unlock(&mutex);
        response = 1;
        write(socket, &response, 1);
        return;
    }
    // Si el usuario ya está conectado, no se permite conectar de nuevo (no se actualiza su IP ni puerto) y se responde con error.
    if (curr->status == 1) {
        pthread_mutex_unlock(&mutex);
        response = 2;
        write(socket, &response, 1);
        return;
    }
    //Asignamos campos al usuario (en register solo se crea y ya)
    curr->status = 1;
    strncpy(curr->ip, client_ip, 15); //IP del cliente, se copia hasta 15 caracteres para dejar espacio para el '\0' final. Se asume que client_ip es una cadena válida de IP.
    curr->ip[15] = '\0';
    curr->port   = client_port; //Puerto del cliente asignado al usuario

    char local_ip[16];
    strncpy(local_ip, curr->ip, 16); //Copiamos la ip local

    Message *msg       = curr->pending_msgs;
    curr->pending_msgs = NULL;
    pthread_mutex_unlock(&mutex);

    while (msg != NULL) {
        Message *nxt = msg->next;
        int r = send_message_to_client(local_ip, client_port,
                                       msg->sender, msg->id, msg->content); //Asignamos los datos para enviar el mensaje al cliente
        if (r == 0)
            printf("[CONNECT] Mensaje %u entregado a '%s'.\n", msg->id, name);
        else
            printf("[CONNECT] Error entregando mensaje %u a '%s'.\n", msg->id, name);
        free(msg);
        msg = nxt;
    }

    response = 0;
    write(socket, &response, 1);
    printf("[CONNECT] '%s' conectado desde %s:%d.\n", name, client_ip, client_port);
}

void handle_disconnect(int socket, char *client_ip) {
    (void)client_ip;
    char name[MAX_NAME];
    char port_str[16];

    /* Puerto llega como string */
    if (read_str(socket, name,     MAX_NAME)        < 0) return;
    if (read_str(socket, port_str, sizeof(port_str)) < 0) return; //Sacamos puerto y nombre

    uint8_t response;
    pthread_mutex_lock(&mutex);

    User *curr = user_list;
    while (curr != NULL && strcmp(curr->name, name) != 0)
        curr = curr->next;

    if (curr == NULL) {
        pthread_mutex_unlock(&mutex);
        response = 1;
        write(socket, &response, 1);
        return;
    }
    if (curr->status == 0) {
        pthread_mutex_unlock(&mutex);
        response = 2;
        write(socket, &response, 1);
        return;
    }

    //Deasignamos campos de conexion del usuario, todo menos el nombre

    curr->status = 0;
    memset(curr->ip, 0, sizeof(curr->ip));
    curr->port = 0;

    pthread_mutex_unlock(&mutex);
    response = 0;
    write(socket, &response, 1);
    printf("[DISCONNECT] Usuario '%s' desconectado.\n", name);
}

void handle_users(int socket) {
    char name[MAX_NAME];
    if (read_str(socket, name, MAX_NAME) < 0) return; //Leemos nombre de usuario 

    pthread_mutex_lock(&mutex);

    // Verificar que el solicitante está conectado
    User *req = user_list;
    while (req != NULL && strcmp(req->name, name) != 0)
        req = req->next;

        // Si el usuario no existe o no está conectado, responder con error
    if (req == NULL || req->status == 0) {
        pthread_mutex_unlock(&mutex);
        uint8_t r = 1;
        write(socket, &r, 1);
        return;
    }
    // Contar usuarios conectados
    uint32_t count = 0; //usuarios conectados, en formato uint32_t para enviar como string sin problemas de tamaño
    User *curr = user_list;
    while (curr != NULL) {
        if (curr->status == 1) count++;
        curr = curr->next;
    }

    // Código OK (1 byte) + count como string terminado en '\0' + lista de usuarios conectados (cada nombre terminado en '\0')
    uint8_t response = 0; //evitar problemas de formato utilizar uint8_t para el código de respuesta, aunque solo se usan 0,1,2, es más claro que usar un int. Se envía como un byte al cliente.
    write(socket, &response, 1);

    char count_str[16];
    snprintf(count_str, sizeof(count_str), "%u", count);
    write(socket, count_str, strlen(count_str) + 1);

    //Un nombre por usuario conectado, terminado en '\0'
    curr = user_list;
    while (curr != NULL) {
        if (curr->status == 1)
            write(socket, curr->name, strlen(curr->name) + 1);
        curr = curr->next;
    } //Escribimos en el socket el nombre de cada usuario conectado, terminado en '\0' para que el cliente sepa dónde termina cada nombre.


    pthread_mutex_unlock(&mutex);
}

void handle_send(int socket) {
    char sender_name[MAX_NAME];
    char dest_name[MAX_NAME];
    char message[MAX_MSG];

    //Orden: remitente, destinatario, mensaje 
    if (read_str(socket, sender_name, MAX_NAME) < 0) return;
    if (read_str(socket, dest_name,   MAX_NAME) < 0) return;
    if (read_str(socket, message,     MAX_MSG)  < 0) return;

    uint8_t response;
    pthread_mutex_lock(&mutex);

    static unsigned int global_msg_id = 0; //Contador global para asignar IDs únicos a los mensajes enviados. Se incrementa cada vez que se envía un mensaje, y si llega a 0 (desbordamiento), se reinicia a 1 para evitar usar el ID 0, que podría usarse para indicar ausencia de mensaje o error.

    User *receiver = user_list;
    while (receiver != NULL && strcmp(receiver->name, dest_name) != 0)
        receiver = receiver->next;

    User *sender = user_list;
    while (sender != NULL &&
           !(strcmp(sender->name, sender_name) == 0 && sender->status == 1))
        sender = sender->next;

    if (receiver == NULL || sender == NULL) {
        pthread_mutex_unlock(&mutex);
        response = 1;
        write(socket, &response, 1);
        return;
    }

    global_msg_id++; // Mensaje 1 de Ana a Luis, mensaje 2 de Luis a Ana, mensaje 3 de Ana a Carlos, etc.
    if (global_msg_id == 0) global_msg_id = 1; // Evitar ID 0, que podría usarse para indicar ausencia de mensaje o error.

    Message *new_msg = (Message *)malloc(sizeof(Message));
    if (new_msg == NULL) {
        pthread_mutex_unlock(&mutex);
        response = 2;
        write(socket, &response, 1);
        return;
    }
    new_msg->id   = global_msg_id; //Asignar id de mensaje global único
    new_msg->next = NULL; //siguiente mensaje por escribir todavía no existe, se asigna NULL
    strncpy(new_msg->sender,  sender_name, MAX_NAME); //se completan los campos
    strncpy(new_msg->content, message,     MAX_MSG);

    if (receiver->pending_msgs == NULL) {
        receiver->pending_msgs = new_msg; //Añadir mensaje a la lista si no hay ninguno pendiente
    } else {
        Message *tail = receiver->pending_msgs; //Si hay pendientes, buscamos el último para añadir el nuevo al final de la lista
        while (tail->next != NULL) tail = tail->next;
        tail->next = new_msg;
    }

    //Responder OK + ID al remitente
    response = 0;
    write(socket, &response, 1); //Respuesta de OK
    char id_str[16];
    snprintf(id_str, sizeof(id_str), "%u", global_msg_id); //convertir mensaje a string
    write(socket, id_str, strlen(id_str) + 1); //Enviar ID del mensaje al cliente, terminado en '\0' para que el cliente sepa dónde termina.

    //Copiar datos del receptor antes de soltar el mutex
    char receiver_ip[16];
    int  receiver_port   = receiver->port;
    int  receiver_status = receiver->status;
    unsigned int msg_id_saved = new_msg->id;
    strncpy(receiver_ip, receiver->ip, 16);
    receiver_ip[15] = '\0';
    char receiver_name[MAX_NAME];
    strncpy(receiver_name, receiver->name, MAX_NAME);

    if (receiver_status == 1) { //Si el cliente está ya conectado, se envia de inmediato
        pthread_mutex_unlock(&mutex);

        int send_result = send_message_to_client(receiver_ip, receiver_port,
                                                 sender_name, msg_id_saved, message); //Enviamos mensaje al cliente receptor
        pthread_mutex_lock(&mutex);

        if (send_result == 0) { //Si el mensaje se ha enviado correctamente, lo eliminamos de la lista de pendientes del receptor
            User *recv2 = user_list;
            while (recv2 != NULL && strcmp(recv2->name, receiver_name) != 0)
                recv2 = recv2->next;

            if (recv2 != NULL) {
                Message *cur = recv2->pending_msgs, *prev = NULL;
                while (cur != NULL) {
                    if (cur->id == msg_id_saved) {
                        if (prev == NULL) recv2->pending_msgs = cur->next;
                        else              prev->next           = cur->next;
                        free(cur);
                        break;
                    }
                    prev = cur;
                    cur  = cur->next;
                }
            }
        }
    }
    

    pthread_mutex_unlock(&mutex);
    printf("s> SEND MESSAGE %u FROM %s TO %s\n", msg_id_saved, sender_name, receiver_name);
}

int send_message_to_client(const char *ip, int port,
                            const char *sender, unsigned int msg_id,
                            const char *message) {
    int sock = socket(AF_INET, SOCK_STREAM, 0);
    if (sock < 0) { perror("socket"); return -1; }

    struct sockaddr_in addr;
    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_port   = htons(port);
    if (inet_pton(AF_INET, ip, &addr.sin_addr) <= 0) {
        perror("inet_pton"); close(sock); return -1;
    }
    if (connect(sock, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
        perror("connect"); close(sock); return -1;
    }

    // Protocolo que listen_thread espera:
    // "SEND_MESSAGE\0" + sender\0 + id\0 + message\0  
    write(sock, "SEND_MESSAGE", strlen("SEND_MESSAGE") + 1);
    write(sock, sender,  strlen(sender)  + 1);
    char id_str[32];
    snprintf(id_str, sizeof(id_str), "%u", msg_id);
    write(sock, id_str,  strlen(id_str)  + 1);
    write(sock, message, strlen(message) + 1);

    close(sock);
    return 0;
}

void handle_sendattach(int socket) {
    (void)socket;
    /* TODO */
}

//Quit 
void handle_quit(int socket) {
    char name[MAX_NAME];
    char port_str[16];

    if (read_str(socket, name,     MAX_NAME)        < 0) return;
    if (read_str(socket, port_str, sizeof(port_str)) < 0) return;

    pthread_mutex_lock(&mutex);

    User *curr = user_list;
    while (curr != NULL && strcmp(curr->name, name) != 0)
        curr = curr->next;

    if (curr == NULL) {
        pthread_mutex_unlock(&mutex);
        uint8_t r = 1;
        write(socket, &r, 1);
        return;
    }

    curr->status = 0; //Limpiamos los datos del usuario, dejando el nombre para que siga registrado pero sin datos de conexión ni mensajes pendientes. Si el usuario se conecta de nuevo, se le asignarán nuevos datos y mensajes pendientes vacía.
    memset(curr->ip, 0, sizeof(curr->ip));
    curr->port = 0;

    pthread_mutex_unlock(&mutex);
    printf("[QUIT] Usuario '%s' cerró la sesión.\n", name);
}