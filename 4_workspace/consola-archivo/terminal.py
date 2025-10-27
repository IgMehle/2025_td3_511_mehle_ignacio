import os
from datetime import datetime

LOG_FILE = "log.txt"

def init_log_file():
    # Crea el archivo si no existe
    if not os.path.exists(LOG_FILE):
        open(LOG_FILE, "w").close()

def cmd_set(args):
    # Comando SET: guarda una línea en el log
    if len(args) != 8:
        print("Uso: set -l <lux> -u <up> -d <down> -a <ajuste>")
        return
    
    # Tomo los valores correspondientes a cada argumento
    try:
        lux = float(args[args.index("-l") + 1])
        up = float(args[args.index("-u") + 1])
        down = float(args[args.index("-d") + 1])
        ajuste = float(args[args.index("-a") + 1])
    except (ValueError, IndexError):
        print("Error: argumentos inválidos.")
        return
    
    # Lee el número de entrada actual
    with open(LOG_FILE, "r") as f:
        lines = f.readlines()
    entry_number = len(lines) + 1
    
    # Fecha y hora actual
    fecha = datetime.now().strftime("%Y-%m-%d %H:%M:%S")
    
    # Crea la línea de log
    line = f"{entry_number}: {fecha} - Lux={lux} - Up={up} - Down={down} - Ajuste={ajuste}\n"
    
    # Escribe en archivo
    with open(LOG_FILE, "a") as f:
        f.write(line)
    
    print(f"[OK] Registro agregado: {line.strip()}")

def cmd_get(_args):
    # Comando GET: devuelve el último valor de Lux guardado
    # Leo el archivo
    with open(LOG_FILE, "r") as f:
        lines = f.readlines()
    if not lines:
        print("No hay registros.")
        return
    last_line = lines[-1]
    try:
        # Separo la ultima linea desde "Lux=", y separo el dato numerico usando " -"
        lux_str = last_line.split("Lux=")[1].split(" -")[0]
        print(f"Último valor de Lux: {lux_str}")
    except IndexError:
        print("Formato de línea inválido.")

def cmd_log(_args):
    # Comando LOG: imprime todas las líneas del archivo
    with open(LOG_FILE, "r") as f:
        lines = f.readlines()
    if not lines:
        print("El archivo de log está vacío.")
        return
    print("---- Contenido del log ----")
    for line in lines:
        print(line.strip())

def cmd_clear(_args):
    # Comando CLEAR: borra el contenido del archivo
    open(LOG_FILE, "w").close()
    print("[OK] Archivo log borrado.")

def main():
    # Inicializa el archivo de log
    init_log_file()
    print("Aplicación de terminal iniciada.")
    print("Comandos disponibles: set, get, log, clear, exit")

    while True:
        try:
            # imprimmo el prompt y separo la entrada
            user_input = input(">> ").strip()
            # si no ingreso nada, continuo
            if not user_input:
                continue
            
            # separo la entrada por espacios en command + args
            parts = user_input.split()
            command = parts[0].lower()
            args = parts[1:]
            
            # disparo funciones segun el comando
            if command == "set":
                cmd_set(args)
            elif command == "get":
                cmd_get(args)
            elif command == "log":
                cmd_log(args)
            elif command == "clear":
                cmd_clear(args)
            elif command == "exit":
                print("Saliendo...")
                break
            else:
                print(f"Comando desconocido: {command}")
                print("Comandos disponibles: set, get, log, clear, exit")

        except KeyboardInterrupt:
            print("\nSaliendo...")
            break
        except Exception as e:
            print(f"Error: {e}")

if __name__ == "__main__":
    main()
