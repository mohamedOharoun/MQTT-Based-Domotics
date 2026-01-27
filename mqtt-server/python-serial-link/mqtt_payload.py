import json
import dataclasses
from dataclasses import dataclass
import time
from typing import Literal

MsgType = Literal["sensor_data", "command", "status", "config", "alert", "event"]
SensorType = Literal["light", "ultrasonic"]

@dataclass
class LightSensorData:
    """
    Sensor data structure for light sensor readings.
    """
    
    lux: float
    als: int
    estado: Literal["Brillante", "Oscuro", "Medio"]
    
    # LightSensorData example:
    # {
    #     "node_id": "node_01",
    #     "msg_type": "sensor_data",
    #     "timestamp": 1699564800,
    #     "msg_id": 1,
    #     "sensor_type": "light",
    #     "data": {
    #         "lux": 450.5,
    #         "als": 120,
    #         "estado": "Medio"
    #     }
    # }

@dataclass
class UltraSonicSensorData:
    """
    Sensor data structure for ultrasonic sensor readings.
    """
    
    distance_cm: float
    estado: Literal["Cerca", "Lejos", "Medio"]
    
    # UltraSonicSensorData example:
    # {
    #     "node_id": "node_02",
    #     "msg_type": "sensor_data",
    #     "timestamp": 1699564800,
    #     "msg_id": 2,
    #     "sensor_type": "ultrasonic",
    #     "data": {
    #         "distance_cm": 25.5,
    #         "estado": "Cerca"
    #     }
    # }

@dataclass
class SensorData:
    """
    Sensor data message structure for incoming Arduino sensor readings.
    """
    
    node_id: str
    msg_type: MsgType
    timestamp: int
    msg_id: int
    sensor_type: SensorType
    data: LightSensorData | UltraSonicSensorData
    
    @staticmethod
    def from_dict(data: dict) -> 'SensorData':
        if not "sensor_type" in data:
            raise ValueError("Missing sensor_type in data")
        
        if data["sensor_type"] == "light":
            sensor_data = LightSensorData(**data["data"])
        elif data["sensor_type"] == "ultrasonic":
            sensor_data = UltraSonicSensorData(**data["data"])
        else:
            raise ValueError(f"Unknown sensor type: {data['sensor_type']}")
        
        return SensorData(
            node_id=data["node_id"],
            msg_type=data["msg_type"],
            timestamp=data["timestamp"],
            msg_id=data["msg_id"],
            sensor_type=data["sensor_type"],
            data=sensor_data
        )
        
@dataclass
class EventType:
    msg_type: MsgType
    sensor_type: SensorType
    trigger_threshold: float | int
    trigger_type: Literal["above", "below", "equal"]
    alert_message: str
    is_active: bool
    
    # EventType example:
    #     {
    #         "msg_type": "event",
    #         "sensor_type": "ultrasonic",
    #         "trigger_threshold": 10.0,
    #         "trigger_type": "below",
    #         "alert_message": "Something happened",
    #         "is_active": true
    #     }
    
    @staticmethod
    def from_dict(data: dict) -> 'EventType':
        return EventType(
            msg_type=data["msg_type"],
            sensor_type=data["sensor_type"],
            trigger_threshold=data["trigger_threshold"],
            trigger_type=data["trigger_type"],
            alert_message=data.get("alert_message", ""),
            is_active=data["is_active"]
        )

@dataclass
class EventTrigger:
    node_id: str
    msg_type: MsgType
    event: EventType
    timestamp: int
    
    # EventTrigger example:
    # {
    #     "node_id": "node_03",
    #     "msg_type": "alert",
    #     "timestamp": 173927397333,
    #     "event": {
    #         "msg_type": "event",
    #         "sensor_type": "ultrasonic",
    #         "trigger_threshold": 10.0,
    #         "trigger_type": "below",
    #         "alert_message": "Something happened",
    #         "is_active": true
    #     }
    # }
    
    @staticmethod
    def from_dict(data: dict) -> 'EventTrigger':
        event_data = EventType(**data["event"])    
        return EventTrigger(
            node_id=data["node_id"],
            msg_type=data["msg_type"],
            event=event_data,
            timestamp=int(time.time())
        )



def parse_raw_payload(raw_payload: str) -> SensorData | EventTrigger | EventType:
    data_dict = json.loads(raw_payload)
    
    if isinstance(data_dict, str):
        data_dict = json.loads(data_dict)
    
    match data_dict.get("msg_type"):
        case "sensor_data":
            return SensorData.from_dict(data_dict)
        case "alert":
            return EventTrigger.from_dict(data_dict)
        case "event":
            return EventType.from_dict(data_dict)
        case _:
            raise ValueError(f"Unknown msg_type: {data_dict.get('msg_type')}")

def to_raw_payload(data: SensorData | EventTrigger) -> str:
    return json.dumps(dataclasses.asdict(data))