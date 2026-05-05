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

void handle_register(int socket) { //Se debe crear un socket para cada cliente que se conecta al servidor, y cada socket se maneja en un hilo separado, por lo que cada función de manejo de operaciones (como handle_register) se ejecuta en un hilo diferente para cada cliente. Esto permite que el servidor pueda atender a múltiples clientes simultáneamente sin bloquearse, ya que cada operación se maneja de forma independiente en su propio hilo.

    char name[MAX_NAME];
    if (read_str(socket, name, MAX_NAME) < 0) return;
    
    uint8_t response;
    pthread_mutex_lock(&mutex);

    User *curr = user_list; //buscamos el usuario en la lista para comprobar si ya existe, user_list = NULL al principio, y se va actualizando a medida que se añaden usuarios a la lista, siempre apuntando al primer usuario de la lista. Si el usuario que queremos registrar ya existe, respondemos con un error. Si no existe, lo añadimos al principio de la lista.
    while (curr != NULL) {
        if (strcmp(curr->name, name) == 0) {
            pthread_mutex_unlock(&mutex);
            response = 1; // usuario ya existe
            write(socket, &response, sizeof(uint8_t));
            return;
        }
        curr = curr->next;
    }

    // No existe y lo añadimos
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
    memset(new_user->ip, 0, sizeof(new_user->ip)); //liberamos la memoria de la IP, ya que el usuario no está conectado, y por lo tanto no tiene una IP asignada
    new_user->last_msg_id = 0;
    new_user->next = user_list; // Insertamos el nuevo usuario al principio de la lista
    user_list = new_user; // Actualizamos la cabecera de la lista para que apunte al nuevo usuario
 
    pthread_mutex_unlock(&mutex);
    response = 0; // OK
    write(socket, &response, sizeof(uint8_t));
    printf("[REGISTER] Usuario '%s' registrado.\n", name);
}


void handle_unregister(int socket) {
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
    
    //Liberar uno a uno los mensajes pendientes del usuario 
    Message *msg = curr->pending_msgs;
    while (msg != NULL) {
        Message *next = msg->next;
        free(msg);
        msg = next;
    }
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
    
    char local_ip[16];
    strncpy(local_ip, curr->ip, 16);
    //Cuando se conecten los usuarios, se les intentará entregar los mensajes pendientes que tengan en su lista de mensajes pendientes, y si el envío es exitoso, se eliminarán de la lista de pendientes. Si el envío falla, los mensajes seguirán en la lista de pendientes para futuros intentos de entrega.
    Message *msg = curr->pending_msgs;
    curr->pending_msgs = NULL;  // el nodo ya no tiene pendientes, pero no los perdemos porque tenemos el puntero msg apuntando al primer mensaje pendiente, y desde ahí podemos recorrer la lista de pendientes para intentar entregarlos uno por uno.
    
    
    pthread_mutex_unlock(&mutex);  // ← soltar antes de cualquier I/O

    // Procesar la lista local fuera del mutex
    if (msg != NULL) {
        printf("[CONNECT] Usuario '%s' tiene mensajes pendientes. Intentando entregar...\n", name);
    }

    while (msg != NULL) {
        Message *next = msg->next;  // guardar antes de cualquier otra cosa

        int send_result = send_message_to_client(local_ip, client_port,
                                                msg->sender, msg->id, msg->content);
        if (send_result == 0) {
            printf("[CONNECT] Mensaje pendiente ID %u entregado a '%s'.\n", msg->id, name);
        } else {
            printf("[CONNECT] Error al entregar mensaje ID %u a '%s'.\n", msg->id, name);
        }

        free(msg);   // siempre, independientemente del resultado
        msg = next;
    }
    response = 0; 

    // Responder al cliente (según el protocolo de la práctica)
    write(socket, &response, sizeof(uint8_t));
    printf("[CONNECT] Usuario '%s' conectado desde %s:%d.\n", name, client_ip, client_port);
}

