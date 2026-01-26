# MQTT Based Domotics

## Arduino node mesh and sensors

### Overview
Arduino-based LoRa mesh network with a master node that aggregates sensor data from slave nodes (light/brightness and ultrasonic sensors). The master node acts as a central hub, communicating with slaves via LoRa and bridging data to the Python serial link for MQTT integration.

### Architecture

**Master Node**: Central coordinator that manages all slave communications and serial bridge
**Slave Nodes**: Distributed sensor nodes (AmbientLight-Sensor, Ultrasonic-Sensor) that transmit data via LoRa

### Master Node

Located in `arduino/LoRa/Master/`:

- **Master.ino** - Main LoRa initialization and message loop
- **SerialReport.ino** - Handles framed serial packet formatting and transmission to Python bridge

#### Master Serial-Bridge Communication Protocol

Serial payload structure (JSON):
```json
{
	"node_id": "LIGHT_01",
	"msg_type": "sensor_data",
	"timestamp": 1234567890,
	"sensor_type": "light|ultrasonic",
	"msg_id": 42,
	"data": {
		"lux": 456.7,
		"als": 1234,
// ------------------------------------------
		"distance_cm": 123
	}
}
```

## LoRa Protocol Data Structure

The LoRa protocol used in this system implements a custom packet structure:

### Packet Format (Binary Layout)

```
[DESTINATION_ADDRESS] [SENDER_ADDRESS] [MSG_ID] [PAYLOAD_LENGTH] [PAYLOAD]
```

### Field Descriptions
- **SENDER AND DESTIONATION ADDRESS** (1 byte): local arduino address to identify over the network

- **MSG_ID** (1 byte): Unique identifier of the LoRa message

- **PAYLOAD_LENGTH** (1 byte): Number of bytes in payload section
  - Range: 0x00-0x50 (0-80 bytes typical)

- **PAYLOAD** (variable length): includes a header with different types of packets and carring data:
	- **MESSAGE_TYPE** (1 byte): Defines packet purpose
	  - **MSG_TYPE_DATA** (0x01): Data transmission from the master
	  - **MSG_TYPE_SENSOR** (0x05): Sensor data received by the master/sent by the slave.
	- **PAYLOAD_DATA** (variable length): data that is being sent over LoRa to be processed by the destination
		- **NODE_ID**: [SENSOR_TYPE (1 bit)] [SENSOR_NUMERIC_ID (7 bits)]


### Transmission Flow

1. Slave node packages data into LoRa frame
2. Transmits via LoRa radio at configured power/frequency
3. Master receives packet and validates header/checksum
4. Parses MESSAGE_TYPE and extracts payload
5. Publishes to MQTT topic: 
- Light sensor: `sensors/brightness/[NODE_ID]/{light,als,estado}`
- Ultrasonic sensor: `sensors/distance/[NODE_ID]/{distance_cm,estado}`
6. Sends acknowledgment back to slave node

#### Serial Bridge Integration

The master formats LoRa-received messages into framed serial packets for the Python bridge:
- Packets are framed with start/end delimiters for reliable parsing
- Each packet contains one complete JSON sensor message
- Serial communication operates at configured baud rate (defined in globals.h)
- Messages are queued and transmitted sequentially to prevent buffer overflow

### Slave Nodes

#### Light Sensor

Located in `arduino/LoRa/LightSlave`

Measures ambient light using analog sensors, transmits via LoRa to master with fields:
- `lux` - Calculated light intensity
- `als` - Raw analog light sensor value

#### Ultrasonic Sensor

Located in `arduino/LoRa/Slave`

Measures distance using ultrasonic sensors, transmits via LoRa with field:
- `distance_cm` - Distance measurement in centimeters

Both sensors include state classification (e.g., "Brillante", "Cerca") based on threshold values.

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

## MQTT Web Monitor

### Overview
A TypeScript/React-based web application for monitoring and managing MQTT-based domotics events in real-time.

Features:
- Real-time sensor data monitoring
- Event management and visualization
- MQTT message integration
- Interactive dashboard interface

### Installation

1. Navigate to the `web` directory
2. Install dependencies:
```bash
bun install
```

### Usage

Start the development server:
```bash
bun dev
```

Build for production:
```bash
bun run build
```

### Project Structure

- **src/App.tsx** - Main application component
- **src/views/** - Page views (MonitorView, EventManagerView)
- **src/models/** - TypeScript data models (sensor_data, mqtt_msg, events)
- **src/hooks/MessageContext.tsx** - React context for MQTT message handling
- **src/index.html** - HTML entry point
- **src/index.css** - Styling

### Components

- **MonitorView** - Real-time sensor data dashboard
- **EventManagerView** - Event management interface

### Configuration

Configure your MQTT broker connection in the application settings (`App.tsx`) before connecting to the web monitor.
