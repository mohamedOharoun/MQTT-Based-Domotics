import paho.mqtt.client as mqtt
import mqtt_payload
from paho.mqtt.client import ReasonCodes

DOWNLINK_TOPIC = 'params/event/#'   # what you publish to → goes to Arduino

def setup_mqtt(broker: str = "localhost", port: int = 1883) -> None:
    mqtt_client = mqtt.Client(protocol=mqtt.MQTTv311, callback_api_version=mqtt.CallbackAPIVersion.VERSION2)

    mqtt_client.on_connect = on_connect

    try:
        mqtt_client.connect(broker, port, 60)
        mqtt_client.loop_start()
    except Exception as e:
        print(f"Cannot connect to MQTT broker: {e}")
        print("Continuing anyway (you can still use terminal simulation)")
    
    return mqtt_client
                    
def upload(mqtt_client: mqtt.Client, mqtt_topic_prefix: str, topic: str, payload: str, retain: bool = False, qos: int = 0):
    if mqtt_client and mqtt_client.is_connected():
        full_topic = mqtt_topic_prefix + topic
        try:
            mqtt_client.publish(full_topic, payload, qos=qos, retain=retain)
            print(f"Simulated uplink → MQTT {full_topic} ← {payload!r}")
        except Exception as e:
            print(f"MQTT publish failed: {e}")
    else:
        print("MQTT not connected — cannot publish")
        
def upload_light_sensor_data(mqtt_client: mqtt.Client, sensor_data: mqtt_payload.SensorData):
    payload = {
        f"sensors/brightness/{sensor_data.node_id}/light": sensor_data.data.lux,
        f"sensors/brightness/{sensor_data.node_id}/als": sensor_data.data.als,
        f"sensors/brightness/{sensor_data.node_id}/estado": sensor_data.data.estado,
        f"status/{sensor_data.node_id}/last_update": sensor_data.timestamp
    }
    
    for topic_suffix, value in payload.items():
        topic_is_status = "status" in topic_suffix
        qos = 2 if topic_is_status else 0
        upload(mqtt_client, "", topic_suffix, str(value), retain=topic_is_status, qos=qos)
        
def upload_ultrasonic_sensor_data(mqtt_client: mqtt.Client, sensor_data: mqtt_payload.SensorData):
    payload = {
        f"sensors/distance/{sensor_data.node_id}/distance_cm": sensor_data.data.distance_cm,
        f"sensors/distance/{sensor_data.node_id}/estado": sensor_data.data.estado,
        f"status/{sensor_data.node_id}/last_update": sensor_data.timestamp
    }
    
    for topic_suffix, value in payload.items():
        topic_is_status = "status" in topic_suffix
        qos = 2 if topic_is_status else 0
        upload(mqtt_client, "", topic_suffix, str(value), retain=topic_is_status, qos=qos)
        
def upload_sensor_data(mqtt_client: mqtt.Client, sensor_data: mqtt_payload.SensorData):
    match sensor_data.sensor_type:
        case "light":
            upload_light_sensor_data(mqtt_client, sensor_data)
        case "ultrasonic":
            upload_ultrasonic_sensor_data(mqtt_client, sensor_data)

def upload_event_trigger(mqtt_client: mqtt.Client, event_trigger: mqtt_payload.EventTrigger):
    topic = f"params/event/alerts/{event_trigger.node_id}_{event_trigger.event.sensor_type}"
    payload = mqtt_payload.to_raw_payload(event_trigger)
    upload(mqtt_client, "", topic, payload, retain=False, qos=1)

def act_on_payload(
    payload: mqtt_payload.SensorData | mqtt_payload.EventTrigger,
    mqtt_client: mqtt.Client
):
    match payload:
        case mqtt_payload.SensorData():
            print(f"source → MQTT  parsed sensor payload: {payload}")
            upload_sensor_data(mqtt_client, payload)
        case mqtt_payload.EventTrigger():
            print(f"callback to alert triggered: {payload}")
            upload_event_trigger(mqtt_client, payload)
        case _:
            print(f"source → MQTT  unknown payload type: {payload}")
            
# ────────────────────────────────────────────────
#  MQTT callbacks
# ────────────────────────────────────────────────
def on_connect(client: mqtt.Client, userdata, flags: mqtt.ConnectFlags, rc: ReasonCodes, *args, **kwargs):
    if rc == 0:
        print(f"Connected to MQTT broker (code {rc})")
        client.subscribe(DOWNLINK_TOPIC)
        print(f"  Subscribed to ↓ {DOWNLINK_TOPIC}")
    else:
        print(f"MQTT connection failed (code {rc})")