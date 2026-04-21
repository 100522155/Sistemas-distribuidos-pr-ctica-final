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

#define PORT 5000 //Puerto a utilizar
#define BUFFER_SIZE 1024 //tamaño del buffer para recibir datos

typedef struct { // guarda el descriptor del socket del cliente
    int socket;
} task_t;

task_t task_queue[256]; // Cola de tareas para clientes
int queue_size = 0; //Contador de clientes
int queue_capacity = 256; //Cuantos clientes caben
int front = 0; //Es donde el main "lee" (saca) la siguiente conexión para atenderla. Siempre apunta a la siguiente tarea a procesar.
int rear = -1; //Es donde el main "escribe" (añade) la nueva conexión.

pthread_mutex_t queue_mutex = PTHREAD_MUTEX_INITIALIZER; // Mutex para proteger el acceso a la cola de tareas
pthread_cond_t queue_cond = PTHREAD_COND_INITIALIZER; // Condición para notificar a los hilos trabajadores de que hay tareas disponibles

// Función para añadir tareas a la cola
void enqueue(int client_socket) {
    pthread_mutex_lock(&queue_mutex); // Bloqueamos el mutex para modificar la cola de tareas
    if (queue_size < queue_capacity) { 
        rear = (rear + 1) % queue_capacity; // Incrementamos rear de forma circular
        task_queue[rear].socket = client_socket; // Añadimos el nuevo cliente a la cola
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






int main() {
    int server_socket, client_socket;
    struct sockaddr_in server_addr, client_addr;
    socklen_t client_addr_len = sizeof(client_addr);

    // Creamos el socket del servidor
    if ((server_socket = socket(AF_INET, SOCK_STREAM, 0)) == -1) {
        perror("socket");
        exit(EXIT_FAILURE);
    }

    // Configuramos la dirección del servidor
    memset(&server_addr, 0, sizeof(server_addr));
    server_addr.sin_family = AF_INET;
    server_addr.sin_addr.s_addr = INADDR_ANY;
    server_addr.sin_port = htons(PORT);

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

    while (1) {
        // Aceptamos una conexión entrante
        if ((client_socket = accept(server_socket, (struct sockaddr *)&client_addr, &client_addr_len)) == -1) {
            perror("accept");
            continue; // Si hay un error al aceptar, seguimos esperando nuevas conexiones
        }
        printf("Cliente conectado: %s:%d\n", inet_ntoa(client_addr.sin_addr), ntohs(client_addr.sin_port));

        // Añadimos la nueva conexión a la cola de tareas para que los hilos trabajadores la atiendan
        enqueue(client_socket);
    }

    close(server_socket); // Cerramos el socket del servidor (aunque nunca llegaremos aquí)
    return 0;
}