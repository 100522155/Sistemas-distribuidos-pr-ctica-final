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

#define PORT 5000 // Puerto por defecto del servidor si no se pasa uno como argumento
#define BUFFER_SIZE 1024 // Tamaño del buffer para recibir datos
#define NUM_THREADS 8 // Número de hilos del pool para atender clientes concurrentemente

// Estructura que respresenta una tarea, almacenada en la cola para que los hilos trabajadores la atiendan En este caso, cada tarea es una conexión de cliente que se debe atender, y se guarda el descriptor del socket del cliente y su dirección IP para poder procesar las solicitudes que envíe posteriormente.
typedef struct {
    int socket;     // Descriptor del socket del cliente
    char ip[16];    // Dirección IP del cliente 
} task_t;

// Cola de tareas circular
task_t task_queue[256]; // Tamaño máximo de la cola
int queue_size = 0; // Número actual de tareas en la cola
int queue_capacity = 256;   // Capacidad máxima
int front = 0;  // Índice de la primera tarea
int rear = -1;  // Índice de la última tarea

pthread_mutex_t queue_mutex = PTHREAD_MUTEX_INITIALIZER; // Mutex para proteger el acceso a la cola de tareas
pthread_cond_t queue_cond = PTHREAD_COND_INITIALIZER; // Condición para notificar a los hilos trabajadores de que hay tareas disponibles

// Prototipos de las funciones
void handle_register(int socket);
void handle_unregister(int socket);
void handle_connect(int socket, char *client_ip);
void handle_disconnect(int socket, char *client_ip);
void handle_users(int socket);
void handle_send(int socket);
void handle_sendattach(int socket);
void handle_quit(int socket);

// Función para añadir tareas a la cola
void enqueue(task_t new_task) {
    // Bloqueamos el mutex para modificar la cola de tareas
    pthread_mutex_lock(&queue_mutex); 

    if (queue_size < queue_capacity) { 
        // Incrementamos rear de forma circular
        rear = (rear + 1) % queue_capacity; 
        // Añadimos una tarea a la cola
        task_queue[rear] = new_task;
        // Incrementamos el contador de tareas
        queue_size++; 
        pthread_cond_signal(&queue_cond); // Avisamos a un hilo trabajador
    }
    pthread_mutex_unlock(&queue_mutex);
}

//Función para sacar tareas de la cola
task_t dequeue() {
    // Bloqueamos el mutex para modificar la cola de tareas
    pthread_mutex_lock(&queue_mutex); 

    while (queue_size == 0) { 
        // Si no hay tareas, esperamos a que se añadan
        pthread_cond_wait(&queue_cond, &queue_mutex);
    }
    // Obtenemos la tarea de la cola
    task_t task = task_queue[front]; 
    // Incrementamos front de forma circular
    front = (front + 1) % queue_capacity; 
    // Decrementamos el contador de tareas
    queue_size--; 

    pthread_mutex_unlock(&queue_mutex);
    return task; // Devolvemos la tarea de la cola
}

// Función que es un bucle infinito que se ejecuta en cada hilo trabajador para atender las conexiones de los clientes
void *thread_function() {
    while (1) {
        // Sacamos una tarea de la cola para atenderla
        task_t task = dequeue();
        int client_socket = task.socket;

        int exit_loop = 0;
        while (!exit_loop) {

            // Leemos la operación byte a byte hasta '\0'
            char op[32];
            int op_len = 0;
            int ok = 1;
            while (op_len < (int)sizeof(op) - 1) {
                ssize_t r = read(client_socket, &op[op_len], 1); 

                if (r <= 0) { ok = 0; break; } // Si hay un error o el cliente se desconecta, salimos del bucle
                if (op[op_len] == '\0') break; // Si encontramos el carácter nulo, hemos leído toda la operación y podemos salir del bucle
                
                op_len++;
            }
            op[op_len] = '\0'; // Aseguramos que la operación es una cadena de caracteres terminada en nulo para evitar problemas al procesarla posteriormente

            if (!ok) {
                exit_loop = 1;
                break;
            }
            // Comparamos la operación con las definidas en el protocolo
            if (strcmp(op, "REGISTER") == 0) handle_register(client_socket);
            else if (strcmp(op, "UNREGISTER") == 0) handle_unregister(client_socket);
            else if (strcmp(op, "CONNECT") == 0) handle_connect(client_socket, task.ip); //Se requiere la ip para esta operacion
            else if (strcmp(op, "DISCONNECT") == 0) handle_disconnect(client_socket, task.ip);
            else if (strcmp(op, "USERS") == 0) handle_users(client_socket);
            else if (strcmp(op, "SEND") == 0) handle_send(client_socket);
            else if (strcmp(op, "SENDATTACH") == 0) handle_sendattach(client_socket);
            else if (strcmp(op, "QUIT") == 0) {
                handle_quit(client_socket);
                exit_loop = 1; // Después de QUIT cerramos la conexión
            }

        }
        // Cerramos el socket del cliente al terminar
        close(client_socket);
    }
    return NULL;
}


