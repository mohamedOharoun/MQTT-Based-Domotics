import time
import threading
import sys
import select
import json

import mqtt_lib
import mqtt_payload
import arduino_lib

# ────────────────────────────────────────────────
#  Configuration
# ────────────────────────────────────────────────
SERIAL_PORT    = '/dev/ttyACM0'     # ← change as needed
BAUD_RATE      = 9600
MQTT_BROKER    = 'localhost'
MQTT_PORT      = 1883

# ────────────────────────────────────────────────
#  Globals / State
# ────────────────────────────────────────────────
ser = None
mqtt_client = None
running = True

# ────────────────────────────────────────────────
#  Terminal command interface (non-blocking)
# ────────────────────────────────────────────────
def print_help():
    print("\nCommands:")
    print("  send <payload>                    → simulate MQTT → Arduino (downlink)")
    print("  fake <payload>                    → simulate Arduino → MQTT (payload must be JSON)")
    print("  status                            → show connection status")
    print("  help / ?                          → this help")
    print("  q / quit / exit / ^C              → stop the bridge\n")
    print("Examples:")
    print("  send 123 21 3123                  # → serial")
    print("  fake {\"id\":1,\"value\":742}       # JSON style to raw topic\n")


def terminal_input_thread():
    global running

    print("\nTerminal control ready. Type 'help' for commands.\n")

    while running:
        if select.select([sys.stdin], [], [], 0.1)[0]:
            try:
                line = sys.stdin.readline().rstrip()
            except:
                continue

            if not line.strip():
                continue

            cmd = line.strip()

            if cmd in ('q', 'quit', 'exit'):
                running = False
                break

            elif cmd in ('help', '?'):
                print_help()

            elif cmd == 'status':
                ser_status = 'open' if (ser and ser.is_open) else 'closed/unavailable'
                mqtt_status = 'connected' if (mqtt_client and mqtt_client.is_connected()) else 'not connected'
                print(f"Serial : {ser_status}")
                print(f"MQTT   : {mqtt_status}")

            # ── Downlink: MQTT → Arduino simulation ────────────────────────
            elif cmd.startswith('send '):
                payload = cmd[5:].strip()
                if payload:
                    print(f"Simulating downlink → {payload!r}")
                    if ser and ser.is_open:
                        ser.write((payload + '\n').encode('utf-8', errors='replace'))
                        print(f"   → serial : {payload}")
                    else:
                        print("   (serial not available)")

            # ── Uplink: Arduino → MQTT simulation ──────────────────────────
            elif cmd.startswith('fake '):
                rest = cmd[5:].strip()
                if not rest:
                    print("Usage: fake <payload>  (from raw JSON only)")
                    continue
                
                try:
                    json_payload = mqtt_payload.parse_raw_payload(rest)
                    mqtt_lib.act_on_payload(json_payload, mqtt_client)
                except json.JSONDecodeError:
                    print("Usage: fake <payload> (error parsing JSON)")
                    continue
            else:
                print(f"Unknown command: {cmd}")
                print("Type 'help' for available commands")

        time.sleep(0.05)

# ────────────────────────────────────────────────
#  Main
# ────────────────────────────────────────────────
def main():
    global running
    global ser
    global mqtt_client

    ser = arduino_lib.setup_serial(SERIAL_PORT, BAUD_RATE)
    mqtt_client = mqtt_lib.setup_mqtt(MQTT_BROKER, MQTT_PORT)
    
    mqtt_client.on_message = arduino_lib.on_mqtt_message

    print("\nBidirectional MQTT ↔ Serial bridge started")
    print(  "───────────────────────────────────────")

    # Terminal input in background thread
    input_thread = threading.Thread(target=terminal_input_thread, daemon=True)
    input_thread.start()

    # Serial → MQTT in main thread
    try:
        arduino_lib.serial_to_mqtt_loop(mqtt_client)
    except KeyboardInterrupt:
        print("\nKeyboardInterrupt received")
    finally:
        running = False
        print("\nShutting down...")
        if mqtt_client:
            mqtt_client.loop_stop()
            mqtt_client.disconnect()
        if ser and ser.is_open:
            ser.close()
        print("Done.")


if __name__ == '__main__':
    main()