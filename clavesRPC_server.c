#include <stdio.h>
#include <stdlib.h>
#include "clavesRPC.h" // Generado por rpcgen
#include "claves.h"    // lógica original de la Práctica 1

/* SET_VALUE */
int * set_value_rpc_1_svc(char *key, char *value1, int N_value2, float *V_value2, struct PaqueteRPC value3, struct svc_req *rqstp) {
    static int result;
    printf("Petición RPC: SET_VALUE para clave: %s\n", key);

    // Adaptamos el struct RPC al struct de claves.h
    struct Paquete p = {value3.x, value3.y, value3.z};

    result = set_value(key, value1, N_value2, V_value2, p);
    return &result;
}

/* GET_VALUE */
struct GetValueResult * get_value_rpc_1_svc(char *key, struct svc_req *rqstp) {
    static struct GetValueResult res;
    static char v1_local[256];
    struct Paquete p;

    printf("Petición RPC: GET_VALUE para clave: %s\n", key);

    res.error = get_value(key, v1_local, &res.N_value2, res.V_value2, &p);

    if (res.error == 0) {
        res.value1 = v1_local; // Apuntamos la respuesta al buffer
        res.value3.x = p.x;
        res.value3.y = p.y;
        res.value3.z = p.z;
    }
    return &res;
}

/* DELETE_KEY */
int * delete_key_rpc_1_svc(char *key, struct svc_req *rqstp) {
    static int result;
    printf("Petición RPC: DELETE_KEY para clave: %s\n", key);
    result = delete_key(key); 
    return &result;
}

/* EXIST */
int * exist_rpc_1_svc(char *key, struct svc_req *rqstp) {
    static int result;
    printf("Petición RPC: EXIST para clave: %s\n", key);
    result = exist(key);
    return &result;
}

/* MODIFY_VALUE */
int * modify_value_rpc_1_svc(char *key, char *value1, int N_value2, float *V_value2, struct PaqueteRPC value3, struct svc_req *rqstp) {
    static int result;
    printf("Petición RPC: MODIFY_VALUE para clave: %s\n", key);

    struct Paquete p = {value3.x, value3.y, value3.z};
    result = modify_value(key, value1, N_value2, V_value2, p);
    return &result;
}

/* DESTROY */
int * destroy_rpc_1_svc(struct svc_req *rqstp) {
    static int result;
    printf("Petición RPC: DESTROY\n");
    result = destroy(); 
    return &result;
}
