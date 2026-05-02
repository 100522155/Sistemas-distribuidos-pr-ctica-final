from enum import Enum
import argparse
import socket
import struct
import threading

class client:

    class RC(Enum):
        OK         = 0
        ERROR      = 1
        USER_ERROR = 2

    _server = None
    _port   = -1
    _socket = None
    _listen_thread = None
    _listen_socket = None
    _listening = False

    @staticmethod
    def register(user):
        try:
            client._socket.send(struct.pack('B', 0))
            client._socket.send(user.encode('utf-8').ljust(256, b'\0'))
            res = struct.unpack('B', client._socket.recv(1))[0]
            if   res == 0: print("REGISTER OK")
            elif res == 1: print("REGISTER IN USE")
            else:          print(f"REGISTER FAIL")
            return client.RC.OK if res == 0 else (client.RC.USER_ERROR if res == 1 else client.RC.ERROR)
        except Exception as e:
            print(f"Error en REGISTER: {e}")
            return client.RC.ERROR

    @staticmethod
    def unregister(user):
        try:
            client._socket.send(struct.pack('B', 1))
            client._socket.send(user.encode('utf-8').ljust(256, b'\0'))
            res = struct.unpack('B', client._socket.recv(1))[0]
            if   res == 0: print("UNREGISTER OK")
            elif res == 1: print("USER DOES NOT EXIST")
            else:          print(f"UNREGISTER FAIL ({res})")
            return client.RC.OK if res == 0 else (client.RC.USER_ERROR if res == 1 else client.RC.ERROR)
        except Exception as e:
            print(f"Error en UNREGISTER: {e}")
            return client.RC.ERROR

    @staticmethod
    def connect(user):
        try:
            
            tmp = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
            tmp.bind(('', 0))
            client._listen_port = tmp.getsockname()[1]
            tmp.close()

            listen_socket = socket.socket(socket.AF_INET, socket.SOCK_STREAM) #Creamos un socket para escuchar las conexiones entrantes del servidor
            listen_socket.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
            listen_socket.bind(('', client._listen_port))
            listen_socket.listen(10)

            client._listen_socket = listen_socket
            client._listening = True
            thr = threading.Thread(target=client.listen_thread, daemon=True)
            thr.start()
            client._listen_thread = thr


            client._socket.send(struct.pack('B', 2))
            client._socket.send(user.encode('utf-8').ljust(256, b'\0'))
            client._socket.send(struct.pack('!I', client._listen_port))  # puerto de escucha del cliente
            client._cur_user = user
            res = struct.unpack('B', client._socket.recv(1))[0]
            if   res == 0: print("CONNECT OK")
            elif res == 1: print("EL USUARIO NO EXISTE")
            elif res == 2: print("EL USUARIO YA ESTÁ CONECTADO")
            else:          print(f"CONNECT FAIL ({res})")
            return client.RC.OK if res == 0 else (client.RC.USER_ERROR if res == 1 else client.RC.ERROR)
        except Exception as e:
            print(f"Error en CONNECT: {e}")
            return client.RC.ERROR

    @staticmethod
    def disconnect(user):
        try:
            client._socket.send(struct.pack('B', 3))
            client._socket.send(user.encode('utf-8').ljust(256, b'\0'))
            client._socket.send(struct.pack('!I', 0))
            client._listening = False
            res = struct.unpack('B', client._socket.recv(1))[0]
            if   res == 0: print("DISCONNECT OK")
            elif res == 1: print("DISCONNECT: usuario no registrado")
            else:          print(f"DISCONNECT ERROR ({res})")
            return client.RC.OK if res == 0 else (client.RC.USER_ERROR if res == 1 else client.RC.ERROR)
        except Exception as e:
            print(f"Error en DISCONNECT: {e}")
            return client.RC.ERROR

    @staticmethod
    def users():
        try:
        # Enviar operación 4 y nombre (256 bytes)
            client._socket.send(struct.pack('B', 4))
            user_name = getattr(client, '_cur_user', "anon")
            client._socket.send(user_name.encode('utf-8').ljust(256, b'\0'))

        # Leer respuesta y contador
            res = struct.unpack('B', client._socket.recv(1))[0] # Leer el código de respuesta (1 byte), con si la operación fue exitosa o no
            if res == 0:
                count = struct.unpack('!I', client._socket.recv(4))[0] # Leer el número de usuarios conectados (4 bytes)
                print(f"USERS: {count} conectados")
                for _ in range(count): #leer cada usuario (256 bytes)
                # Leer hasta el \n
                    line = b"" #linea vacía de bytes
                    while not line.endswith(b"\n"): # mientras la línea no tenga un salro de línea al final
                        line += client._socket.recv(1) #lee un byte y lo añade a la línea
                    print(line.decode().strip()) #decodifica la línea a string, elimina espacios y saltos de línea y la imprime
        except Exception as e:
            print(f"Error: {e}")

    @staticmethod
    def send(user, message):
        client._socket.send(struct.pack('B', 5)) #operación 5: SEND
        sender = getattr(client, '_cur_user', "anon")
        client._socket.send(sender.encode('utf-8').ljust(256, b'\0')) #nombre del destinatario (256 bytes)
        client._socket.send(message.encode('utf-8').ljust(1024, b'\0')) #mensaje (1024 bytes)
        client._socket.send(user.encode('utf-8').ljust(256, b'\0')) #nombre del remitente (256 bytes)

        res = struct.unpack('B', client._socket.recv(1))[0] #respuesta del servidor (1 byte)
        if res == 0:
            print("SEND OK")
        else:
            print(f"SEND FAIL ({res})")
        return client.RC.OK if res == 0 else client.RC.ERROR

    @staticmethod
    def sendAttach(user, file, message):
        # TODO
        return client.RC.ERROR

    @staticmethod
    def listen_thread():
        """Hilo de escucha: acepta mensajes del servidor y otros clientes"""
        while client._listening:
            try:
                # Aceptar la conexión del servidor o de otro cliente
                connection, address = client._listen_socket.accept()
                
                # Leer el nombre del remitente (256 bytes)
                sender_name = connection.recv(256).decode('utf-8').strip('\x00')

                
                # Leer el mensaje (1024 bytes)
                message = connection.recv(1024).decode('utf-8').strip('\x00')
                
                # Mostrar el mensaje recibido
                print(f"\n>>> Mensaje de {sender_name}: {message}")
                print("c> ", end="", flush=True)
                
                connection.close()
            except Exception as e:
                if client._listening:
                    print(f"Error en listen_thread: {e}")
                break

    @staticmethod
    def shell():
        while True:
            try:
                command = input("c> ")
                line = command.split(" ")
                if not line:
                    continue
                line[0] = line[0].upper()

                if line[0] == "REGISTER":
                    if len(line) == 2: client.register(line[1])
                    else: print("Uso: REGISTER <usuario>")

                elif line[0] == "UNREGISTER":
                    if len(line) == 2: client.unregister(line[1])
                    else: print("Uso: UNREGISTER <usuario>")

                elif line[0] == "CONNECT":
                    if len(line) == 2: client.connect(line[1])
                    else: print("Uso: CONNECT <usuario>")

                elif line[0] == "DISCONNECT":
                    if len(line) == 2: client.disconnect(line[1])
                    else: print("Uso: DISCONNECT <usuario>")

                elif line[0] == "USERS":
                    if len(line) == 1: 
                        client.users() # Llama a la función si solo escribes USERS
                    else: print("Uso: USERS ")

                elif line[0] == "SEND":
                    if len(line) >= 3:
                        client.send(line[1], ' '.join(line[2:]))
                    else: print("Uso: SEND <usuario> <mensaje>")

                elif line[0] == "SENDATTACH":
                    if len(line) >= 4:
                        client.sendAttach(line[1], line[2], ' '.join(line[3:]))
                    else: print("Uso: SENDATTACH <usuario> <fichero> <mensaje>")

                elif line[0] == "QUIT":
                    if len(line) == 1: break
                    else: print("Uso: QUIT")

                else:
                    print(f"Comando desconocido: {line[0]}")

            except Exception as e:
                print(f"Excepción: {e}")

    @staticmethod
    def usage():
        print("Uso: python3 client.py -s <servidor> -p <puerto>")

    @staticmethod
    def parseArguments(argv):
        parser = argparse.ArgumentParser()
        parser.add_argument('-s', type=str, required=True, help='IP del servidor')
        parser.add_argument('-p', type=int, required=True, help='Puerto del servidor')
        args = parser.parse_args()

        if not (1024 <= args.p <= 65535):
            parser.error("El puerto debe estar entre 1024 y 65535")
            return False

        client._server = args.s
        client._port   = args.p
        return True

    @staticmethod
    def main(argv):
        if not client.parseArguments(argv):
            client.usage()
            return

        try:
            client._socket = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
            client._socket.connect((client._server, client._port))
            print(f"Conectado a {client._server}:{client._port}")
        except Exception as e:
            print(f"Error conectando al servidor: {e}")
            return

        client.shell()
        client._socket.close() #Cerramos la conexión con el servidor al salir de la shell (salimos del shell con el comando QUIT)
        print("+++ FINISHED +++")


if __name__ == "__main__":
    client.main([])