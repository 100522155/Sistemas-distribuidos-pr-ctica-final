from spyne import Application, ServiceBase, Unicode, rpc
from spyne.protocol.soap import Soap11
from spyne.server.wsgi import WsgiApplication
from wsgiref.simple_server import make_server
import logging

class MensajeService(ServiceBase):
    @rpc(Unicode, _returns=Unicode)
    def normalizar_mensaje(ctx, mensaje):
        # Lógica: split() sin argumentos elimina todos los espacios en blanco 
        # (espacios, tabs, etc) y join() los une con un solo espacio.
        palabras = mensaje.split()
        resultado = " ".join(palabras)
        print(f"[WS] Transformado: '{mensaje}' -> '{resultado}'")
        return resultado

# Configuración de la aplicación Spyner
application = Application(
    services=[MensajeService],
    tns='http://ssdd.practica.ws/',
    in_protocol=Soap11(validator='lxml'),
    out_protocol=Soap11()
)

if __name__ == '__main__':
    # Usamos el puerto 8000 como en vuestro ejemplo
    host = '127.0.0.1'
    port = 8000
    wsgi_app = WsgiApplication(application)
    server = make_server(host, port, wsgi_app)
    
    print(f"Servidor Web de Normalización escuchando en http://{host}:{port}")
    print(f"WSDL disponible en: http://{host}:{port}/?wsdl")
    server.serve_forever()