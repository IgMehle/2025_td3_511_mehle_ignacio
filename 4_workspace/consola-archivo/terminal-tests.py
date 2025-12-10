import os
from datetime import datetime
import time

# RX_FILE = "rx.txt"          # Archivo de lineas recibidas por UART
CMD_FILE = "/dev/egb_uart"  # Char Device de escritura de UART
RX_FILE = "/dev/egb_uart"   # Archivo de lineas recibidas por UART
LOG_FILE = "log.txt"        # Archivo de logs recibidos por UART

def wait_for_line(timeout = 5):
    """Espera hasta que RX_FILE tenga al menos 1 línea nueva.
       Devuelve la línea o None si hay timeout.
    """
    start = time.time()
    while True:
        try:
            with open(RX_FILE, "r") as f:
                lines = f.readlines()
                if len(lines) > 0:
                    return lines[-1].strip()
        except:
            pass

        if (time.time() - start) > timeout:
            return None
        time.sleep(0.1)

def wait_for_ack():
    """Bloquea hasta que RX_FILE contenga la línea 'ACK'."""
    print("Esperando ACK del dispositivo...")

    while True:
        try:
            with open(RX_FILE, "r") as f:
                lines = f.readlines()
                for line in lines:
                    if "ACK" in line:
                        return True
        except:
            pass
        time.sleep(0.1)


def init_log_file():
    # Crea los archivos si no existen
    if not os.path.exists(LOG_FILE):
        open(LOG_FILE, "w").close()

def parse_args(args, expected_flags):
    result = {}
    i = 0
    while i < len(args):
        arg = args[i]
        if arg.startswith("-") and arg in expected_flags:
            if i + 1 < len(args):
                value = args[i + 1]
                result[arg] = value
                i += 2
            else:
                print(f"Error: falta un valor para el argumento {arg}")
                return None
        else:
            i += 1
    for flag in expected_flags:
        if flag not in result:
            print(f"Error: falta el argumento {flag}")
            return None
    return result

################################################################
#   CMD_SET_LUX()
################################################################
def cmd_set_lux(args):
    expected_flags = ["-v", "-h", "-l", "-a"]
    parsed = parse_args(args[1:], expected_flags)
    if parsed is None:
        print("Uso: set lux -v <valor> -h <up> -l <down> -a <ajuste>")
        return

    try:
        lux = int(parsed["-v"])
        up = int(parsed["-h"])
        down = int(parsed["-l"])
        ajuste = int(parsed["-a"])
    except ValueError:
        print("Error: los argumentos deben ser numéricos.")
        return

    with open(RX_FILE, "r") as f:
        lines = f.readlines()
    entry_number = len(lines) + 1

    fecha = datetime.now().strftime("%Y-%m-%d %H:%M:%S")

    # Nuevo formato del log
    line = f"[{entry_number}] ({fecha}) Lux={lux} - High={up} - Low={down} - Ajuste={ajuste}\n"

    with open(CMD_FILE, "") as f:
        f.write(line)

    print(f"[OK] Registro agregado: {line.strip()}")

################################################################
#   CMD_GET_LUX()
################################################################
def cmd_get_lux():
    with open(RX_FILE, "r") as f:
        lines = f.readlines()
    if not lines:
        print("No hay registros.")
        return
    last_line = lines[-1]
    try:
        lux_str = last_line.split("Lux =")[1].split(" -")[0]
        print(f"Último valor de Lux: {lux_str}")
    except IndexError:
        print("Formato de línea inválido.")

################################################################
#   CMD_GET_LOG()
################################################################
def cmd_get_log():
    with open(RX_FILE, "r") as f:
        lines = f.readlines()
    if not lines:
        print("El archivo de log está vacío.")
        return
    print("---- Contenido del log ----")
    for line in lines:
        print(line.strip())

################################################################
#   CMD_SET_ECLEAR()
################################################################
def cmd_set_eclear():
    open(RX_FILE, "w").close()
    print("[OK] Archivo log borrado.")

################################################################
#   CMD_SET_RTC()
################################################################
def cmd_set_rtc(args):
    # Comando: set rtc day/month/year weekday hour:min:seg
    if len(args) != 4:
        print('Uso: set rtc <day/month/year> <weekday> <hour:min:seg>')
        return

    date_str = " ".join(args[1:])

    with open("rtc.txt", "w") as f:
        f.write(date_str + "\n")

    print(f"RTC configurado a: {date_str}")

################################################################
#   MAIN()
################################################################
def main():
    init_log_file()
    print("Aplicación de terminal iniciada.")
    print("Comandos disponibles:")
    print("  set lux -v INT -h INT -l INT -a INT")
    print("  get lux")
    print("  get log")
    print("  set rtc day/month/year weekday hour:min:seg")
    print("  set eclear")
    print("  exit")

    while True:
        try:
            user_input = input(">> ").strip()
            if not user_input:
                continue

            # === ENVÍO DE COMANDO UART ===
            with open(CMD_FILE, "w") as f:
                if user_input != "exit":
                    f.write(user_input + "\n")

            # === MODO NORMAL: ESPERO RESPUESTA UNA SOLA LÍNEA ===
            if user_input.startswith("get log"):
                # MODO ESPECIAL GET LOG
                wait_for_ack()
                print("ACK recibido. Imprimiendo LOG...\n")
                try:
                    with open(LOG_FILE, "r") as f:
                        for line in f:
                            print(line.strip())
                except:
                    print("Error leyendo LOG_FILE")
                # NO SE VUELVE AUTOMÁTICAMENTE — QUEDA EN PROMPT
                continue

            # MODO NORMAL (timeout 5 segundos)
            line = wait_for_line(timeout=5)
            if line is None:
                print("[Timeout] No se recibió respuesta del dispositivo.")
            else:
                print(line)

            parts = user_input.split()
            command = parts[0].lower()
            args = parts[1:]

            if command == "set":
                if not args:
                    print("Uso: set <lux|rtc|eclear> ...")
                    continue
                subcmd = args[0].lower()
                if subcmd == "lux":
                    cmd_set_lux(args)
                elif subcmd == "rtc":
                    cmd_set_rtc(args)
                elif subcmd == "clear":
                    cmd_set_eclear()
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
