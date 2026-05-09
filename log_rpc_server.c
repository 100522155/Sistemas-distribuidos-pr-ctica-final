#include "log_rpc.h"
#include <stdio.h>
#include <string.h>

int * print_log_1_svc(log_request *argp, struct svc_req *rqstp) {
    static int result = 0;

    // Imprimimos "usuario OPERACION" en la misma línea
    printf("%s %s", argp->username, argp->operation);
    
    // Si es SENDATTACH, añadimos el nombre del fichero a la misma línea
    if (strcmp(argp->operation, "SENDATTACH") == 0 && strlen(argp->filename) > 0) {
        printf(" %s", argp->filename);
    }

    // Salto de línea final para que la siguiente petición empiece abajo
    printf("\n");

    return &result;
}