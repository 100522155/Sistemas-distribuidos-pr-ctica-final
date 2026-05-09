from spyne import Application, ServiceBase, Unicode, rpc
from spyne.protocol.soap import Soap11
from spyne.server.wsgi import WsgiApplication
from wsgiref.simple_server import make_server

class MensajeService(ServiceBase):
    """ Servicio que normaliza un mensaje.
    Elimina los espacios en blanco repetidos de un texto."""

    @rpc(Unicode, _returns=Unicode)
    def normalizar_mensaje(ctx, mensaje):
        # Primero utilizamos split() para dividir el mensaje en palabras y elimina los espacios en blanco 
        # que haya antes o despues de la palabra y luego unimos las palabras con un solo espacio
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
    host = '127.0.0.1'
    port = 8000
    wsgi_app = WsgiApplication(application)
    server = make_server(host, port, wsgi_app)
    
    print(f"Servidor Web de Normalización escuchando en http://{host}:{port}")
    print(f"WSDL disponible en: http://{host}:{port}/?wsdl")
    server.serve_forever()