from enum import Enum
import argparse
import socket
import struct
import threading
import os
import zeep

class client:
    """Esta clase corresponde con el cliente del sistema de mensajería. 
    Permite registrar usuarios, conectarse, enviar mensajes simples o con adjuntos,
    recibir mensajes, listar usuarios conectados y transferir ficheros P2P."""

    class RC(Enum):
        """Códigos de retorno para las operaciones."""

        OK         = 0
        ERROR      = 1
        USER_ERROR = 2

    # Atributos estáticos
    _connected_users = {} # Diccionario: {nombre_usuario: (ip, puerto)} de usuarios conectados
    _server = None  # IP del servidor de mensajería
    _port   = -1    # Puerto del servidor de mensajería
    _socket = None  # Socket temporal para operaciones puntuales
    _listen_thread = None   # Hilo de escucha para recibir mensajes del servidor y de otros clientes
    _listen_socket = None   # Socket de escucha (para conexiones entrantes del servidor y P2P)
    _listening = False  # Flag que indica si el hilo de escucha está activo
    _ws_client = None   # Cliente para el servicio web de normalización de mensajes

    @staticmethod
    def get_ws_client():
        """Obtiene el cliente del servicio web. Si no existe, lo crea."""

        if client._ws_client is None:
            try:
                wsdl_url = "http://localhost:8000/?wsdl"
                client._ws_client = zeep.Client(wsdl=wsdl_url)
            except Exception as e:
                print(f"Error conectando al Servicio Web: {e}")
        return client._ws_client    

    @staticmethod
    def register(user):
        """ Registra un nuevo usuario en el sistema.
        El protocolo de envío es "REGISTER\0" + nombre\0
        Recibe un byte: 0 = OK, 1 = nombre en uso, 2 = error."""
        
        try:
            # Creamos un socket nuevo para esta operación
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
        """Esta función da de baja a un usuario del sistema.
        El protocolo de envío es "UNREGISTER\0" + nombre\0
        Recibe un byte: 0 = OK, 1 = usuario no existe, 2 = error.
        """

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
        """Esta función conecta al usuario al servidor (cambia a estado conectado).
        Previamente crea un hilo de escucha y un socket pasivo.
        El protocolo de envío es "CONNECT\0" + nombre\0 + puerto_de_escucha\0
        Recibe un byte: 0 = OK, 1 = usuario no existe, 2 = ya conectado, 3 = error.
        """

        try:
            # Obtenemos un puerto libre para el hilo de escucha
            tmp = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
            tmp.bind(('', 0))
            client._listen_port = tmp.getsockname()[1] #Obtenemos el puerto asignado por el SO y lo guardamos para usarlo en la conexión con el servidor. El puerto se asigna dinámicamente al usar 0, lo que permite al sistema operativo elegir un puerto libre automáticamente.
            tmp.close()

            # Configuramos el socket de escucha
            listen_socket = socket.socket(socket.AF_INET, socket.SOCK_STREAM) #Creamos un socket para escuchar las conexiones entrantes del servidor
            listen_socket.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
            listen_socket.bind(('', client._listen_port))
            listen_socket.listen(10)

            # Guardamos el socket de escucha y el estado de escucha para que el hilo de escucha pueda usarlo
            client._listen_socket = listen_socket 
            client._listening = True
            # Creamos el hilo de escucha para recibir mensajes del servidor y de otros clientes. El hilo se ejecuta en segundo plano (daemon=True) para que se cierre automáticamente al salir del programa principal.
            thr = threading.Thread(target=client.listen_thread, daemon=True) 
            thr.start() 
            client._listen_thread = thr

            # Enviamos la petición CONNECT al servidor
            client._socket = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
            client._socket.connect((client._server, client._port))
            client._socket.send(b"CONNECT\0")
            client._socket.send(user.encode('utf-8') + b'\0')
            client._socket.send(str(client._listen_port).encode('utf-8') + b'\0')

            # Recibimos la respuesta del servidor
            res = struct.unpack('B', client._socket.recv(1))[0]

            # Cerramos el socket de la operación CONNECT
            client._socket.close()
            
            # Guardamos el usuario conectado actualmente en una variable para usarlo en otras operaciones
            client._cur_user = user 
            
            # Mostramos un mensaje con el resultado de la operación según el código recibido del servidor
            if res == 0: print("CONNECT OK")
            elif res == 1: print("CONNECT FAIL, USER DOES NOT EXIST")
            elif res == 2: print("USER ALREADY CONNECTED")
            else: print("CONNECT FAIL")

            # Si la conexión falló, detenemos el hilo de escucha
            if res != 0:
                client._listening = False
                client._listen_socket.close()

            return client.RC.OK if res == 0 else (client.RC.USER_ERROR if res == 1 else client.RC.ERROR)
        
        except Exception as e:
            print(f"Error en CONNECT: {e}")
            return client.RC.ERROR

    @staticmethod
    def disconnect(user):
        """ Esta función desconecta al usuario (sin darle de baja).
        El protocolo de envío es "DISCONNECT\0" + nombre\0
        Recibe un byte: 0 = OK, 1 = usuario no existe, 2 = no estaba conectado, 3 = error."""

        try:
            # Enviamos la petición DISCONNECT al servidor
            client._socket = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
            client._socket.connect((client._server, client._port))
            client._socket.send(b"DISCONNECT\0")
            client._socket.send(user.encode('utf-8') + b'\0')
            #Detenemos el hilo de escucha para que deje de aceptar conexiones entrantes del servidor
            client._listening = False 

            # Recibimos la respuesta del servidor sobre la operación DISCONNECT
            res = struct.unpack('B', client._socket.recv(1))[0]
            
            # Mostramos un mensaje con el resultado de la operación según el código recibido del servidor
            if res == 0: print("DISCONNECT OK")
            elif res == 1: print("DISCONNECT FAIL, USER DOES NOT EXIST")
            elif res == 2: print("DISCONNECT FAIL, USER NOT CONNECTED")
            else: print("DISCONNECT FAIL")

            # Cerramos socket de la operación DISCONNECT
            client._socket.close() 

            return client.RC.OK if res == 0 else (client.RC.USER_ERROR if res == 1 else client.RC.ERROR)
        except Exception as e:
            print(f"Error en DISCONNECT: {e}")
            return client.RC.ERROR

    @staticmethod
    def users(verprint=True):
        """ ESta función solicita al servidor la lista de usuarios conectados.
        Si verprint es True, muestra la lista por pantalla. Si es False, solo actualiza internamente.
        EL protocolo de envío es "USERS\0" + nombre_solicitante\0
        Recibe: 1 byte (0 éxito, 1 solicitante no conectado, 2 error)
        Si éxito: cadena con número de usuarios ("N\0") y luego N cadenas con formato "nombre :: IP :: puerto".
        Actualiza el diccionario _connected_users y opcionalmente imprime."""

        try:
            # Enviamos la petición USERS al servidor
            client._socket = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
            client._socket.connect((client._server, client._port))
            client._socket.send(b"USERS\0")
            user_name = getattr(client, '_cur_user', "anon")
            client._socket.send(user_name.encode('utf-8') + b'\0')

            # Recibimos la respuesta del servidor sobre la operación USERS
            res_raw = client._socket.recv(1)
            if not res_raw: return client.RC.ERROR
            res = struct.unpack('B', res_raw)[0]

            # Si la respuesta es éxito:
            if res == 0:
                # Leer el número de usuarios (cadena terminada en \0)
                count_str = b""
                while True:
                    c = client._socket.recv(1)
                    if c == b'\0' or not c: break
                    count_str += c

                count = int(count_str.decode())
                if verprint:
                    print(f"CONNECTED USERS ({count} users connected) OK")

                # Limpiamos los usuarios conocidos antes de actualizar
                client._connected_users = {}

                for _ in range(count):
                    # Leemos cada línea "usuario :: IP :: puerto"
                    data = b""
                    while True:
                        c = client._socket.recv(1) 
                        if c == b'\0' or not c: break
                        data += c

                    full_info = data.decode()
                    
                    # Parseamos la información para extraer el nombre, IP y puerto, y guardarla en el diccionario de usuarios conectados
                    parts = full_info.split("::")
                    if len(parts) == 3:
                        uname = parts[0].strip()
                        uip   = parts[1].strip()
                        uport = int(parts[2].strip())
                        
                        # Guardamos en el diccionario
                        client._connected_users[uname] = (uip, uport)
                        
                        # Imprimimos solo el nombre (o la info completa según prefieras)
                        if verprint:    
                            print(f"  {uname}")
                    else:
                        # Por si el servidor solo envía el nombre (compatibilidad)
                        if verprint:
                            print(f"  {full_info}")

            # Si la respuesta es que el solicitante no está conectado, mostramos un mensaje de error específico
            elif res == 1:
                if verprint:
                    print("CONNECTED USERS FAIL, USER IS NOT CONNECTED")
            # Si es otro error, mostramos un mensaje general
            else:
                if verprint:
                    print("CONNECTED USERS FAIL")
            # Cerramos el socket de la operación USERS
            client._socket.close()
            return client.RC.OK if res == 0 else (client.RC.USER_ERROR if res == 1 else client.RC.ERROR)
            
        except Exception as e:
            if verprint:
                print(f"Error en USERS: {e}")
            return client.RC.ERROR

    @staticmethod
    def send(user, message):
        """ Esta función envía un mensaje de texto simple a otro usuario.
        Normaliza el mensaje usando el servicio web (si está disponible).
        El protocolo de envio es "SEND\0" + remitente\0 + destinatario\0 + mensaje\0
        Recibe: 1 byte (0 éxito, 1 destinatario no existe, 2 error)
        Si éxito: recibe cadena con el ID del mensaje.
        """
        try:
            # Normalizamos el mensaje usando el servicio web
            ws = client.get_ws_client()
            if ws:
                message = ws.service.normalizar_mensaje(message)

            # Enviamos la petición SEND al servidor
            client._socket = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
            client._socket.connect((client._server, client._port))
            client._socket.send(b"SEND\0")
            sender = getattr(client, '_cur_user', "anon")
            client._socket.send(sender.encode('utf-8') + b'\0')
            client._socket.send(user.encode('utf-8') + b'\0')
            client._socket.send(message.encode('utf-8') + b'\0')

            # Recibimos la respuesta del servidor sobre la operación SEND
            res = struct.unpack('B', client._socket.recv(1))[0]
            
            # Si el envío fue exitoso leemos el ID del mensaje y mostramos mensaje de éxito
            if res == 0:
                id_str = b""
                while True:
                    c = client._socket.recv(1)
                    if c == b'\0': break
                    id_str += c

                # Mostramos éxito en el envío y el id del mensaje
                print(f"SEND OK - MESSAGE {id_str.decode()}")  
            
            # Si el destinatario no existe, mostramos un mensaje de error específico
            elif res == 1: print("SEND FAIL, USER DOES NOT EXIST")

            # Si es otro error, mostramos un mensaje general de fallo en el envío
            else: print("SEND FAIL")

            return client.RC.OK if res == 0 else client.RC.ERROR
        except Exception as e:
            print(f"Error en SEND: {e}")
            return client.RC.ERROR

    @staticmethod
    def sendAttach(user, file, message):
        """ ESta función envía un mensaje con fichero adjunto.
        El protocolo de envio es "SENDATTACH\0" + remitente\0 + destinatario\0 + mensaje\0 + nombre_fichero\0
        Recibe: 1 byte (0 éxito, 1 destinatario no existe, 2 error) y luego ID si éxito.
        """
        try:
            # Verificamos si el fichero existe
            if not os.path.exists(file):
                print(f"ERROR: El fichero {file} no existe.")
                return client.RC.ERROR

            # Normalizamos el mensaje usando el servicio web
            ws = client.get_ws_client()
            if ws:
                message = ws.service.normalizar_mensaje(message)

            # Enviamos la petición SENDATTACH al servidor
            client._socket = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
            client._socket.connect((client._server, client._port))
            client._socket.send(b"SENDATTACH\0")
            sender = getattr(client, '_cur_user', "anon")
            client._socket.send(sender.encode('utf-8') + b'\0')
            client._socket.send(user.encode('utf-8') + b'\0')
            client._socket.send(message.encode('utf-8') + b'\0')
            client._socket.send(file.encode('utf-8') + b'\0')

            # Esperamos la respuesta del servidor sobre la operación SENDATTACH
            res_raw = client._socket.recv(1)
            if not res_raw:
                return client.RC.ERROR
                
            res = struct.unpack('B', res_raw)[0]
            
            # Si el envío fue exitoso leemos el ID del mensaje y mostramos mensaje de éxito
            if res == 0:
                id_str = b""
                while True:
                    c = client._socket.recv(1)
                    if not c or c == b'\0': break
                    id_str += c

                # Mostramos éxito en el envío y el id del mensaje
                print(f"SENDATTACH OK - MESSAGE {id_str.decode()}")
                # Cerramos el socket de la operación SENDATTACH
                client._socket.close()

                return client.RC.OK
            
            # Si el destinatario no existe, mostramos un mensaje de error específico
            elif res == 1: 
                print("SENDATTACH FAIL, USER DOES NOT EXIST")
            # Si es otro error, mostramos un mensaje general de fallo en el envío
            else:          
                print("SENDATTACH FAIL")

            client._socket.close()            
            return client.RC.ERROR

        except Exception as e:
            print(f"Error en SENDATTACH: {e}")
            return client.RC.ERROR
    
    @staticmethod
    def getfile(username, remote_filename, local_filename):
        """ Esta función transfiere un fichero desde otro usuario (P2P) usando la conexión directa.
        El protocolo de envío consiste en que se conecta a la IP:puerto del otro usuario (obtenido con USERS).
        Envía: "GET_FILE\0" + nombre_solicitante\0 + nombre_fichero_remoto\0
        Recibe: 1 byte (0 éxito, 1 error) y luego el contenido del fichero hasta cierre de conexión.
        """
        try:
            # Si no tenemos los datos del usuario, llamamos a USERS para actualizar la lista de usuarios conectados
            if username not in client._connected_users:
                client.users(verprint=False)  
            
            # Si después de llamar a USERS sigue sin estar, es que el usuario no está conectado
            if username not in client._connected_users:
                print(f"c> FILE TRANSFER FAILED, user not connected.")
                return client.RC.ERROR
            
            # Obtenemos la IP y puerto
            sender_ip, sender_port = client._connected_users[username]
            
            # Conexión P2P
            s = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
            try:
                s.connect((sender_ip, sender_port))
            except Exception as e:
                print(f"c> FILE TRANSFER FAILED, user not connected.")
                return client.RC.ERROR
            
            # Enviamos la petición GET_FILE al otro usuario 
            s.send(b"GET_FILE\0")
            my_name = getattr(client, '_cur_user', "anon")
            s.send(my_name.encode('utf-8') + b'\0')
            s.send(remote_filename.encode('utf-8') + b'\0')
            
            # Leemos la respuesta del otro usuario sobre la operación GET_FILE
            response = s.recv(1)
            if not response or response[0] != 0:
                print("c> FILE TRANSFER FAILED, user not connected.")
                s.close()
                return client.RC.ERROR
            
            # Recibimos el contenido del fichero hasta que el otro cierre la conexión
            received = 0
            try:
                with open(local_filename, "wb") as f:
                    while True:
                        data = s.recv(4096)
                        if not data:
                            break
                        f.write(data)
                        received += len(data)
            except Exception as e:
                print(f"c> FILE TRANSFER FAILED")
                s.close()
                if os.path.exists(local_filename):
                    os.remove(local_filename)
                return client.RC.ERROR
            
            # Cerramos la conexión P2P
            s.close()
            
            # Si recibimos algo mostramos mensaje de éxito
            if received > 0:  
                print("c> GETFILE OK")
                return client.RC.OK
            else:
                # En caso contrario, mensaje de error y eliminamos el fichero local si se creó
                if os.path.exists(local_filename):
                    os.remove(local_filename)
                print("c> FILE TRANSFER FAILED")
                return client.RC.ERROR
                
        except Exception as e:
            print("c> FILE TRANSFER FAILED")
            return client.RC.ERROR

    @staticmethod
    def listen_thread():
        """ El hilo de escucha atiende:
            - Mensajes entrantes del servidor (SEND_MESSAGE, SEND_MESSAGE_ATTACH, ACKs)
            - Peticiones P2P de ficheros (GET_FILE) """

        while client._listening:
            try:
                connection, _ = client._listen_socket.accept()
                
                # Leemos la operación (cadena terminada en \0)
                op = b""
                while True:
                    c = connection.recv(1)
                    if not c or c == b'\0': break
                    op += c
                op = op.decode()

                # Mensaje simple
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
                    
                    print(f"\ns> MESSAGE {msg_id.decode()} FROM {sender.decode()} {message.decode()} END")
                    print("c> ", end="", flush=True)

                # Mensaje con fichero adjunto
                elif op == "SEND_MESSAGE_ATTACH":
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

                    filename = b""
                    while True:
                        c = connection.recv(1)
                        if c == b'\0': break
                        filename += c

                    print(f"\ns> MESSAGE {msg_id.decode()} FROM {sender.decode()} {message.decode()} END FILE {filename.decode()}")
                    print("c> ", end="", flush=True)

                # Confirmación de entrega de mensaje con adjunto (ACK)
                elif op == "SEND_MESS_ATTACH_ACK":
                    msg_id = b""
                    while True:
                        c = connection.recv(1)
                        if c == b'\0': break
                        msg_id += c
                    
                    filename = b""
                    while True:
                        c = connection.recv(1)
                        if c == b'\0': break
                        filename += c

                    print(f"\ns> SENDATTACH MESSAGE {msg_id.decode()} {filename.decode()} OK")
                    print("c> ", end="", flush=True)
                
                # Confirmación de entrega de mensaje simple (ACK)
                elif op == "SEND_MESS_ACK":
                    msg_id = b""
                    while True:
                        c = connection.recv(1)
                        if c == b'\0': break
                        msg_id += c
                    print(f"\ns> SEND MESSAGE {msg_id.decode()} OK")
                    print("c> ", end="", flush=True)

                # Petición P2P de fichero
                elif op == "GET_FILE":

                    # Leemos nombre del solicitante
                    requester = b""
                    while True:
                        c = connection.recv(1)
                        if not c or c == b'\0': break
                        requester += c

                    # Leemos nombre del fichero solicitado
                    filename_b = b""
                    while True:
                        c = connection.recv(1)
                        if not c or c == b'\0': break
                        filename_b += c
                    filename = filename_b.decode()

                    # Verificamos su existencia y enviamos el contenido
                    if os.path.exists(filename):
                        connection.send(bytes([0]))
                        with open(filename, 'rb') as f:
                            while True:
                                data = f.read(4096)
                                if not data: break
                                connection.sendall(data)
                    else:
                        connection.send(bytes([1])) # 1 = error (fichero no existe)
                
                connection.close()

            except Exception as e:
                if client._listening:
                    print(f"\nError en listen_thread: {e}")
                break

    @staticmethod
    def shell():
        """ Bucle principal que lee comandos del usuario y ejecuta las funciones correspondientes.
        Los comandos disponibles: REGISTER, UNREGISTER, CONNECT, DISCONNECT, USERS, SEND, SENDATTACH, GETFILE, QUIT. """

        while True:
            try:
                #Primero asignamos a command "c>" para que se muestre siempre antres del input
                command = input("c> ")
                line = command.split(" ") # le hacemos el split para separar el comando y sus argumentos,
                if not line: 
                    continue
                #Convertimos el comando a mayúsculas para que no sea sensible a mayúsculas/minúsculas
                line[0] = line[0].upper() 
                
                # Ahora procesamos cada comando según su formato esperado
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
                        client.users()
                    else: print("Uso: USERS ")

                elif line[0] == "SEND":
                    if len(line) >= 3:
                        client.send(line[1], ' '.join(line[2:])) 
                    else: print("Uso: SEND <usuario> <mensaje>")

                elif line[0] == "SENDATTACH":
                    if len(line) >= 4:
                        dest     = line[1]
                        filename = line[2]
                        message  = ' '.join(line[3:])
                        client.sendAttach(dest, filename, message)
                    else: print("Uso: SENDATTACH <usuario> <fichero> <mensaje>")
                
                elif line[0] == "GETFILE":
                    if len(line) == 4:
                        client.getfile(line[1], line[2], line[3])
                    else:
                        print("Uso: GETFILE <usuario> <fichero_remoto> <fichero_local>")

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
        """Esta función procesa los argumentos de línea de comandos."""
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
            client.usage() #Si los argumentos no son correctos, se muestra el mensaje de uso y se sale del programa.
            return

        client.shell()
        print("+++ FINISHED +++")


if __name__ == "__main__":
    client.main([])