import time
import sys
import select
import json
import serial

import mqtt_payload
import mqtt_lib

# ────────────────────────────────────────────────
#  Terminal command interface (non-blocking)
# ────────────────────────────────────────────────
def print_help():
    print("\nCommands:")
    print("  send <payload>                    → simulate MQTT → Arduino (downlink)")
    print("  fake <payload>                    → simulate Arduino → MQTT (payload must be JSON)")
    print("  status                            → show connection status")
    print("  help / ?                          → this help")
    print("  ^C                                → stop the bridge\n")
    print("Examples:")
    print("  send 123 21 3123                  # → serial")
    print("  fake {\"id\":1,\"value\":742}       # JSON style to raw topic\n")


def terminal_input_thread(ser: serial.Serial, mqtt_client: mqtt_lib.mqtt.Client) -> None:
    print("\nTerminal control ready. Type 'help' for commands.\n")

    while True:
        if select.select([sys.stdin], [], [], 0.1)[0]:
            try:
                line = sys.stdin.readline().rstrip()
            except:
                continue

            if not line.strip():
                continue

            cmd = line.strip()

            if cmd in ('help', '?'):
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