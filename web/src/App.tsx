import { useState, useEffect } from "react";
import mqtt, { MqttClient } from "mqtt";
import "./index.css";

import webLogo from "./assets/logo.svg";
import type { MessageRecord, MQTTMessage } from "./models/mqtt_msg";
import type { LightSensorData, SensorDataMessage, UltrasonicSensorData } from "./models/sensor_data";
import type { EventTriggerMessage, EventType } from "./models/events";
import type { MsgType } from "./models/incoming_msg";

const HOST = "localhost";
const PORT = 9001;

type MQTTServerConnectionStatus = "disconnected" | "connecting" | "connected";

export function App() {
	const MQTT_SERVER_URL= `ws://${HOST}:${PORT}`;

  const [messages, setMessages] = useState<MessageRecord[]>([]);
  const [client, setClient] = useState<MqttClient | null>(null);
  const [connectionStatus, setConnectionStatus] = useState<MQTTServerConnectionStatus>("disconnected");
  const [error, setError] = useState<string | null>(null);
  
  // Store partial sensor data keyed by node_id
  const [partialLightData, setPartialLightData] = useState<Map<string, Partial<LightSensorData>>>(new Map());
  const [partialUltrasonicData, setPartialUltrasonicData] = useState<Map<string, Partial<UltrasonicSensorData>>>(new Map());

  const createSensorMessage = (
    nodeId: string,
    sensorType: "light" | "ultrasonic",
    data: LightSensorData | UltrasonicSensorData,
    topic: string
  ): MessageRecord => {
    return {
      node_id: nodeId,
      msg_type: "sensor_data",
      timestamp: Math.floor(Date.now() / 1000),
      msg_id: Math.floor(Math.random() * 1000000),
      sensor_type: sensorType,
      data: data,
      id: `${Date.now()}-${Math.random()}`,
      receivedAt: new Date(),
      topic,
    };
  };

  useEffect(() => {
    // Connect to MQTT broker via WebSocket
    const mqttClient = mqtt.connect(MQTT_SERVER_URL, {
      clientId: `mqtt_web_client_${Math.random().toString(16).slice(2, 8)}`,
      clean: true,
      reconnectPeriod: 1000,
    });

    mqttClient.on("connect", () => {
      console.log("Connected to MQTT broker");
      setConnectionStatus("connected");
      setError(null);
      
      // Subscribe to all topics (adjust based on your topic structure)
      mqttClient.subscribe("#", (err) => {
        if (err) {
          console.error("Subscription error:", err);
          setError("Failed to subscribe to topics");
        }
      });
    });

    mqttClient.on("error", (err) => {
      console.error("MQTT error:", err);
      setError(err.message);
      setConnectionStatus("disconnected");
    });

    mqttClient.on("reconnect", () => {
      setConnectionStatus("connecting");
    });

    mqttClient.on("message", (topic, payload) => {
      try {
        // Handle params/event topics (complete JSON messages)
        if (topic.includes("params/event/")) {
          const message = JSON.parse(JSON.parse(payload.toString())) as MQTTMessage;
          console.log("Received event message:", message);
          const messageRecord: MessageRecord = {
            ...message,
            id: `${Date.now()}-${Math.random()}`,
            receivedAt: new Date(),
            topic,
          };
          setMessages((prev) => [messageRecord, ...prev].slice(0, 100));
        }
        // Handle light sensor topics
        else if (topic.startsWith("sensors/brightness/")) {
          const parts = topic.split("/");
          const nodeId = parts[2] || "unknown"; // NODE_NAME
          const field = parts[3]; // light, als, or estado
          const value = payload.toString();
          
          setPartialLightData((prev) => {
            const updated = new Map(prev);
            const current = updated.get(nodeId) || {};
            
            // Update the specific field
            if (field === "light") {
              current.lux = parseFloat(value);
            } else if (field === "als") {
              current.als = parseInt(value);
            } else if (field === "estado") {
              current.estado = value as "Brillante" | "Oscuro" | "Medio";
            }
            
            updated.set(nodeId, current);
            
            // Check if we have all required fields
            if (current.lux !== undefined && current.als !== undefined && current.estado !== undefined) {
              const completeData: LightSensorData = {
                lux: current.lux,
                als: current.als,
                estado: current.estado,
              };
              
              const message = createSensorMessage(nodeId, "light", completeData, `sensors/brightness/${nodeId}`);
              setMessages((prev) => [message, ...prev].slice(0, 100));
              
              // Clear the partial data for this node
              updated.delete(nodeId);
            }
            
            return updated;
          });
        }
        // Handle ultrasonic sensor topics
        else if (topic.startsWith("sensors/distance/")) {
          const parts = topic.split("/");
          const nodeId = parts[2] || "unknown"; // NODE_NAME
          const field = parts[3]; // distance_cm or estado
          const value = payload.toString();
          
          setPartialUltrasonicData((prev) => {
            const updated = new Map(prev);
            const current = updated.get(nodeId) || {};
            
            // Update the specific field
            if (field === "distance_cm") {
              current.distance_cm = parseFloat(value);
            } else if (field === "estado") {
              current.estado = value as "Cerca" | "Lejos" | "Medio";
            }
            
            updated.set(nodeId, current);
            
            // Check if we have all required fields
            if (current.distance_cm !== undefined && current.estado !== undefined) {
              const completeData: UltrasonicSensorData = {
                distance_cm: current.distance_cm,
                estado: current.estado,
              };
              
              const message = createSensorMessage(nodeId, "ultrasonic", completeData, `sensors/distance/${nodeId}`);
              setMessages((prev) => [message, ...prev].slice(0, 100));
              
              // Clear the partial data for this node
              updated.delete(nodeId);
            }
            
            return updated;
          });
        }
        
        console.log("Topic:", topic, "Payload:", payload.toString());
      } catch (err) {
				console.error("Error processing message:", err);
			}
    });

    setClient(mqttClient);
    setConnectionStatus("connecting");

    return () => {
      mqttClient.end();
    };
  }, []);

  const clearMessages = () => {
    setMessages([]);
  };

  const formatTimestamp = (timestamp: number) => {
    return new Date(timestamp * 1000).toLocaleString();
  };

  const formatReceivedAt = (date: Date) => {
    return date.toLocaleTimeString();
  };

  const renderSensorData = (message: MessageRecord) => {
    if (message.msg_type !== "sensor_data") return null;
    
    const sensorMsg = message as SensorDataMessage;
    
    if (sensorMsg.sensor_type === "light") {
      const data = sensorMsg.data as LightSensorData;
      return (
        <div className="text-xs">
          <div>Lux: {data.lux.toFixed(2)}</div>
          <div>ALS: {data.als}</div>
          <div>Estado: {data.estado}</div>
        </div>
      );
    } else if (sensorMsg.sensor_type === "ultrasonic") {
      const data = sensorMsg.data as UltrasonicSensorData;
      return (
        <div className="text-xs">
          <div>Distance: {data.distance_cm.toFixed(2)} cm</div>
          <div>Estado: {data.estado}</div>
        </div>
      );
    }
  };

  const renderEventData = (message: MessageRecord) => {
    if (message.msg_type !== "event") return null;

		console.log(message);
    
    const eventMsg = message as EventType;
    return (
      <div className="text-xs">
        <div>Sensor: {eventMsg.sensor_type}</div>
        <div>Threshold: {eventMsg.trigger_threshold}</div>
        <div>Type: {eventMsg.trigger_type}</div>
        <div>Active: {eventMsg.is_active ? "Yes" : "No"}</div>
      </div>
    );
  };

  const getMessageTypeColor = (msgType: MsgType) => {
    switch (msgType) {
      case "sensor_data": return "bg-blue-600";
      case "alert": return "bg-red-600";
      case "event": return "bg-yellow-600";
      case "command": return "bg-green-600";
      case "status": return "bg-purple-600";
      case "config": return "bg-orange-600";
      default: return "bg-gray-600";
    }
  };

  return (
    <div className="min-h-screen flex flex-col relative z-10 w-full">
      <header className="bg-[#1a1a1a] border-b border-gray-800 p-2 mb-4">
        <div className="max-w-full mx-auto flex items-center justify-between">
          <img
            src={webLogo}
            alt="MQTT Logo"
            className="h-12 transition-all duration-300 hover:drop-shadow-[0_0_2em_#61dafbaa]"
          />
          <h1 className="text-2xl font-bold">MQTT Message Monitor</h1>
          <div className="w-12"></div>
        </div>
      </header>

      <div className="flex-1 flex flex-col gap-4 mx-auto w-[95%]">
        <div className="flex flex-col gap-4 max-w-2xl mx-auto w-[65%]">
					<div className="flex items-center gap-2">
          <div className={`w-3 h-3 rounded-full ${
            connectionStatus === "connected" ? "bg-green-500" : 
            connectionStatus === "connecting" ? "bg-yellow-500 animate-pulse" : 
            "bg-red-500"
          }`}></div>
          <span className="text-sm font-medium capitalize">{connectionStatus}</span>
        </div>
        
        <button
          onClick={clearMessages}
          className="px-4 py-2 bg-red-600 hover:bg-red-700 rounded text-sm font-medium transition-colors"
        >
          Clear Messages
        </button>
        
        <span className="text-sm text-gray-400">Total: {messages.length}</span>
				</div>

				<div className="overflow-x-auto flex-1">
          <table className="w-full text-sm bg-[#1a1a1a] rounded-lg shadow-xl overflow-hidden border border-gray-800">
            <thead className="bg-[#2a2a2a] border-b border-gray-700 sticky top-0">
              <tr>
                <th className="px-6 py-4 text-left font-semibold">Received</th>
                <th className="px-6 py-4 text-left font-semibold">Topic</th>
                <th className="px-6 py-4 text-left font-semibold">Node ID</th>
                <th className="px-6 py-4 text-left font-semibold">Type</th>
                <th className="px-6 py-4 text-left font-semibold">MQTT Msg ID</th>
                <th className="px-6 py-4 text-left font-semibold">Timestamp</th>
                <th className="px-6 py-4 text-left font-semibold">Data</th>
              </tr>
            </thead>
            <tbody className="divide-y divide-gray-800">
              {messages.length === 0 ? (
                <tr>
                  <td colSpan={7} className="px-6 py-12 text-center text-gray-500">
                    {connectionStatus === "connected" 
                      ? "No messages received yet. Waiting for data..."
                      : "Connecting to MQTT broker..."}
                  </td>
                </tr>
              ) : (
                messages.map((msg) => (
                  <tr key={msg.id} className="hover:bg-[#2a2a2a] transition-colors">
                    <td className="px-4 py-3 whitespace-nowrap text-gray-400">
                      {formatReceivedAt(msg.receivedAt)}
                    </td>
                    <td className="px-4 py-3 font-mono text-xs text-blue-400">
                      {msg.topic}
                    </td>
                    <td className="px-4 py-3 font-mono">
                      {"node_id" in msg ? msg.node_id : "-"}
                    </td>
                    <td className="px-4 py-3">
                      <span className={`px-2 py-1 rounded text-xs font-medium ${getMessageTypeColor(msg.msg_type)}`}>
                        {msg.msg_type}
                      </span>
                    </td>
                    <td className="px-4 py-3">
                      {"msg_id" in msg ? msg.msg_id : "-"}
                    </td>
                    <td className="px-4 py-3 text-xs text-gray-400">
                      {"timestamp" in msg ? formatTimestamp(msg.timestamp) : "-"}
                    </td>
                    <td className="px-4 py-3">
                      {msg.msg_type === "sensor_data" && renderSensorData(msg)}
                      {msg.msg_type === "event" && renderEventData(msg)}
                    </td>
                  </tr>
                ))
              )}
            </tbody>
          </table>
        </div>
      </div>
			
			{error && (
        <div className="p-4 bg-red-900/50 border border-red-600 rounded text-sm">
          <strong>Error:</strong> {error}
        </div>
      )}

      <div className="flex justify-center items-center my-6 text-center text-sm text-gray-500">
        <p>WebSocket connection to localhost:9001. Messages are limited to the last 100 entries</p>
      </div>
    </div>
  );
}

export default App;