void handle_disconnect(int socket, char *client_ip) {
    char name[MAX_NAME];
    int port_str;

    // 1. Leer los datos que envía el cliente (Nombre y Puerto)
    if (read(socket, name, MAX_NAME )<= 0) return;
    if (read(socket, &port_str, sizeof(int)) <= 0) return;
    int client_port = atoi(port_str);
    
    uint8_t response;
    // 2. BLOQUEAR la lista para actualizar el estado de forma segura
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
    char sender_name[MAX_NAME];
    if (recv(socket, sender_name, MAX_NAME, MSG_WAITALL) <= 0) return; // Leer el nombre del remitente
    char message[1024];
    if (recv(socket, message, 1024, MSG_WAITALL) <= 0) return; // Leer el mensaje del cliente
    char name[MAX_NAME];
    if (recv(socket, name, MAX_NAME, MSG_WAITALL) <= 0) return; // Leer el nombre del destinatario
    
    sender_name[MAX_NAME - 1] = '\0';
    name  [MAX_NAME - 1] = '\0';
    message    [MAX_MSG  - 1] = '\0';

    uint8_t response;

    pthread_mutex_lock(&mutex);
    static unsigned int global_msg_id = 0; // Contador global para asignar IDs únicos a los mensajes
    
    User *receiver = user_list;
    while (receiver != NULL) {
        if (strcmp(receiver->name, name) == 0) {
            // Usuario encontrado
            break;
        }
        receiver = receiver->next;
    }

    User *sender = user_list;
    while (sender != NULL) {
        if (sender->status == 1 && strcmp(sender->name, sender_name) == 0) {
            // Usuario encontrado y conectado
            break;
        }
        sender = sender->next;
    }
    if (receiver == NULL) {
        pthread_mutex_unlock(&mutex);
        response = 1; // Usuario no encontrado o no conectado
        write(socket, &response, sizeof(uint8_t));
        return;
    }
    // Asignar un ID único al mensaje utilizando el contador global
    if (sender != NULL) {
        global_msg_id++;
        if (global_msg_id == 0) global_msg_id = 1; // Evitar que el ID sea 0
    } else {
        pthread_mutex_unlock(&mutex);
        response = 1; // Usuario remitente no encontrado o no conectado
        write(socket, &response, sizeof(uint8_t));
        return;
    }


    //Preparar el mensaje para añadirlo a la lista de pendientes del receptor
    Message *new_msg = (Message *)malloc(sizeof(Message));
    if (new_msg == NULL) {
        pthread_mutex_unlock(&mutex);
        response = 2; // Error de memoria
        write(socket, &response, sizeof(uint8_t));
        return;
    }
    printf("mensaje id %u con contenido: %s\n", global_msg_id, message);
    new_msg->id = global_msg_id;
    strncpy(new_msg->sender, sender_name, MAX_NAME);
    strncpy(new_msg->content, message, MAX_MSG);
    new_msg->next = NULL;
    // Añadir el mensaje a la lista de mensajes pendientes del receptor
    if (receiver->pending_msgs == NULL) {
        receiver->pending_msgs = new_msg;
    } else {
        Message *curr = receiver->pending_msgs;
        while (curr->next != NULL) {
            curr = curr->next;
        }
        curr->next = new_msg;
    } //Hemos insertado en los mensajes pendientes del receptor el nuevo mensaje, ahora tenemos que enviarlo al cliente destino. Si el envío falla, el mensaje quedará pendiente para futuros intentos de entrega. Si el envío es exitoso, el mensaje se eliminará de la lista de pendientes.

    /*responder con todo ok*/
    response = 0;
    // Responder al remitente que el mensaje se ha recibido correctamente
    write(socket, &response, sizeof(uint8_t));  

    char receiver_name[MAX_NAME];
    strncpy(receiver_name, receiver->name, MAX_NAME);
    unsigned int msg_id_saved = new_msg->id;
    // Copiar datos del receptor en variables locales antes de soltar el mutex (no condiciones de carrera porque el receptor no puede cambiar mientras estamos dentro del mutex, pero es buena práctica para evitar posibles problemas si se modifica el código en el futuro)
    char receiver_ip[16];
    int  receiver_port;
    int  receiver_status;
    strncpy(receiver_ip, receiver->ip, 16);
    receiver_ip[15] = '\0';
    receiver_port  = receiver->port;
    receiver_status = receiver->status;
    //Si el usuario destino se encuentra conectado, intentar enviar de inmediato el mensaje
    if (receiver->status == 1){
        pthread_mutex_unlock(&mutex);
        //Aqui a lo mejor habría que corregir algunas cosillas, como por ejemplo el formato del mensaje que se envía al cliente destino, o el hecho de que el mensaje se elimina de la lista de pendientes solo si el envío es exitoso.
        int send_result = send_message_to_client(receiver_ip, receiver_port,
                                         sender_name, msg_id_saved, message);
        pthread_mutex_lock(&mutex);
        if (send_result == 0) {
            
            User *recv2 = user_list;
            while (recv2 != NULL && strcmp(recv2->name, receiver_name) != 0)
                recv2 = recv2->next;
            
            if (recv2 != NULL && recv2->status == 1) {
                // Envío exitoso, eliminar el mensaje de la lista de pendientes
                Message *curr = receiver->pending_msgs;
                Message *prev = NULL;
                while (curr != NULL) {
                    if (curr->id == new_msg->id) {
                        if (prev == NULL) {
                            receiver->pending_msgs = curr->next;
                        } else {
                            prev->next = curr->next;
                        }
                        free(curr);
                        break;
                    }
                    prev = curr;
                    curr = curr->next;
            }
        }
        }
    }
    pthread_mutex_unlock(&mutex);
    printf("s> SEND MESSAGE %u FROM %s TO %s\n",
                   msg_id_saved, sender_name, receiver_name);


}

