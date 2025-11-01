import os
from datetime import datetime

LOG_FILE = "log.txt"

def init_log_file():
    # Crea el archivo si no existe
    if not os.path.exists(LOG_FILE):
        open(LOG_FILE, "w").close()

def parse_args(args, expected_flags):
    # Parsea los argumentos tipo -x valor en cualquier orden
    result = {}
    i = 0
    while i < len(args):
        arg = args[i]
        if arg.startswith("-") and arg in expected_flags:
            # Verifica que haya un valor siguiente
            if i + 1 < len(args):
                value = args[i + 1]
                result[arg] = value
                i += 2
            else:
                print(f"Error: falta un valor para el argumento {arg}")
                return None
        else:
            i += 1

    # Verifica que todos los flags esperados estén presentes
    for flag in expected_flags:
        if flag not in result:
            print(f"Error: falta el argumento {flag}")
            return None

    return result

def cmd_set_lux(args):
    # Comando: set lux -v <valor> -h <up> -l <down> -a <ajuste>
    expected_flags = ["-v", "-h", "-l", "-a"]
    parsed = parse_args(args[1:], expected_flags)  # args[0] es "lux"
    if parsed is None:
        print("Uso: set lux -v <valor> -h <up> -l <down> -a <ajuste>")
        return

    try:
        lux = int(parsed["-v"])
        up = int(parsed["-h"])
        down = int(parsed["-l"])
        ajuste = "LENTA" if int(parsed["-a"]) > 0 else "RAPIDA"
    except ValueError:
        print("Error: los argumentos deben ser numéricos.")
        return

    # Lee número de entrada actual
    with open(LOG_FILE, "r") as f:
        lines = f.readlines()
    entry_number = len(lines) + 1

    # Fecha y hora actual
    fecha = datetime.now().strftime("%Y-%m-%d %H:%M:%S")

    # Crea línea de log
    line = f"{entry_number}: {fecha} - Lux={lux} - High={up} - Low={down} - Ajuste={ajuste}\n"

    # Escribe en archivo
    with open(LOG_FILE, "a") as f:
        f.write(line)

    print(f"[OK] Registro agregado: {line.strip()}")

def cmd_get_lux():
    # Comando: get lux → devuelve el último valor de Lux guardado
    with open(LOG_FILE, "r") as f:
        lines = f.readlines()
    if not lines:
        print("No hay registros.")
        return
    last_line = lines[-1]
    try:
        lux_str = last_line.split("Lux=")[1].split(" -")[0]
        print(f"Último valor de Lux: {lux_str}")
    except IndexError:
        print("Formato de línea inválido.")

def cmd_get_log():
    # Comando: get log → imprime todas las líneas del archivo
    with open(LOG_FILE, "r") as f:
        lines = f.readlines()
    if not lines:
        print("El archivo de log está vacío.")
        return
    print("---- Contenido del log ----")
    for line in lines:
        print(line.strip())

def cmd_set_clear():
    # Comando: set clear → borra el contenido del archivo
    open(LOG_FILE, "w").close()
    print("[OK] Archivo log borrado.")

def cmd_set_rtc(args):
    # Comando: set rtc "day/month/year weekday hour:min:seg"
    if len(args) < 2:
        print('Uso: set rtc "day/month/year weekday hour:min:seg"')
        return

    # Junta todo lo que venga después de rtc como una sola cadena
    date_str = " ".join(args[1:]).strip('"')

    # Guarda en archivo rtc.txt y muestra en consola
    with open("rtc.txt", "w") as f:
        f.write(date_str + "\n")

    print(f"RTC configurado a: {date_str}")

def main():
    # Inicializa el archivo de log
    init_log_file()
    print("Aplicación de terminal iniciada.")
    print("Comandos disponibles:")
    print("  set lux -v INT -h INT -l INT -a INT")
    print("  get lux")
    print("  get log")
    print('  set rtc "day/month/year weekday hour:min:seg"')
    print("  set clear")
    print("  exit")

    while True:
        try:
            user_input = input(">> ").strip()
            if not user_input:
                continue

            parts = user_input.split()
            command = parts[0].lower()
            args = parts[1:]

            if command == "set":
                if not args:
                    print("Uso: set <lux|rtc|clear> ...")
                    continue
                subcmd = args[0].lower()
                if subcmd == "lux":
                    cmd_set_lux(args)
                elif subcmd == "rtc":
                    cmd_set_rtc(args)
                elif subcmd == "clear":
                    cmd_set_clear()
                else:
                    print(f"Comando set desconocido: {subcmd}")

            elif command == "get":
                if not args:
                    print("Uso: get <lux|log>")
                    continue
                subcmd = args[0].lower()
                if subcmd == "lux":
                    cmd_get_lux()
                elif subcmd == "log":
                    cmd_get_log()
                else:
                    print(f"Comando get desconocido: {subcmd}")

            elif command == "exit":
                print("Saliendo...")
                break

            else:
                print(f"Comando desconocido: {command}")
                print("Comandos disponibles: set, get, exit")

        except KeyboardInterrupt:
            print("\nSaliendo...")
            break
        except Exception as e:
            print(f"Error: {e}")

if __name__ == "__main__":
    main()
