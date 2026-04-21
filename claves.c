#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <pthread.h>
#include "claves.h"

// Tenemos que almacenar tuplas con la forma <key-value1-value2-value3>, 
// y lo haremos con listas enlazadas, donde nuestro Nodo sera:
struct Nodo {
    char username[256];
    int conectado;       // 0: desconectado, 1: conectado
    char ip[16];         // IP donde escucha el cliente
    int puerto;          // Puerto donde escucha el cliente
    // (Para más adelante: aquí iría la lista de mensajes pendientes)
    struct Nodo *siguiente;
};

//Declaramos un mutex para proteger las secciones críticas de las funciones
pthread_mutex_t mutex = PTHREAD_MUTEX_INITIALIZER;

int destroy(void){
    pthread_mutex_lock(&mutex);
    // Creamos un puntero nuevo que apunte al nodo origen para no perder la referencia
    struct Nodo *actual = origen;
    struct Nodo *siguiente = NULL;
    
    while(actual!=NULL){
        // Guardamos la referencia al siguiente nodo
        siguiente = actual->siguiente;
        // Liberamos el espacio del nodo actual
        free(actual);
        // Y pasamos al siguiente nodo
        actual=siguiente;
    }
    // Dejamos la cabecera tambien vacía
    origen=NULL;
    // Todo ha salido bien, retornamos 0
    pthread_mutex_unlock(&mutex);
    return 0;

}

int set_value(char *key, char *value1, int N_value2, float *V_value2, struct Paquete value3){
    pthread_mutex_lock(&mutex);
    if (N_value2<1 || N_value2>32 || strlen(value1) > 255){
        printf("El valor de N_value2 está fuera de rango \n");
        pthread_mutex_unlock(&mutex);
        return -1;
    }

    struct Nodo *actual = origen;
    struct Nodo *ultimo = NULL;

    while(actual != NULL){
        if (strcmp(actual->key, key)==0){ //strcmp retorna 0 si las cadenas son iguales
            printf("Esta clave ya existe \n");
            pthread_mutex_unlock(&mutex);
            return -1;
        }
        ultimo = actual;
        actual = actual -> siguiente;
    }
    // Creamos el nuevo Nodo
    struct Nodo *nuevo = malloc(sizeof(struct Nodo));
    if (nuevo == NULL){ // si malloc falla devuelve NULL
        pthread_mutex_unlock(&mutex);
        return -1;
    }

    // Ahora copiamos los datos
    strcpy(nuevo -> key, key);
    strcpy(nuevo -> value1 , value1);
    nuevo -> N_value2 = N_value2;
    // Para copiar los valores de la lista lo tenemos que hacer con un bucle, no se puede hacer con =
    for(int i=0; i<N_value2; i++){
        nuevo -> V_value2[i] = V_value2[i];
    }
    nuevo -> value3 = value3;
    nuevo -> siguiente = NULL;

    // Ahora hacemos que el último Nodo apunte a este nuevo Nodo
    if (origen==NULL){
        origen = nuevo;
    }else{
        ultimo -> siguiente = nuevo;
    }
    pthread_mutex_unlock(&mutex);
    return 0;
}

int get_value(char *key, char *value1, int *N_value2, float *V_value2, struct Paquete *value3){
    pthread_mutex_lock(&mutex);
    struct Nodo *actual = origen;

    while(actual!=NULL){
        
        if (strcmp(actual->key, key)==0){
            
            // Ahora que ya estamos en el nodo que coincide con la clave pasada como parámetro, asignamos sus valores
            strcpy(value1, actual -> value1);
            *N_value2 = actual -> N_value2;
            for (int i = 0; i < actual->N_value2; i++) {
                V_value2[i] = actual->V_value2[i];
            }
            *value3 = actual->value3;
            pthread_mutex_unlock(&mutex);
            return 0;
            
        }
        actual = actual -> siguiente;
    }
    
    printf("No hay un nodo con esa clave \n");
    pthread_mutex_unlock(&mutex);
    return -1;

}


int modify_value(char *key, char *value1, int N_value2, float *V_value2, struct Paquete value3){
    pthread_mutex_lock(&mutex);
    if (N_value2<1 || N_value2>32 || strlen(value1) > 255){
        printf("El valor de N_value2 está fuera de rango \n");
        pthread_mutex_unlock(&mutex);
        return -1;
    }
    struct Nodo *actual = origen;

    while(actual!=NULL){
        
        if (strcmp(actual->key, key)==0){
            
            // Ahora que ya estamos en el nodo que coincide con la clave pasada como parámetro, asignamos sus valores
            strncpy(actual->value1, value1, 255);
            actual->value1[255] = '\0'; // Aseguramos el fin de cadena
            for (int i = 0; i < N_value2; i++) {
                actual->V_value2[i] = V_value2[i];
            }
            actual->value3 = value3;
            actual->N_value2 = N_value2;
            pthread_mutex_unlock(&mutex);
            return 0;
            
        }
        actual = actual -> siguiente;
    }

pthread_mutex_unlock(&mutex);
return -1;
}

int delete_key(char *key){
    pthread_mutex_lock(&mutex);
    struct Nodo *actual = origen;
    struct Nodo *anterior = NULL;

    while(actual!=NULL){
        
        if (strcmp(actual->key, key)==0){

            if (anterior == NULL) {
                origen = actual->siguiente;
            } else {
                // Si está en medio o al final, saltamos el nodo actual
                anterior->siguiente = actual->siguiente;
            }
            free(actual);
            pthread_mutex_unlock(&mutex);
            return 0;
            
        }
        anterior = actual;
        actual = actual -> siguiente;
    }
    
    printf("No hay un nodo con esa clave \n");
    pthread_mutex_unlock(&mutex);
    return -1;
}


int exist(char *key){
    pthread_mutex_lock(&mutex);
    struct Nodo *actual = origen;

    while(actual!=NULL){

        if (strcmp(actual->key, key)==0){
            pthread_mutex_unlock(&mutex);
            return 1;
        }
        actual = actual->siguiente;
    }
    pthread_mutex_unlock(&mutex);
    return 0;
}