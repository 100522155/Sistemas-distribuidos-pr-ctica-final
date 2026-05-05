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
    def register(user): #Crear un socket por cada operacion que realice el cliente, para evitar problemas de concurrencia al compartir un mismo socket entre varias operaciones. Cada vez que el cliente realiza una operación (como REGISTER, UNREGISTER, CONNECT, etc.), se crea un nuevo socket para esa operación específica. Esto permite que cada operación se maneje de forma independiente y evita posibles conflictos o bloqueos que podrían surgir al compartir un mismo socket entre múltiples operaciones concurrentes.
        try:
            client._socket = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
            client._socket.connect((client._server, client._port))


            client._socket.send(b"REGISTER\0")
            client._socket.send(user.encode('utf-8') + b'\0')
            res = struct.unpack('B', client._socket.recv(1))[0]
            client._socket.close()
            if   res == 0: print("REGISTER OK")
            elif res == 1: print("USERNAME IN USE")
            else:          print(f"REGISTER FAIL")
            return client.RC.OK if res == 0 else (client.RC.USER_ERROR if res == 1 else client.RC.ERROR)
        except Exception as e:
            print(f"Error en REGISTER: {e}")
            client._socket.close()
            return client.RC.ERROR

    @staticmethod
    def unregister(user):
        try:
            client._socket = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
            client._socket.connect((client._server, client._port))
            client._socket.send(b"UNREGISTER\0")
            client._socket.send(user.encode('utf-8') + b'\0')
            res = struct.unpack('B', client._socket.recv(1))[0]
            client._socket.close()
            if   res == 0: print("UNREGISTER OK")
            elif res == 1: print("USER DOES NOT EXIST")
            else:          print(f"UNREGISTER FAIL ({res})")
            return client.RC.OK if res == 0 else (client.RC.USER_ERROR if res == 1 else client.RC.ERROR)
        except Exception as e:
            print(f"Error en UNREGISTER: {e}")
            client._socket.close()
            return client.RC.ERROR

    @staticmethod
    def connect(user):
        try:
            #Buscamos un puerto libre 
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

            client._socket = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
            client._socket.connect((client._server, client._port))
            client._socket.send(b"CONNECT\0")
            client._socket.send(user.encode('utf-8') + b'\0')
            client._socket.send(str(client._listen_port).encode('utf-8') + b'\0') # Enviamos el puerto de escucha del cliente al servidor para que el servidor sepa a qué puerto debe enviar los mensajes al cliente. Esto es necesario para que el servidor pueda establecer una conexión con el cliente y enviarle los mensajes entrantes. Al enviar el puerto de escucha del cliente durante la operación CONNECT, el servidor puede mantener un registro de los clientes conectados y sus respectivos puertos de escucha, lo que le permite enrutar correctamente los mensajes a cada cliente.

            res = struct.unpack('B', client._socket.recv(1))[0]
            client._socket.close()
            
            client._cur_user = user
            
            if   res == 0: print("CONNECT OK")
            elif res == 1: print("CONNECT FAIL, USER DOES NOT EXIST")
            elif res == 2: print("USER ALREADY CONNECTED")
            else:          print("CONNECT FAIL")

            if res != 0:
                client._listening = False
                client._listen_socket.close()

            return client.RC.OK if res == 0 else (client.RC.USER_ERROR if res == 1 else client.RC.ERROR)
        
        except Exception as e:
            print(f"Error en CONNECT: {e}")
            return client.RC.ERROR

    @staticmethod
    def disconnect(user):
        try:
            client._socket = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
            client._socket.connect((client._server, client._port))
            client._socket.send(b"DISCONNECT\0")
            client._socket.send(user.encode('utf-8') + b'\0')
            client._listening = False
            res = struct.unpack('B', client._socket.recv(1))[0]
            if   res == 0: print("DISCONNECT OK")
            elif res == 1: print("DISCONNECT FAIL, USER DOES NOT EXIST")
            elif res == 2: print("DISCONNECT FAIL, USER NOT CONNECTED")
            else:          print("DISCONNECT FAIL")
            client._socket.close()
            return client.RC.OK if res == 0 else (client.RC.USER_ERROR if res == 1 else client.RC.ERROR)
        except Exception as e:
            print(f"Error en DISCONNECT: {e}")
            return client.RC.ERROR

    @staticmethod
    def users():
        try:
        # Enviar operación 4 y nombre (256 bytes)
            client._socket = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
            client._socket.connect((client._server, client._port))
            client._socket.send(b"USERS\0")
            user_name = getattr(client, '_cur_user', "anon")
            client._socket.send(user_name.encode('utf-8') + b'\0')

        # Leer respuesta y contador
            res = struct.unpack('B', client._socket.recv(1))[0] # Leer el código de respuesta (1 byte), con si la operación fue exitosa o no
            if res == 0:
                count_str = b""
                while True:
                    c = client._socket.recv(1)
                    if c == b'\0': break
                    count_str += c
                count = int(count_str.decode())
                print(f"CONNECTED USERS ({count} users connected) OK")  # formato exacto del enunciado

                for _ in range(count):
                    name = b""
                    while True:
                        c = client._socket.recv(1)
                        if c == b'\0': break
                        name += c
                    print(f"  {name.decode()}")
            elif res == 1:
                print("CONNECTED USERS FAIL, USER IS NOT CONNECTED")
            else:
                print("CONNECTED USERS FAIL")
            client._socket.close()
            return client.RC.OK if res == 0 else (client.RC.USER_ERROR if res == 1 else client.RC.ERROR)
            
        except Exception as e:
            print(f"Error: {e}")

    @staticmethod
    def send(user, message):
        try:
            client._socket = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
            client._socket.connect((client._server, client._port))
            client._socket.send(b"SEND\0") #operación 5: SEND
            sender = getattr(client, '_cur_user', "anon")
            client._socket.send(sender.encode('utf-8') + b'\0')   # remitente
            client._socket.send(user.encode('utf-8') + b'\0')     # destinatario
            client._socket.send(message.encode('utf-8') + b'\0')  # mensaje
            #Se recibe una respuesta del servidor, que es un byte que indica si el envío fue exitoso o no
            res = struct.unpack('B', client._socket.recv(1))[0] #respuesta del servidor (1 byte)
            if res == 0:
                id_str = b""
                while True:
                    c = client._socket.recv(1)
                    if c == b'\0': break
                    id_str += c
                print(f"SEND OK - MESSAGE {id_str.decode()}")
            elif res == 1: print("SEND FAIL, USER DOES NOT EXIST")
            else:          print("SEND FAIL")
            return client.RC.OK if res == 0 else client.RC.ERROR
        except Exception as e:
            print(f"Error en SEND: {e}")
            return client.RC.ERROR

    @staticmethod
    def sendAttach(user, file, message):
        # TODO
        return client.RC.ERROR

    @staticmethod
    def listen_thread():
        while client._listening:
            try:
                connection, _ = client._listen_socket.accept()
                
                # Leer la operación (cadena terminada en \0)
                op = b""
                while True:
                    c = connection.recv(1)
                    if c == b'\0': break
                    op += c
                op = op.decode()

                if op == "SEND_MESSAGE":
                    sender = b""
                    while True:
                        c = connection.recv(1)
                        if c == b'\0': break
                        sender += c
                    
                    msg_id = b""
                    while True:
                        c = connection.recv(1)
                        if c == b'\0': break
                        msg_id += c
                    
                    message = b""
                    while True:
                        c = connection.recv(1)
                        if c == b'\0': break
                        message += c
                    
                    print(f"\ns> MESSAGE {msg_id.decode()} FROM {sender.decode()}")
                    print(f"   {message.decode()}")
                    print("   END")

                elif op == "SEND_MESS_ACK":
                    msg_id = b""
                    while True:
                        c = connection.recv(1)
                        if c == b'\0': break
                        msg_id += c
                    print(f"\nc> SEND MESSAGE {msg_id.decode()} OK")

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

        client.shell()
        print("+++ FINISHED +++")


if __name__ == "__main__":
    client.main([])