int main(int argc, char *argv[]) {
    //Obtenemos el puerto del servidor de los argumentos, si se proporciona, o usamos el puerto por defecto
    int port = PORT; // Valor por defecto
    
    // Procesamos los argumentos de línea de comandos para obtener el puerto del servidor
    if (argc == 2) {
        port = atoi(argv[1]); // Accedemos al índice 1
    } else if (argc > 2) {
        fprintf(stderr, "Uso: %s [puerto]\n", argv[0]);
        exit(EXIT_FAILURE);
    }
    // Validamos que el puerto es un número válido entre 1 y 65535
    if (port <= 0 || port > 65535) {
        exit(EXIT_FAILURE);
    }

    int server_socket, client_socket; //Creamos los descriptores de socket para el servidor y el cliente
    struct sockaddr_in server_addr, client_addr; //struct de tres clases: Familia (IPv4), IP y Puerto.
    socklen_t client_addr_len = sizeof(client_addr); // Variable para almacenar el tamaño de la dirección del cliente
    pthread_t threads[NUM_THREADS];
    
    // Creamos el socket del servidor con IPv4 (AF_INET) y TCP (SOCK_STREAM)
    if ((server_socket = socket(AF_INET, SOCK_STREAM, 0)) == -1) {
        perror("socket");
        exit(EXIT_FAILURE);
    }

    int opt = 1; // Opción para poder reutilizar la dirección del socket
    setsockopt(server_socket, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

    // Configuramos la dirección del servidor
    memset(&server_addr, 0, sizeof(server_addr)); // Limpiamos la estructura de la dirección del servidor
    server_addr.sin_family = AF_INET; // Familia de direcciones (IPv4)
    server_addr.sin_addr.s_addr = INADDR_ANY; // Aceptamos conexiones en cualquier interfaz de red
    server_addr.sin_port = htons(port); // Convertimos el número de puerto a formato de red

    // Enlazamos el socket a la dirección del servidor
    if (bind(server_socket, (struct sockaddr *)&server_addr, sizeof(server_addr)) == -1) {
        perror("bind");
        close(server_socket);
        exit(EXIT_FAILURE);
    }

    // Ponemos el socket en modo escucha y escuchamos por conexiones entrantes
    if (listen(server_socket, 10) == -1) {
        perror("listen");
        close(server_socket);
        exit(EXIT_FAILURE);
    }

    printf("Servidor escuchando en el puerto %d...\n", port);

    // Creamos a los hilos del pool de hilos
    for (int i = 0; i < NUM_THREADS; i++) {
        if (pthread_create(&threads[i], NULL, thread_function, NULL) != 0) {
            perror("pthread_create");
            close(server_socket);
            exit(EXIT_FAILURE);
        }
    }

    // Bucle principal del servidor para aceptar conexiones entrantes
    while (1) {
        // Aceptamos una conexión entrante
        if ((client_socket = accept(server_socket, (struct sockaddr *)&client_addr, &client_addr_len)) == -1) { 
            perror("accept");
            continue; // Si hay un error al aceptar, seguimos esperando nuevas conexiones
        }
        task_t new_task;
        new_task.socket = client_socket;

        strcpy(new_task.ip, inet_ntoa(client_addr.sin_addr));        
        // Añadimos la nueva conexión a la cola de tareas para que los hilos trabajadores la atiendan
        enqueue(new_task);
    }

    close(server_socket); // Cerramos el socket del servidor (aunque nunca llegaremos aquí)
    return 0;
}