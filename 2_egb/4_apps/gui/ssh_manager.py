# ssh_manager.py

import paramiko

class SSHManager:
    def __init__(self):
        self.client = None
        self._is_connected = False
        self.host = None

    def connect(self, host, username, password=None, port=22, timeout=10):
        """
        Intenta establecer conexión SSH con el host indicado.
        Retorna (ok: bool, mensaje: str)
        """
        try:
            self.client = paramiko.SSHClient()
            self.client.set_missing_host_key_policy(paramiko.AutoAddPolicy())
            self.client.connect(
                hostname=host,
                username=username,
                password=password,
                port=port,
                timeout=timeout
            )
            self._is_connected = True
            self.host = host
            return True, f"✅ Conectado a {host} como {username}"
        except Exception as e:
            self._is_connected = False
            self.client = None
            return False, f"❌ Error de conexión: {e}"

    def disconnect(self):
        """Cierra la conexión SSH."""
        try:
            if self.client:
                self.client.close()
                self.client = None
            self._is_connected = False
            host = self.host or "<desconocido>"
            self.host = None
            return True, f"🔌 Desconectado de {host}"
        except Exception as e:
            return False, f"❌ Error al desconectar: {e}"

    def is_connected(self):
        """Devuelve True si hay una conexión SSH activa."""
        return (
            self.client is not None
            and self.client.get_transport() is not None
            and self.client.get_transport().is_active()
        )
    

    def execute_command(self, command):
        
            #Ejecuta un comando remoto por SSH y devuelve su salida.
            #Retorna (ok: bool, salida: str)
    
        if not self.is_connected():
                return False, "⚠️ No hay conexión SSH activa."

        try:
            stdin, stdout, stderr = self.client.exec_command(command)
            salida = stdout.read().decode("utf-8").strip()
            error = stderr.read().decode("utf-8").strip()

            if error:
                return False, f"❌ Error al ejecutar: {error}"

            return True, salida if salida else "(sin salida)"
        
        except Exception as e:
            return False, f"❌ Excepción al ejecutar comando: {e}"


