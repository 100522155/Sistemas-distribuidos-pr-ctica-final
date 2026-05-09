CC = gcc
CFLAGS = -Wall -Wextra -pthread -I/usr/include/tirpc
LDLIBS = -ltirpc

# Ejecutables
BIN_SERVER = servidor
BIN_RPC_SERVER = log_rpc_server

# Archivos generados por rpcgen
RPC_GEN = log_rpc.h log_rpc_clnt.c log_rpc_svc.c log_rpc_xdr.c

# Objetos del servidor de mensajería
OBJS_SERVER = servidor.o claves.o log_rpc_clnt.o log_rpc_xdr.o

# Objetos del servidor RPC
OBJS_RPC = log_rpc_server.o log_rpc_svc.o log_rpc_xdr.o

all: $(RPC_GEN) $(BIN_SERVER) $(BIN_RPC_SERVER)

# Generar stubs RPC
$(RPC_GEN): log_rpc.x
	rpcgen -C log_rpc.x

# Servidor principal (mensajería)
$(BIN_SERVER): $(OBJS_SERVER)
	$(CC) $(CFLAGS) -o $@ $^ $(LDLIBS)

# Servidor RPC de logs
$(BIN_RPC_SERVER): $(OBJS_RPC)
	$(CC) $(CFLAGS) -o $@ $^ $(LDLIBS)

# Regla implícita para compilar .c -> .o
%.o: %.c
	$(CC) $(CFLAGS) -c $< -o $@

# Dependencias explícitas
servidor.o: servidor.c claves.h log_rpc.h
claves.o: claves.c claves.h log_rpc.h
log_rpc_server.o: log_rpc_server.c log_rpc.h
log_rpc_clnt.o: log_rpc_clnt.c log_rpc.h
log_rpc_svc.o: log_rpc_svc.c log_rpc.h
log_rpc_xdr.o: log_rpc_xdr.c log_rpc.h

clean:
	rm -f *.o $(BIN_SERVER) $(BIN_RPC_SERVER) $(RPC_GEN)