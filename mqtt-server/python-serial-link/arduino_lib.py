import serial
import time
import json

import mqtt_payload
import mqtt_lib

_active_serial = None

def setup_serial(port: str, baud_rate: int) -> serial.Serial | None:
    global _active_serial
    
    try:
        _active_serial = serial.Serial(port, baud_rate, timeout=1)
        time.sleep(2.5)  # Arduino reset delay
        print(f"Serial port opened: {port} @ {baud_rate}")
    except Exception as e:
        print(f"Serial open failed: {e}")
        print("Continuing without serial (MQTT → terminal only)")
    
    return _active_serial

def read_serial_line() -> str | None:
    if _active_serial and _active_serial.is_open and _active_serial.in_waiting > 0:
        try:
            line = _active_serial.readline().decode('utf-8', errors='replace').strip()
            return line
        except Exception as e:
            print(f"Serial read error: {e}")
    return None

# ────────────────────────────────────────────────
#  Serial → MQTT uplink
#  Protocol: comm:start${raw json data}$comm:end
# ────────────────────────────────────────────────
START_MARKER = "comm:start$"
END_MARKER = "$comm:end"

def serial_to_mqtt_loop(mqtt_client: mqtt_lib.mqtt.Client) -> None:
    while True:
        try:
            line = read_serial_line()
            if not line:
                continue
            
            if line.startswith(START_MARKER) and line.endswith(END_MARKER):
                raw_payload = line[len(START_MARKER):-len(END_MARKER)]
                try:
                    json_payload = mqtt_payload.parse_raw_payload(raw_payload)
                except json.JSONDecodeError:
                    print(f"serial → MQTT  invalid JSON payload: {raw_payload!r}")
                    continue
                
                mqtt_lib.act_on_payload(json_payload, mqtt_client)
        except Exception as e:
            print(f"Serial read error: {e}")

        time.sleep(0.02)

def write_event_update_to_serial(event_trigger: mqtt_payload.EventType) -> None:
    if _active_serial and _active_serial.is_open:
        raw_payload = mqtt_payload.to_raw_payload(event_trigger)
        framed_payload = f"{START_MARKER}{raw_payload}{END_MARKER}\n"
        try:
            _active_serial.write(framed_payload.encode('utf-8', errors='replace'))
            print(f"MQTT → serial  sent event update: {framed_payload.strip()!r}")
        except Exception as e:
            print(f"Serial write error: {e}")
            
def on_mqtt_message(client, userdata, msg: mqtt_lib.mqtt.MQTTMessage):
    try:
        payload = msg.payload.decode('utf-8').strip()
    except:
        print("MQTT message decode failed")
        return

    print(f"← MQTT  {msg.topic:20}  |  {payload}")
    
    # downlink from params/event/#
    if msg.topic.startswith(mqtt_lib.DOWNLINK_TOPIC[:-1]):
        try:
            event_payload = mqtt_payload.parse_raw_payload(payload)
            if isinstance(event_payload, mqtt_payload.EventType):
                print("MQTT → serial  updating event trigger list on Arduino")
                write_event_update_to_serial(event_payload)
        except json.JSONDecodeError:
            print("MQTT → serial  invalid JSON payload on downlink topic")