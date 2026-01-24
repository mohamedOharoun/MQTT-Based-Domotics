import { useState, useEffect } from "react";
import mqtt, { MqttClient } from "mqtt";
import "./index.css";

import webLogo from "./assets/logo.svg";
import MonitorView from "./MonitorView";
import type { MQTTServerConnectionStatus } from "./models/mqtt_status";

const HOST = "localhost";
const PORT = 9001;

export function App() {
	const MQTT_SERVER_URL= `ws://${HOST}:${PORT}`;

  const [client, setClient] = useState<MqttClient | null>(null);
	const [connectionStatus, setConnectionStatus] = useState<MQTTServerConnectionStatus>("disconnected");
  const [error, setError] = useState<string | null>(null);
  
	useEffect(() => {
		const mqttClient = mqtt.connect(MQTT_SERVER_URL, {
      clientId: `mqtt_web_client_${Math.random().toString(16).slice(2, 8)}`,
      clean: true,
      reconnectPeriod: 1000,
    });

		mqttClient.on("error", (err) => {
      console.error("MQTT error:", err);
      setError(err.message);
      setConnectionStatus("disconnected");
    });

		mqttClient.on("reconnect", () => {
      setConnectionStatus("connecting");
    });

		// Connect to MQTT broker via WebSocket
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

    setClient(mqttClient);
		setConnectionStatus("connecting");

		return () => {
			mqttClient.end();
		};
	}, []);

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
			<MonitorView mqttClient={client} connectionStatus={connectionStatus} />

			{error && (
        <div className="p-4 bg-red-900/50 border border-red-600 rounded text-sm">
          <strong>Error:</strong> {error}
        </div>
      )}

      <div className="flex justify-center items-center my-6 text-center text-sm text-gray-500">
        <p>WebSocket connection to localhost:9001. Messages are limited to the last 100 entries</p>
      </div>
	</div>);
}

export default App;
