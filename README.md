# MQTT Based Domotics

## Python Serial Bridge Communication

### Overview
This Python app provides a bridge to communicate the Arduino master node commands via serial with the MQTT Server with event based communication.
It includes a CLI to control and monitor your MQTT-based domotics system via serial communication and MQTT server.

Features:
- Send payloads to the Arduino via serial
- Send raw JSON payloads to the MQTT server
- Automatic bridged communication on serial message to the MQTT server
- Automatic bridged communication on MQTT message to the serial port

### Installation

1. Navigate to the `python-serial-link` directory
2. Install dependencies:
```bash
python -m venv venv
source venv/bin/activate
pip install -r requirements.txt
```

### Usage

Run the terminal CLI:
```bash
source venv/bin/activate # if not done already
python main.py
```

Example for faking received data from Arduino Serial as JSON to send to the MQTT server:
- Distance Sensor
```bash
fake "{\n  \"node_id\": \"DISTANCE_01\",\n  \"msg_type\": \"sensor_data\",\n  \"timestamp\": 12345,\n  \"sensor_type\": \"ultrasonic\",\n  \"msg_id\": 42,\n  \"data\": {\n    \"distance_cm\": 123,\n    \"estado\": \"Cerca\"\n  }\n}"
```

- Light Sensor
```bash
fake "{\n  \"node_id\": \"LIGHT_01\",\n  \"msg_type\": \"sensor_data\",\n  \"timestamp\": 12345,\n  \"sensor_type\": \"light\",\n  \"msg_id\": 42,\n  \"data\": {\n    \"lux\": 456.7,\n    \"als\": 1234,\n    \"estado\": \"Brillante\"\n  }\n}"
```

- Triggered event payload
```bash
fake "{\n \"node_id\": \"node_03\",\n \"msg_type\": \"alert\",\n \"event\": {\n   \"msg_type\": \"event\",\n   \"sensor_type\": \"ultrasonic\",\n   \"trigger_threshold\": 10.0,\n   \"trigger_type\": \"below\",\n   \"is_active\": true\n }\n}"
```
> For this type of payload, timestamp is attached by the Python bridge, to save communication bandwidth in the Serial channel with the Arduino.

### Components

- **main.py** - Entry point for the CLI application
- **arduino_lib.py** - Arduino communication library
- **mqtt_lib.py** - MQTT protocol utilities
- **mqtt_payload.py** - Payload serialization/deserialization
- **terminal_cli.py** - Interactive command-line interface

### Configuration

Ensure your MQTT broker is running with the configuration in `config/mosquitto.conf` before starting the CLI.

### Troubleshooting

Check the `log/` directory for application logs if you encounter issues.
