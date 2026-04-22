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

#define PORT 5000 //Puerto a utilizar
#define BUFFER_SIZE 1024 //tamaño del buffer para recibir datos
#define NUM_THREADS 8 //Número de hilos trabajadores


typedef struct { // guarda el descriptor del socket del cliente
    int socket;
    char ip[16]; // IP del cliente 
} task_t;

task_t task_queue[256]; // Cola de tareas para clientes
int queue_size = 0; //Contador de clientes
int queue_capacity = 256; //Cuantos clientes caben
int front = 0; //Es donde el main "lee" (saca) la siguiente conexión para atenderla. Siempre apunta a la siguiente tarea a procesar.
int rear = -1; //Es donde el main "escribe" (añade) la nueva conexión.

pthread_mutex_t queue_mutex = PTHREAD_MUTEX_INITIALIZER; // Mutex para proteger el acceso a la cola de tareas
pthread_cond_t queue_cond = PTHREAD_COND_INITIALIZER; // Condición para notificar a los hilos trabajadores de que hay tareas disponibles

void handle_register(int socket, char *client_ip);
void handle_unregister(int socket, char *client_ip);
void handle_connect(int socket, char *client_ip);
void handle_disconnect(int socket, char *client_ip);
void handle_users(int socket, char *client_ip);
void handle_send(int socket);
void handle_sendattach(int socket);
void handle_quit(int socket);

// Función para añadir tareas a la cola
void enqueue(task_t new_task) {
    pthread_mutex_lock(&queue_mutex); // Bloqueamos el mutex para modificar la cola de tareas
    if (queue_size < queue_capacity) { 
        rear = (rear + 1) % queue_capacity; // Incrementamos rear de forma circular
        task_queue[rear] = new_task; // Añadimos el nuevo cliente a la cola
        queue_size++; // Incrementamos el contador de tareas
        pthread_cond_signal(&queue_cond);
    }
    pthread_mutex_unlock(&queue_mutex);
}

//Función para sacar tareas de la cola
task_t dequeue() {
    pthread_mutex_lock(&queue_mutex); // Bloqueamos el mutex para modificar la cola de tareas
    while (queue_size == 0) { // Si no hay tareas, esperamos a que se añadan
        pthread_cond_wait(&queue_cond, &queue_mutex);
    }
    task_t task = task_queue[front]; // Obtenemos la tarea de la cola
    front = (front + 1) % queue_capacity; // Incrementamos front de forma circular
    queue_size--; // Decrementamos el contador de tareas
    pthread_mutex_unlock(&queue_mutex);
    return task; // Devolvemos la tarea de la cola
}

void *thread_function(void *arg) {
    while (1) {
        task_t task = dequeue(); 
        int client_socket = task.socket;
        char client_ip[16];
        strcpy(client_ip, task.ip);

        int exit_loop = 0;
        while (!exit_loop) {
            uint8_t op_code;
            ssize_t bytes = read(client_socket, &op_code, sizeof(op_code));
            
            if (bytes <= 0) {
                // El cliente cerró la conexión inesperadamente
                printf("[Hilo %ld] Cliente %s desconectado (EOF/error).\n",
                       pthread_self(), task.ip);
                exit_loop = 1;
                break;
            }
            switch (op_code) {

            case 0: //REGISTER
                handle_register(client_socket,task.ip);
                /* code */
                break;
            case 1: //UNREGISTER
                handle_unregister(client_socket,task.ip);
                /* code */
                break;
            case 2: //CONNECT
                handle_connect(client_socket,task.ip);
                //Si devuelve 0, el cliente se ha conectado correctamente. Si devuelve -1, el cliente no estaba registrado. 
                break;
            case 3: //DISCONNECT
                handle_disconnect(client_socket,task.ip);
                exit_loop = 1;
                break;
            case 4: //USERS
                handle_users(client_socket,task.ip);
                /* code */
                break;
            case 5: //SEND
                handle_send(client_socket);
                /* code */
                break;
            case 6: //SENDATTACH
                handle_sendattach(client_socket);
                /* code */
                break;
            case 7: //QUIT
                handle_quit(client_socket);
                exit_loop = 1;
                /* code */
                break;

            default: //ERROR
                break;
            }
        }

        close(client_socket);
        printf("[Hilo %ld] Socket %d cerrado.\n", pthread_self(), client_socket);
    }
    return NULL;
}


int main() {
    int server_socket, client_socket;
    struct sockaddr_in server_addr, client_addr; //struct de tres clases: Familia (IPv4), IP y Puerto.
    socklen_t client_addr_len = sizeof(client_addr); // Variable para almacenar el tamaño de la dirección del cliente
    pthread_t threads[NUM_THREADS];
    
    // Creamos el socket del servidor
    if ((server_socket = socket(AF_INET, SOCK_STREAM, 0)) == -1) { // AF_INET: IPv4, SOCK_STREAM: TCP
        perror("socket");
        exit(EXIT_FAILURE);
    }

    int opt = 1;
    setsockopt(server_socket, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

    // Configuramos la dirección del servidor
    memset(&server_addr, 0, sizeof(server_addr)); // Limpiamos la estructura de la dirección del servidor
    server_addr.sin_family = AF_INET; // Familia de direcciones (IPv4)
    server_addr.sin_addr.s_addr = INADDR_ANY; // Aceptamos conexiones en cualquier interfaz de red
    server_addr.sin_port = htons(PORT); // Convertimos el número de puerto a formato de red

    // Enlazamos el socket a la dirección del servidor
    if (bind(server_socket, (struct sockaddr *)&server_addr, sizeof(server_addr)) == -1) {
        perror("bind");
        close(server_socket);
        exit(EXIT_FAILURE);
    }

    // Escuchamos por conexiones entrantes
    if (listen(server_socket, 10) == -1) {
        perror("listen");
        close(server_socket);
        exit(EXIT_FAILURE);
    }

    printf("Servidor escuchando en el puerto %d...\n", PORT);

    for (int i = 0; i < NUM_THREADS; i++) {
        if (pthread_create(&threads[i], NULL, thread_function, NULL) != 0) {
            perror("pthread_create");
            close(server_socket);
            exit(EXIT_FAILURE);
        }
    }

    while (1) {
        // Aceptamos una conexión entrante
        if ((client_socket = accept(server_socket, (struct sockaddr *)&client_addr, &client_addr_len)) == -1) { 
            perror("accept");
            continue; // Si hay un error al aceptar, seguimos esperando nuevas conexiones
        }
        task_t new_task;
        new_task.socket = client_socket;

        strcpy(new_task.ip, inet_ntoa(client_addr.sin_addr));

        printf("Cliente conectado: %s:%d\n", inet_ntoa(client_addr.sin_addr), ntohs(client_addr.sin_port));
        
        // Añadimos la nueva conexión a la cola de tareas para que los hilos trabajadores la atiendan
        enqueue(new_task);
    }

    close(server_socket); // Cerramos el socket del servidor (aunque nunca llegaremos aquí)
    return 0;
}