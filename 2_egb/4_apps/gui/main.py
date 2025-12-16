# main.py

from PyQt5 import QtWidgets
from GUI_controller import GUIController
from ui_mainwindow import Ui_MainWindow

if __name__ == "__main__":     # Garantiza que este archivo se ejecute solo cuando es el script principal    
    import sys
    app = QtWidgets.QApplication(sys.argv)

    main_window = QtWidgets.QMainWindow()   #Crear la ventana principal (QWidget real)
    
    #Crear la interfaz y vincularla a la ventana
    ui = Ui_MainWindow()
    ui.setupUi(main_window)


    #Crear el controlador, pasándole ambos objetos
    gui = GUIController(ui, main_window) #gui = GUIController(main_window)  
    
    #Muestra la ventana
    main_window.show()          #Muestra la ventana
    sys.exit(app.exec_())       #Inicio del evento loop



