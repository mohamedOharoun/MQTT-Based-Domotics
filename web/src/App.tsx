import { useState, useEffect } from "react";
import mqtt, { MqttClient } from "mqtt";
import "./index.css";

import webLogo from "./assets/logo.svg";
import MonitorView from "./views/MonitorView";
import type { MQTTServerConnectionStatus } from "./models/mqtt_status";
import { MessageProvider } from "./hooks/MessageContext";
import EventManagerView from "./views/EventManagerView";

const HOST = "localhost";
const PORT = 9001;

export function App() {
	const MQTT_SERVER_URL= `ws://${HOST}:${PORT}`;

  const [client, setClient] = useState<MqttClient | null>(null);
	const [connectionStatus, setConnectionStatus] = useState<MQTTServerConnectionStatus>("disconnected");
  const [error, setError] = useState<string | null>(null);

	const [toggleMonitor, setToggleMonitor] = useState(true);
  
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

		mqttClient.on("connect", () => {
			console.log("Connected to MQTT broker");
			setConnectionStatus("connected");
			setError(null);
			
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
	<MessageProvider mqttClient={client}>
		<div className="min-h-screen flex flex-col relative z-10 w-full">
			<header className="bg-[#1a1a1a] border-b border-gray-800 p-2 mb-4">
				<div className="max-w-full mx-auto flex items-center justify-between">
					<img
						src={webLogo}
						alt="MQTT Logo"
						className="h-12 w-24 transition-all duration-300 hover:drop-shadow-[0_0_2em_#61dafbaa]"
					/>
					<h1 className="text-2xl font-bold">MQTT Message Monitor</h1>
					<div className="w-24">
						<button
							onClick={() => setToggleMonitor(!toggleMonitor)}
							className="px-4 py-2 bg-blue-600 hover:bg-blue-700 hover:cursor-pointer rounded text-sm font-medium transition-colors"
						>
							{toggleMonitor ? "Event Manager" : "Monitor View"}
						</button>
					</div>
				</div>
			</header>

			<div className="flex justify-center items-center my-6 text-center text-sm text-gray-500">
				<p>WebSocket connection to {HOST}:{PORT}. Messages are limited to the last 100 entries</p>
			</div>			

			{toggleMonitor ? (
				<MonitorView connectionStatus={connectionStatus} />
			) : (
				<EventManagerView mqtt_client={client}/>
			)}

			{error && (
				<div className="p-4 bg-red-900/50 border border-red-600 rounded text-sm">
					<strong>Error:</strong> {error}
				</div>
			)}
		</div>
	</MessageProvider>
	);
}

export default App;
