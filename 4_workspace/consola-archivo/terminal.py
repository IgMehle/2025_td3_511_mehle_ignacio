import os
import time

CMD_FILE = "/dev/egb_uart"   # Char device para enviar comandos UART
RX_FILE  = "/dev/egb_uart"   # Archivo donde el driver escribe respuestas
LOG_FILE = "log.txt"         # Archivo donde el driver escribe el log (modo get log)

# ---------------------------------------------------------
# Espera una línea válida en RX_FILE con timeout
# ---------------------------------------------------------
def wait_for_line(timeout=5):
    """Espera hasta que RX_FILE tenga una línea legible.
       Devuelve la línea o None si hace timeout.
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

# ---------------------------------------------------------
# Espera que RX_FILE contenga "ACK" (sin timeout)
# ---------------------------------------------------------
def wait_for_ack():
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

# ---------------------------------------------------------
# Inicializar archivos si no existen
# ---------------------------------------------------------
def init_files():
    if not os.path.exists(LOG_FILE):
        open(LOG_FILE, "w").close()

# ---------------------------------------------------------
# PROGRAMA PRINCIPAL
# ---------------------------------------------------------
def main():
    init_files()

    print("Terminal UART Pico 2")
    print("Comandos disponibles:")
    print("  set lux -v INT -h INT -l INT -a INT")
    print("  get lux")
    print("  get log")
    print("  set rtc day/month/year weekday hour:min:seg")
    print("  exit\n")

    while True:
        try:
            user_input = input(">> ").strip()
            if not user_input:
                continue

            if user_input == "exit":
                print("Saliendo...")
                break

            # =====================================================
            # Envío literal del comando a la UART
            # =====================================================
            with open(CMD_FILE, "w") as f:
                f.write(user_input + "\n")

            # =====================================================
            # Modo especial GET LOG → esperar ACK y leer log.txt
            # =====================================================
            if user_input.startswith("get log"):
                wait_for_ack()
                print("\nACK recibido. Imprimiendo LOG:\n")
                try:
                    with open(LOG_FILE, "r") as f:
                        for line in f:
                            print(line.strip())
                except:
                    print("Error leyendo LOG_FILE")
                continue  # vuelve al prompt

            # =====================================================
            # Modo normal → esperar una línea del RX_FILE
            # =====================================================
            line = wait_for_line(timeout=5)
            if line is None:
                print("[Timeout] No se recibió respuesta.")
            else:
                print(line)

        except KeyboardInterrupt:
            print("\nSaliendo...")
            break
        except Exception as e:
            print(f"Error: {e}")

# ---------------------------------------------------------
if __name__ == "__main__":
    main()
