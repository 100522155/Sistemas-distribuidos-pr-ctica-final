CC = gcc
CFLAGS = -Wall -Wextra -pthread -I/usr/include/tirpc
LDLIBS = -ltirpc

# Ficheros generados por rpcgen
RPC_GEN = log_rpc_clnt.c log_rpc_svc.c log_rpc_xdr.c log_rpc.h

all: servidor log_rpc_server

# Regla para rpcgen
$(RPC_GEN): log_rpc.x
	rpcgen -C log_rpc.x

# Servidor principal (ahora incluye el cliente RPC)
servidor: servidor.o claves.o log_rpc_clnt.o log_rpc_xdr.o
	$(CC) $(CFLAGS) -o $@ $^ $(LDLIBS)

# Servidor RPC de logs
log_rpc_server: log_rpc_server.o log_rpc_svc.o log_rpc_xdr.o
	$(CC) $(CFLAGS) -o $@ $^ $(LDLIBS)

servidor.o: servidor.c claves.h log_rpc.h
claves.o: claves.c claves.h log_rpc.h
log_rpc_server.o: log_rpc_server.c log_rpc.h

clean:
	rm -f *.o servidor log_rpc_server $(RPC_GEN)