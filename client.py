from enum import Enum
import argparse
import socket
import struct
import threading
import os

class client:

    class RC(Enum):
        OK         = 0
        ERROR      = 1
        USER_ERROR = 2

    _connected_users = {} #Lista de usuarios que tiene este que están conectados
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
            client._listen_port = tmp.getsockname()[1] #Obtenemos el puerto asignado por el SO y lo guardamos para usarlo en la conexión con el servidor. El puerto se asigna dinámicamente al usar 0, lo que permite al sistema operativo elegir un puerto libre automáticamente.
            tmp.close()

            listen_socket = socket.socket(socket.AF_INET, socket.SOCK_STREAM) #Creamos un socket para escuchar las conexiones entrantes del servidor
            listen_socket.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
            listen_socket.bind(('', client._listen_port))
            listen_socket.listen(10)

            client._listen_socket = listen_socket #El usuario tiene su propio socket de escucha para recibir mensajes
            client._listening = True #Está escuchando para recibir mensajes del servidor
            thr = threading.Thread(target=client.listen_thread, daemon=True) #Creamos un hilo para manejar las conexiones entrantes del servidor de forma concurrente, sin bloquear la ejecución del hilo principal del cliente, que se encarga de leer los comandos del usuario y enviar las solicitudes al servidor. Al usar un hilo separado para escuchar las conexiones entrantes, el cliente puede seguir respondiendo a los comandos del usuario mientras espera mensajes del servidor.
            thr.start() #El hilo empieza a escuchar las conexiones entrantes del servidor 
            client._listen_thread = thr #Asignamos el hilo al usuario para poder controlarlo (por ejemplo, para detenerlo al desconectar)

            client._socket = socket.socket(socket.AF_INET, socket.SOCK_STREAM) #Hilo de "connect", distinto del hilo de escucha
            client._socket.connect((client._server, client._port))
            client._socket.send(b"CONNECT\0")
            client._socket.send(user.encode('utf-8') + b'\0')
            client._socket.send(str(client._listen_port).encode('utf-8') + b'\0') # Enviamos el puerto de escucha del cliente al servidor para que el servidor sepa a qué puerto debe enviar los mensajes al cliente. Esto es necesario para que el servidor pueda establecer una conexión con el cliente y enviarle los mensajes entrantes. Al enviar el puerto de escucha del cliente durante la operación CONNECT, el servidor puede mantener un registro de los clientes conectados y sus respectivos puertos de escucha, lo que le permite enrutar correctamente los mensajes a cada cliente.

            res = struct.unpack('B', client._socket.recv(1))[0] #recibimos la respuesta de servidor.
            #struct = modulo para convertir datos a bytes y viceversa, se usa para interpretar la respuesta del servidor como un byte que indica el resultado de la operación CONNECT (0 para éxito, 1 para usuario no existe, 2 para usuario ya conectado, etc.)
            #unpack('B', data) interpreta el byte recibido como un entero sin signo (unsigned char) y devuelve una tupla con ese valor. Al usar [0], obtenemos el valor entero directamente.
            client._socket.close() #se cierra el hilo al terminar la operacion de conexion, el hilo de escucha sigue abierto para recibir mensajes del servidor
            
            client._cur_user = user # Guardamos el usuario conectado actualmente en una variable para usarlo en otras operaciones (como USERS, SEND, etc.) y para enviarlo al servidor cuando sea necesario (por ejemplo, en la operación USERS para que el servidor sepa quién está solicitando la lista de usuarios conectados). Al guardar el usuario conectado actualmente, el cliente puede mantener un estado interno sobre quién está conectado y usar esa información para interactuar con el servidor de manera más eficiente y personalizada.
            
            if   res == 0: print("CONNECT OK")
            elif res == 1: print("CONNECT FAIL, USER DOES NOT EXIST")
            elif res == 2: print("USER ALREADY CONNECTED")
            else:          print("CONNECT FAIL")

            if res != 0:
                client._listening = False
                client._listen_socket.close() #se cierra el hilo de escucha si no se ha podido conectar, para evitar que quede escuchando sin necesidad

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
            client._socket.send(user.encode('utf-8') + b'\0') # Enviamos el nombre del usuario con encode (para codificarlo a bytes) y b'\0' para indicar el final de la cadena, siguiendo el protocolo definido para las operaciones del cliente-servidor. Esto permite al servidor interpretar correctamente el nombre del usuario que se está desconectando y realizar las acciones necesarias para actualizar su estado y liberar los recursos asociados a ese usuario.
            client._listening = False #Detenemos el hilo de escucha para que deje de aceptar conexiones entrantes del servidor, ya que el cliente se va a desconectar y no necesita seguir recibiendo mensajes.
            res = struct.unpack('B', client._socket.recv(1))[0]
            if   res == 0: print("DISCONNECT OK")
            elif res == 1: print("DISCONNECT FAIL, USER DOES NOT EXIST")
            elif res == 2: print("DISCONNECT FAIL, USER NOT CONNECTED")
            else:          print("DISCONNECT FAIL")
            client._socket.close() #cerramos socket de la operación DISCONNECT, el hilo de escucha ya se ha detenido y cerrado, por lo que no es necesario cerrarlo aquí.
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
                    name = b"" # Se usa name = b"" para crear un objeto de tipo byte vacío en el que se irán acumulando los bytes recibidos del servidor hasta encontrar el byte de terminación '\0'.
                    while True:
                        c = client._socket.recv(1) # Se lee un byte del socket, que representa un carácter del nombre de usuario. El servidor envía los nombres de usuario como cadenas de bytes terminadas en '\0', por lo que se lee byte a byte hasta encontrar el byte de terminación.
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
                id_str = b"" # Si el envío fue exitoso, se lee el ID del mensaje asignado por el servidor, que es una cadena de bytes terminada en '\0'. 
                while True:
                    c = client._socket.recv(1)
                    if c == b'\0': break
                    id_str += c
                print(f"SEND OK - MESSAGE {id_str.decode()}") #Se muestra éxito en el envío y el id del mensaje 
            elif res == 1: print("SEND FAIL, USER DOES NOT EXIST")
            else:          print("SEND FAIL")
            return client.RC.OK if res == 0 else client.RC.ERROR
        except Exception as e:
            print(f"Error en SEND: {e}")
            return client.RC.ERROR

    @staticmethod
    def sendAttach(user, file, message):
        try:
            client._socket = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
            client._socket.connect((client._server, client._port))
            client._socket.send(b"SENDATTACH\0")

            sender = getattr(client, '_cur_user', "anon")
            client._socket.send(sender.encode('utf-8') + b'\0')
            client._socket.send(user.encode('utf-8') + b'\0')
            client._socket.send(message.encode('utf-8') + b'\0')
            client._socket.send(file.encode('utf-8') + b'\0')
            
            # --- CORRECCIÓN DE LECTURA ---
            #with open(file, 'rb') as f:
                #while True:
                    #data = f.read(1024) # 1024 o 256 es indiferente, pero 1024 es más eficiente
                    #if not data:
                        #break # Fin del archivo
                    #client._socket.sendall(data) #sendall asegura que se envíe todo el bloque
            
            # IMPORTANTE: Aquí el servidor debe saber que el archivo terminó.
            # Si el protocolo no envía el tamaño antes, el servidor podría quedarse bloqueado.
            # Suponiendo que el servidor detecta el final o tú envías un cierre:
            # client._socket.shutdown(socket.SHUT_WR) # Opcional: indica que no enviarás más datos
            res_raw = client._socket.recv(1) # Esperamos a que el servidor nos envíe la respuesta despues de recibir el archivo.
            if not res_raw: #Si no recibimos nada, significa que cerro la conexión, lo que indica un error en el envío.
                return client.RC.ERROR
                
            res = struct.unpack('B', res_raw)[0] #Devolver el resultado de la operación, que es un byte que indica si el envío fue exitoso o no. El servidor debe enviar esta respuesta después de procesar el mensaje y el archivo adjunto, para que el cliente sepa si todo se ha recibido correctamente.
            
            if res == 0:
                id_str = b""
                while True:
                    c = client._socket.recv(1)
                    if not c or c == b'\0': break
                    id_str += c
                print(f"SEND OK - MESSAGE {id_str.decode()}")
                return client.RC.OK
            elif res == 1: 
                print("SEND FAIL, USER DOES NOT EXIST")
            else:          
                print("SEND FAIL")
            
            client._socket.shutdown(socket.SHUT_WR) #Cerramos el socket de envío después de recibir la respuesta del servidor, para indicar que no se enviarán más datos y permitir que el servidor procese la solicitud y libere los recursos asociados a esa conexión. Esto es especialmente importante después de enviar un archivo adjunto, ya que el servidor necesita saber cuándo ha terminado de recibir los datos para poder procesarlos correctamente.
            
            return client.RC.ERROR

        except Exception as e:
            print(f"Error en SEND: {e}")
            return client.RC.ERROR
    
    def getfile(username, remote_filename, local_filename):

        if username not in client._connected_users:
            #Hacemos un users para buscar si el usuario que queremos que nos envíe los datos si no los tenemos.
            s = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
            s.connect((client._server, client._port))
            s.send(b"USERS\0")
            user_name = getattr(client, '_cur_user', "anon")
            s.send(user_name.encode('utf-8') + b'\0')
            res = struct.unpack('B', s.recv(1))[0]
            if res == 0:
                count_str = b""
                while True:
                    c = s.recv(1)
                    if c == b'\0': break
                    count_str += c
                count = int(count_str.decode())
                for _ in range(count):
                    name = b""
                    while True:
                        c = s.recv(1)
                        if c == b'\0': break
                        name += c
                    parts = name.decode().split("::")
                    if len(parts) == 3:
                        uname, uip, uport = parts[0].strip(), parts[1].strip(), parts[2].strip()
                        client._connected_users[uname] = (uip, int(uport))
            s.close() 

            if username not in client._connected_users:
                print(f"c> GET_FILE FAIL, {username} not connected")
                return client.RC.ERROR 

            sender_ip, sender_port = client._connected_users[username]    

            #Connectarse al socket del emisor
            s_list = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
            try:
                s_list.connect((sender_ip, sender_port))
            except Exception as e:
                print(f"c> GET_FILE FAIL, cannot connect to {username}: {e}")
                return client.RC.ERROR
            s_list.send(b"GETFILE\0")
            s_list.send(remote_filename.encode('utf-8') + b'\0')


            response = s_list.recv(1)#Lo que se recibe

            if not response:
                print("GET_FILE FAIL")
                return client.RC.ERROR

            response_code = response[0]
            if response_code != 0:
                print("c> GET_FILE FAIL")
                s_list.close()
                return client.RC.ERROR
            
            #Leer el tamaño del fichero
            size_str = b""

            while True:
                c =s_list.recv(1)
                if not c or c == b'\0': break
                size_str  += c
            size_str = int(size_str.decode())
            
            received = 0
            try:
                with open(local_filename, "wb") as f:
                    while received < size_str:
                        data = s_list.recv(min(4096,size_str-received))
                        if not data:
                            break
                        f.write(data)
                        received += len(data)
            except Exception as e:
                print(f"c> GET_FILE FAIL: {e}")
                s_list.close()
                return client.RC.ERROR

            if received == size_str:
                print("GET_FILE OK")
                return client.RC.OK
            else:
                if os.path.exists(local_filename):
                    os.remove(local_filename)
                print("GET_FILE FAIL")
                return client.RC.ERROR

        if username not in client._connected_users:
            print("c> FILE TRANSFER FAILED, user not connected.")
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
                elif op == "SEND_ATTACH":
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

                    print(f"\nc> SENDATTACH MESSAGE {sender.decode()} {msg_id.decode()} {filename.decode()} {message.decode()} OK")   
                    print("c> ", end="", flush=True)
                    connection.close()

                elif op == "GETFILE":
                    # Leer nombre del fichero remoto pedido
                    filename = b""
                    while True:
                        c = connection.recv(1)
                        if c == b'\0': break
                        filename += c
                    filename = filename.decode()

                    if os.path.exists(filename):
                        size = os.path.getsize(filename)
                        connection.send(bytes([0]))                          # OK
                        connection.send(str(size).encode() + b'\0')         # tamaño
                        with open(filename, 'rb') as f:
                            while True:
                                data = f.read(4096)
                                if not data: break
                                connection.sendall(data)
                                print(data)
                    else:
                        connection.send(bytes([1])) 
                    print("El getfile funciona")


            except Exception as e:
                if client._listening:
                    print(f"Error en listen_thread: {e}")
                break


    @staticmethod
    def shell():
        while True:
            try: #Primero asignamos a command "c>" para que se muestre siempre antres del input, luego le hacemos el split para separar el comando y sus argumentos, y luego procesamos cada comando según su formato esperado. Si el formato no es correcto, se muestra un mensaje de uso para ese comando específico.
                command = input("c> ")
                line = command.split(" ")
                if not line: #Si no se ha introducido ningún comando, se vuelve a mostrar el prompt sin hacer nada
                    continue
                line[0] = line[0].upper() #Convertimos el comando a mayúsculas para que no sea sensible a mayúsculas/minúsculas, es decir, que se pueda escribir "register", "REGISTER", "Register", etc. y se reconozca como el mismo comando.

                if line[0] == "REGISTER": 
                    if len(line) == 2: client.register(line[1]) # Comando register tiene que tener 2 argumentos y tras verificarlo se llama a la función register con el nombre de usuario (line[1]) como argumento. 
                    # Si el número de argumentos no es correcto, se muestra un mensaje de uso para ese comando específico.
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
                        client.send(line[1], ' '.join(line[2:])) #Longitud igual o mayor a 3 (hay espacios) se imprime el nombre del destinatario (line[1]) y el mensaje (que se obtiene uniendo con espacios los argumentos a partir de line[2] hasta el final de la línea, usando ' '.join(line[2:])) 
                        #y se llama a la función send con esos argumentos. Esto permite enviar mensajes que contienen espacios sin que se interpreten como argumentos separados.
                    else: print("Uso: SEND <usuario> <mensaje>")

                elif line[0] == "SENDATTACH":
                    if len(line) >= 4:
                        #Guardamos los campos para emplearlos luego en el getfile
                        dest     = line[1]
                        filename = line[-1]
                        message  = ' '.join(line[2:-1])
                        client.sendAttach(dest, message, filename) #4 aurgumentos o mas, la operacion, el usuario el fichero y el mensaje, se llama a la función sendAttach con esos argumentos. El mensaje se obtiene uniendo con espacios los argumentos a partir de line[3] hasta el final de la línea, usando ' '.join(line[3:]), para permitir mensajes con espacios.
                    else: print("Uso: SENDATTACH <usuario> <fichero> <mensaje>")
                
                elif line[0] == "GETFILE":
                    # GETFILE <usuario> <fichero_remoto> <fichero_local>
                    if len(line) == 4:
                        client.getfile(line[1], line[2], line[3])
                    else:
                        print("Uso: GETFILE <usuario> <fichero_remoto> <fichero_local>")

                elif line[0] == "QUIT": # solo quit sin argumentos, para salir del cliente
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
            client.usage() #Si los argumentos no son correctos, se muestra el mensaje de uso y se sale del programa.
            return

        client.shell()
        print("+++ FINISHED +++")


if __name__ == "__main__":
    client.main([])