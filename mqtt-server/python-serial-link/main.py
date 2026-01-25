import threading

import mqtt_lib
import arduino_lib
import terminal_cli

# ────────────────────────────────────────────────
#  Configuration
# ────────────────────────────────────────────────
SERIAL_PORT    = '/dev/cu.usbmodem14101'     # ← change as needed
BAUD_RATE      = 9600
MQTT_BROKER    = 'localhost'
MQTT_PORT      = 1883

# ────────────────────────────────────────────────
#  Main
# ────────────────────────────────────────────────
def main():
    global ser
    global mqtt_client

    ser = arduino_lib.setup_serial(SERIAL_PORT, BAUD_RATE)
    mqtt_client = mqtt_lib.setup_mqtt(MQTT_BROKER, MQTT_PORT)
    
    mqtt_client.on_message = arduino_lib.on_mqtt_message

    print("\nBidirectional MQTT ↔ Serial bridge started")
    print(  "───────────────────────────────────────")

    # Terminal input in background thread
    input_thread = threading.Thread(target=terminal_cli.terminal_input_thread, daemon=True, kwargs={'ser': ser, 'mqtt_client': mqtt_client})
    input_thread.start()

    # Serial → MQTT in main thread
    try:
        arduino_lib.serial_to_mqtt_loop(mqtt_client)
    except KeyboardInterrupt:
        print("\nKeyboardInterrupt received")
    finally:
        print("\nShutting down...")
        if mqtt_client:
            mqtt_client.loop_stop()
            mqtt_client.disconnect()
        if ser and ser.is_open:
            ser.close()
        print("Done.")


if __name__ == '__main__':
    main()