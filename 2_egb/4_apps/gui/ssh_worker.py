# ssh_worker.py

from PyQt5.QtCore import QObject, pyqtSignal, pyqtSlot
from ssh_manager import SSHManager

class SSHWorker(QObject):
    finished = pyqtSignal(bool, str)   # Señal cuando termina una acción (exito(ok) o error , mensaje)
    progress = pyqtSignal(str)         # Señales intermedias de progreso (texto)

    def __init__(self):
        super().__init__()
        self.manager = SSHManager()

    @pyqtSlot(dict)
    def do_connect(self, params):
        host = params.get("host")
        user = params.get("user")
        password = params.get("password")

        self.progress.emit(f"Iniciando conexión con {host}...")
        ok, msg = self.manager.connect(host, user, password)
        self.finished.emit(ok, msg)

    @pyqtSlot()
    def do_disconnect(self):
        self.progress.emit("Cerrando conexión...")
        ok, msg = self.manager.disconnect()
        self.finished.emit(ok, msg)
