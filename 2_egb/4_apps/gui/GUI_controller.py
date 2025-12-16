# GUI_controller.py

from PyQt5.QtWidgets import QMessageBox
from PyQt5.QtCore import QTimer, QDateTime
from ui_mainwindow import Ui_MainWindow
from ssh_manager import SSHManager
from datetime import datetime




class GUIController:
    def __init__(self, ui, main_window):
        self.ui = ui   
        self.main_window = main_window  # # El QWidget real
        self.ssh_manager = SSHManager()

        # Ruta para los archivos dentro de la Pi-> Ubicación exacta
        #self.char_device = "/home/alelinux/char_device_sim.txt"
        self.char_device = "/dev/egb_uart"
        #self.recepcion = "/home/alelinux/recepcion.txt"
        self.recepcion = "/home/nacho/egb/rx.txt"

        # Conección de señales para botones SSH
        self.ui.btn_connect.clicked.connect(self.connect_ssh)
        self.ui.btn_disconnect.clicked.connect(self.disconnect_ssh)
        # Conección de señales para los botones COMANDOS
        self.ui.BT_SET.clicked.connect(self.enviar_datos)
        self.ui.BT_GET.clicked.connect(self.leer_datos)

        self.ui.BT_SETLOG.clicked.connect(self.enviar_log)
        self.ui.BT_GETLOG.clicked.connect(self.leer_log)
        self.ui.BT_CLEAR.clicked.connect(self.limpiar_memoria)
        self.ui.BT_SETRTC.clicked.connect(self.setear_rtc)
        # Botones de borrado local de logs
        self.ui.pushButton.clicked.connect(self.borrar_log_eventos)
        self.ui.pushButton_2.clicked.connect(self.borrar_log_memoria)
        # Se añade para el RTC 
        self.timer_rtc = QTimer()
        self.timer_rtc.timeout.connect(self.actualizar_rtc)


    #MANEJO DE SSH

    def log_message(self, message):
        #Muestra mensajes en el QTextEdit
        self.ui.textEdit_log.append(message)

    def show_error(self, message):
        #Muestra errores en un QMessageBox
        QMessageBox.critical(None, "Error", message)
        self.log_message(f"[ERROR] {message}")

    def connect_ssh(self):
        ip = self.ui.lineEdit_ip.text().strip()
        user = self.ui.lineEdit_user.text().strip()
        password = self.ui.lineEdit_pass.text().strip()

        if not ip or not user or not password:
            self.show_error("Debe ingresar IP, usuario y contraseña.")
            return

        try:
            print("Intentando comectar a",{ip})             #self.log_message(f"Intentando conectar a {ip}...")
            self.ssh_manager.connect(ip, user, password)
            self.ui.label_status.setText("Conectado ✅")
            print("Conectado correctamente a", {ip})       #self.log_message(f"Conectado correctamente a {ip}.")
            print("DEBUG SSH conectado:", self.ssh_manager.is_connected())  #Estado real de conexión.

        except Exception as e:
            self.show_error(f"Error al conectar: {str(e)}")
            self.ui.label_status.setText("Error ❌")
        
        
    def disconnect_ssh(self):
        try:
            self.ssh_manager.disconnect()
            self.ui.label_status.setText("Desconectado 🔌")
            print("Desconectado correctamente.")        #self.log_message("Desconectado correctamente.")
        except Exception as e:
            self.show_error(f"Error al desconectar: {str(e)}")     #HASTA ACÁ ES MANEJO DE CONEXION SSH.



    #Funcionalidades para los botones COMANDOS#

    def enviar_datos(self):
         
        #Confirmacion de conexion:
        if not self.ssh_manager or not self.ssh_manager.is_connected():
            print("⚠ No hay conexión SSH")      #self.ui.textEdit_log.append("⚠ No hay conexión SSH")
            return

        val = self.ui.lineEdit.text()
        vmax = self.ui.lineEdit_2.text()
        vmin = self.ui.lineEdit_3.text()

        #curva = 1 if self.ui.checkBox.isChecked() else 0
        # Se determina Curva:
        if self.ui.checkBox.isChecked():
            curva = 0
        elif self.ui.checkBox_2.isChecked():
            curva = 1
        else:
            print("⚠ Selecciona una curva (Rápida o Lenta)")       #self.ui.textEdit_log.append("⚠ Selecciona una curva (Rápida o Lenta)") 
            return

        # Se arma comando:
        cmd = f"set lux -v {val} -h {vmax} -l {vmin} -a {curva}"

        #Se escribe en consola
        self.ui.textEdit_log.append(cmd)

        # Se escribe en Raspberry:  
        open(self.recepcion, "w").close()   #ESTO ES LIMPIAR BUFFER DE RECEPCION
        self.ssh_manager.execute_command(f'echo "{cmd}" >> {self.char_device}')
        #self.ssh_manager.execute_command(f'echo "{cmd}" >> {self.recepcion}') 
        #self.ssh_manager.execute_command(f"echo '{cmd}' >> char_device_sim.txt")

        #print("📡 Enviado a Raspberry OK")      #self.ui.textEdit_log.append("📡 Enviado a Raspberry OK")
        print("COMANDO SET LUX ENVIADO OK.")

    def leer_datos(self):
        #Verificacion de estado de conexion
        if not self.ssh_manager.is_connected():
            print("⚠ No hay conexión SSH")     #self.ui.textEdit_log.append("⚠ No hay conexión SSH")
            return

        stdout, stderr = self.ssh_manager.execute_command(f'cat {self.recepcion}')
        #salida = self.ssh_manager.execute_command(f'cat {self.recepcion}')
        #salida = self.ssh_manager.execute_command(f"cat recepcion.txt")

        #Verificacion de lectura
        if not stdout: 
                        #or not isinstance(stdout, str):   
            print("Error leyendo archivo.")
            #self.ui.textEdit_log.append(f"❌ Error leyendo archivo: {stderr}")
            return

      
        lineas = stderr.strip().split("\n")
        ultima = lineas[-1].strip()
        
        if not ultima.startswith("set "):   #Se va a tener que cambiar despues porque hay más comandos
            print("Comando inválido en recepción")      #self.ui.textEdit_log.append(f"❌ Comando inválido en recepción: {ultima}")
            return
    
        try:
            partes = ultima.split()

            valor = partes[partes.index("-v") + 1]
            valor_max = partes[partes.index("-h") + 1]
            valor_min = partes[partes.index("-l") + 1]
            curva = partes[partes.index("-a") + 1]

            self.ui.lineEdit_4.setText(valor)
            self.ui.lineEdit_5.setText(valor_max)
            self.ui.lineEdit_6.setText(valor_min)
            self.ui.lineEdit_7.setText(curva)

            # checkboxes
            if curva == "1":
                self.ui.checkBox.setChecked(True)
                self.ui.checkBox_2.setChecked(False)
            elif curva == "0":
                self.ui.checkBox.setChecked(False)
                self.ui.checkBox_2.setChecked(True)
            else:
                self.ui.checkBox.setChecked(False)
                self.ui.checkBox_2.setChecked(False)

            print("Datos leídos correctamente de Raspberry ✔")
            #self.ui.textEdit_log.append("📥 Datos leídos correctamente de Raspberry ✔")

        except Exception as e:
            print("Error ṕrocesando comando:", {e})
            #self.ui.textEdit_log.append(f"❌ Error procesando comando: {e}")

       



    def enviar_log(self):
        contenido = self.ui.textEdit_log.toPlainText().strip().split("\n")

        for linea in contenido:
            if linea.strip():
                self.ssh_manager.execute_command(f'echo "{linea}" >> {self.char_device}')
                self.ssh_manager.execute_command(f'echo "{linea}" >> {self.recepcion}')

                #self.ssh_manager.execute_command(f"echo '{linea}' >> char_device_sim.txt")

        print("Log enviado a char_device_sim.txt OK")
        #self.ui.textEdit_log.append("✔ Log enviado a char_device_sim.txt")


    """
    def enviar_log(self):
        if not self.ssh_manager or not self.ssh_manager.is_connected():
            QMessageBox.warning(self.main_window, "Error", "No hay conexión SSH activa.")
            return

        try:
            valor_actual = self.ui.lineEdit.text()
            valor_max    = self.ui.lineEdit_2.text()
            valor_min    = self.ui.lineEdit_3.text()

            curva = 1 if self.ui.checkBox.isChecked() else 0

            comando = f"set -v {valor_actual} -h {valor_max} -l {valor_min} -a {curva}"     #Se arma el comando.
            comando_log = f"SET LOG"

            # Escribir el comando en el archivo remoto (char_device_sim)
            ok, salida = self.ssh_manager.execute_command(f"echo '{comando}' >> char_device_sim.txt") # > trunca, >> append 

            # Copiar también en el archivo de recepción
            if ok:
                self.ssh_manager.execute_command(f"echo '{comando}' >> recepcion.txt")
                self.ui.textEdit_log.append(f">> {comando_log}")
                self.ui.textEdit_logMemmoria.append(f"Escrito: {comando}")
            else:
                QMessageBox.warning(self.main_window, "Error SSH", salida)

        except Exception as e:
            QMessageBox.critical(self.main_window, "Error", str(e))
    """

    def leer_log(self):
        stdout, stderr = self.ssh_manager.execute_command(f'cat {self.recepcion}')      #stdout es un tuple: (True/False , "output"), stderr es el texto de salida y setPlainText() solo acepta string.

        if not stdout:
                        #or not isinstance(stdout, str)
            print("Error leyendo archivo")
            #self.ui.textEdit_log.append(f"❌ Error leyendo archivo: {stderr}")
            return

        self.ui.textEdit_logMemmoria.setPlainText(stderr)

        #salida = self.ssh_manager.execute_command(f"cat recepcion.txt")
        #self.ui.textEdit_logMemmoria.setPlainText(salida)
        

    """
    def leer_log(self):
        #Lee el archivo receiver.txt y lo muestra.
        if not self.ssh_manager or not self.ssh_manager.is_connected():
            QMessageBox.warning(self.main_window, "Error", "No hay conexión SSH activa.")
            return
        
        ok, salida = self.ssh_manager.execute_command("cat recepcion.txt")
        if ok:
            self.ui.textEdit_log.append(">> GET LOG")
            self.ui.textEdit_logMemmoria.setPlainText(salida)
        else:
            QMessageBox.warning(self.main_window, "Error", salida)
     """  

    def limpiar_memoria(self):
        #Borra elcontenido de char_device_sim.txt.
        if not self.ssh_manager or not self.ssh_manager.is_connected():
            QMessageBox.warning(self.main_window, "Error", "No hay conexión SSH activa.")
            return
        
        cmd_clear = f"set eclear"
        open(self.recepcion, "w").close()
        self.ssh_manager.execute_command(f'echo "{cmd_clear}" >> {self.char_device}')
 

    def setear_rtc(self):
        """
        #Simula comando set rtc y actualiza el DateTimeEdit.
        self.ui.textEdit_log.append("[SET RTC]")
        now = datetime.now()
        self.ui.dateTimeEdit.setDateTime(now)
        fecha_str = now.strftime("%d/%m/%Y %H:%M:%S")
        self.ui.textEdit_log.append(f">> SET RTC\n{fecha_str}")
        self.ui.textEdit_logMemmoria.append(f"RTC actualizado: {fecha_str}\n")
        """
        
        #print("SET_RTC")
        #self.ui.textEdit_log.append(cmd)
        #self.ssh_manager.execute_command(f'echo "{cmd}" >> {self.char_device}')

        #self.ssh_manager.execute_command(f"echo '{cmd}' >> char_device_sim.txt")     # Envia a char device
        self.ui.dateTimeEdit.setDateTime(QDateTime.currentDateTime())                 #Actualiza localmente con tiempo real
        self.timer_rtc.start(1000)    # Activa actualización cada 1 segundo

        now = datetime.now()                    #Objeto datetime que toma la fecha y la hora actual de la PC.
        
        #fecha_str = now.strftime("%d/%m/%Y")    #Se formatea como d/m/y: dia/mes/año.+
        dia = now.strftime("%d")
        mes = now.strftime("%m")
        
        anno = int(now.strftime("%Y")) 
        m_anno = int(anno/1000)
        anno = anno - m_anno * 1000 
        anno = str(anno)
        
        hora_str = now.strftime("%H:%M:%S")     #Se formatea como h/m/s: hora:minuto:segundo.
        
        dia_semana_num = now.weekday()

        cmd_rtc = f"set rtc {dia}/{mes}/{anno} {dia_semana_num} {hora_str}"     #Armo el comando.
        self.ui.textEdit_log.append(cmd_rtc)                                    #Impresión en log de eventos
        print(cmd_rtc)                                                          #Se imprime el comando en consola.
        
        try:
            open(self.recepcion, "w").close()
            self.ssh_manager.execute_command(f'echo "{cmd_rtc}" >> {self.char_device}')
            print("COMANDO RTC ENVIADO OK.")        
        except Exception as e:
            print("Error ṕrocesando comando:", {e})


    
    def actualizar_rtc(self):
        self.ui.dateTimeEdit.setDateTime(QDateTime.currentDateTime())



    # BORRADO LOCAL DE LOS LOG 
    def borrar_log_eventos(self):
        self.ui.textEdit_log.clear()

    def borrar_log_memoria(self):
        self.ui.textEdit_logMemmoria.clear()

