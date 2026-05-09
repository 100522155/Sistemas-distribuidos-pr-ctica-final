#include "log_rpc.h"
#include <stdio.h>
#include <string.h>

int * print_log_1_svc(log_request *argp, struct svc_req *rqstp) {
    static int result = 0;

    // Imprimimos el nombre de usuario y la operación realizada
    printf("%s %s", argp->username, argp->operation);
    
    // Si es SENDATTACH, añadimos el nombre del fichero a la misma línea
    if (strcmp(argp->operation, "SENDATTACH") == 0 && strlen(argp->filename) > 0) {
        printf(" %s", argp->filename);
    }

    // Añadimos un salto de línea final para que la siguiente petición empiece abajo
    printf("\n");

    return &result;
}