int send_message_to_client(const char *ip, int port,
                           const char *sender, unsigned int msg_id,
                           const char *message){
    // Implementation for sending message to a specific client
    int sock = socket(AF_INET, SOCK_STREAM, 0); // Crear un socket para enviar el mensaje
    if (sock < 0) {
        perror("socket");
        return -1; // Error al crear el socket
    }
    struct sockaddr_in client_addr;
    memset(&client_addr, 0, sizeof(client_addr)); // Limpiar la estructura de dirección
    client_addr.sin_family = AF_INET;
    client_addr.sin_port = htons(port);
    if (inet_pton(AF_INET, ip, &client_addr.sin_addr) <= 0) { // Convertir la IP a formato binario
        perror("inet_pton");
        close(sock);
        return -1;

    }// Conectar al cliente destino
    if (connect(sock, (struct sockaddr *)&client_addr, sizeof(client_addr)) < 0 ) {
        perror("connect");
        close(sock);
        return -1; // Error al conectar
    }    
    write(sock, sender, strlen(sender)+1); // Enviar el nombre del remitente al cliente destino
    write(sock, "|", 2); // Separador entre la operación y el nombre del remitente
    char id_str[32];
    snprintf(id_str, sizeof(id_str), "%u", msg_id);
    write(sock, id_str, strlen(id_str) + 1);  // añadir esto antes del mensaje +1 para incluir el carácter nulo al final de la cadena para que el cliente destino pueda leer correctamente el ID del mensaje ya que el cliente destino espera recibir el ID del mensaje como una cadena de caracteres seguida de un carácter nulo para indicar el final de la cadena, y si no se incluye el carácter nulo, el cliente destino podría leer datos no válidos o causar un desbordamiento de búfer al intentar procesar el ID del mensaje.
    write(sock, "|", 2); // Separador entre la operación y el nombre del remitente
    write(sock, message, strlen(message) + 1); // Enviar el mensaje al cliente destino
    close(sock); // Cerrar el socket después de enviar el mensaje
    // Aquí se podría implementar una lógica para esperar una respuesta del cliente destino si es necesario
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
    //No se hace curr->next =NULL porque el usuario sigue existiendo en la lista, aunque esté desconectado, y queremos mantener su información (como los mensajes pendientes) para cuando se vuelva a conectar en el futuro. Si se hiciera curr->next = NULL, estaríamos rompiendo la lista de usuarios y perdiendo la información de los usuarios que están después del usuario que se desconecta en la lista.

    pthread_mutex_unlock(&mutex);
    
    printf("[QUIT] Cliente cerró la sesión.\n");

}