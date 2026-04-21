CC = gcc
CFLAGS = -Wall -fPIC -I/usr/include/tirpc
LDLIBS = -lnsl -lpthread -ltirpc

# Generaramos las librerías y los 3 ejecutables
all: libclaves.so libproxyclaves.so servidor_rpc cliente_rpc

# Generar archivos RPC
RPC_FILES = clavesRPC_clnt.c clavesRPC_svc.c clavesRPC_xdr.c clavesRPC.h
$(RPC_FILES): clavesRPC.x
	rpcgen -aNM clavesRPC.x

# Librería del lado del servidor
libclaves.so: claves.o
	$(CC) -shared -o $@ $^

# Librería del lado del cliente
libproxyclaves.so: proxy-rpc.o clavesRPC_clnt.o clavesRPC_xdr.o
	$(CC) -shared -o $@ $^ $(LDLIBS)

# --- EJECUTABLES ---

# El Servidor (usa la lógica real de claves.c)
servidor_rpc: clavesRPC_server.o clavesRPC_svc.o clavesRPC_xdr.o libclaves.so
	$(CC) $(CFLAGS) -o $@ clavesRPC_server.o clavesRPC_svc.o clavesRPC_xdr.o -L. -lclaves $(LDLIBS) -Wl,-rpath,.

# Cliente Distribuido (usa proxy con rpc)
cliente_rpc: app_cliente.o libproxyclaves.so
	$(CC) $(CFLAGS) -o $@ app_cliente.o -L. -lproxyclaves $(LDLIBS) -Wl,-rpath,.

%.o: %.c
	$(CC) $(CFLAGS) -c $<
	
clean:
	rm -f *.o *.so servidor_rpc cliente_rpc $(RPC_FILES)