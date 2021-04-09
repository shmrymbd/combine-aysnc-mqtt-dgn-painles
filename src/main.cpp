#include <Arduino.h>
// Example project which can be built with SSL enabled or disabled.
// The espressif8266_stage platform must be installed.
// Refer to platformio.ini for the build configuration and platform installation.

#include <Arduino.h>
#include <ESP8266WiFi.h>
#include <Ticker.h>
#include <AsyncMqttClient.h>
#include <painlessMesh.h>
#include <WiFiClient.h>

#define   MESH_PREFIX     "whateverYouLike"
#define   MESH_PASSWORD   "somethingSneaky"
#define   MESH_PORT       5555


#define  WIFI_SSID "loranet"
#define  WIFI_PASSWORD "1qaz2wsx"
#define HOSTNAME "MQTT_Bridge"

// #define MQTT_HOST IPAddress(143, 198, 209, 63) //143.198.209.63

#if ASYNC_TCP_SSL_ENABLED
#define MQTT_SECURE true
// #define MQTT_SERVER_FINGERPRINT {0x02, 0xae, 0x3b, 0x15, 0xea, 0x1a, 0x2a, 0xe4, 0xdd, 0x2e, 0x5c, 0x4e, 0x7b, 0x55, 0xb5, 0xab, 0xf1, 0x17, 0x91, 0xe9}  //mosquttio 
// 70:1D:2F:1F:FC:5E:7A:C9:12:32:A2:C9:8A:C9:EE:91:8E:0B:82:45
#define MQTT_SERVER_FINGERPRINT {0x70, 0x1d, 0x2f, 0x1f, 0xfc, 0x5e, 0x7a, 0xc9, 0x12, 0x32, 0xa2, 0xc9, 0x8a, 0xc9, 0xee, 0x91, 0x8e, 0x0b, 0x82, 0x45} //emqx
#define MQTT_PORT 8888
#else
#define MQTT_PORT 1883
#endif

// IPAddress getlocalIP();
// IPAddress myIP(0,0,0,0);
IPAddress mqttBroker(143, 198, 209, 63);

AsyncMqttClient mqttClient;
Ticker mqttReconnectTimer;

WiFiEventHandler wifiConnectHandler;
WiFiEventHandler wifiDisconnectHandler;
Ticker wifiReconnectTimer;

// // Prototypes
void receivedCallback( const uint32_t &from, const String &msg );
void mqttCallback(char* topic, byte* payload, unsigned int length);


painlessMesh  mesh;
WiFiClient wifiClient;

void connectToWifi() {
  Serial.println("Connecting to Wi-Fi...");
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
}

void connectToMqtt() {
  Serial.println("Connecting to MQTT...");
  mqttClient.connect();
}

void onWifiConnect(const WiFiEventStationModeGotIP& event) {
  Serial.println("Connected to Wi-Fi.");
  connectToMqtt();
}

void onWifiDisconnect(const WiFiEventStationModeDisconnected& event) {
  Serial.println("Disconnected from Wi-Fi.");
  mqttReconnectTimer.detach(); // ensure we don't reconnect to MQTT while reconnecting to Wi-Fi
  wifiReconnectTimer.once(2, connectToWifi);
}



void onMqttConnect(bool sessionPresent) {
  Serial.println("Connected to MQTT.");
  mqttClient.publish("painlessMesh/from/gateway",1,true,"Ready!");
  mqttClient.subscribe("painlessMesh/to/#", 2);
}

void onMqttDisconnect(AsyncMqttClientDisconnectReason reason) {
  Serial.println("Disconnected from MQTT.");

  if (reason == AsyncMqttClientDisconnectReason::TLS_BAD_FINGERPRINT) {
    Serial.println("Bad server fingerprint.");
  }

  if (WiFi.isConnected()) {
    mqttReconnectTimer.once(2, connectToMqtt);
  }
}

void onMqttSubscribe(uint16_t packetId, uint8_t qos) {
  Serial.println("Subscribe acknowledged.");
  Serial.print("  packetId: ");
  Serial.println(packetId);
  Serial.print("  qos: ");
  Serial.println(qos);
}

void onMqttUnsubscribe(uint16_t packetId) {
  Serial.println("Unsubscribe acknowledged.");
  Serial.print("  packetId: ");
  Serial.println(packetId);
}

void onMqttMessage(char* topic, char* payload, AsyncMqttClientMessageProperties properties, size_t len, size_t index, size_t total) {
  
  Serial.println("Publish received.");
  
  String msg = String(payload);

  String targetStr = String(topic).substring(16);

  if(targetStr == "gateway")
  {
    if(msg == "getNodes")
    {
      auto nodes = mesh.getNodeList(true);
      String mesg;
      for (auto &&id : nodes)
        mesg += String(id) + String(" ");
      mqttClient.publish("painlessMesh/from/gateway", 1, true, mesg.c_str());
      // mqttClient.publish("painlessMesh/from/gateway",1,true,"Ready!");
    }
  }
  else if(targetStr == "broadcast") 
  {
    mesh.sendBroadcast(msg);
  }
  else
  {
    uint32_t target = strtoul(targetStr.c_str(), NULL, 10);
    if(mesh.isConnected(target))
    {
      mesh.sendSingle(target, msg);
    }
    else
    {
      mqttClient.publish("painlessMesh/from/gateway", 1, true,"Client not connected!");
    }
  }
}


void onMqttPublish(uint16_t packetId) {
  Serial.println("Publish acknowledged.");
  Serial.print("  packetId: ");
  Serial.println(packetId);
}



void receivedCallback( const uint32_t &from, const String &msg ) {
  Serial.printf("bridge: Received from %u msg=%s\n", from, msg.c_str());
  String topic = "painlessMesh/from/" + String(from);
  mqttClient.publish(topic.c_str(), 1, true, msg.c_str());
}

void setup() {
  Serial.begin(115200);
  Serial.println();
  Serial.println();

  mesh.setDebugMsgTypes( ERROR | STARTUP | CONNECTION );  // set before init() so that you can see startup messages

  // Channel set to 6. Make sure to use the same channel for your mesh and for you other
  // network (STATION_SSID)
  mesh.init( MESH_PREFIX, MESH_PASSWORD, MESH_PORT, WIFI_AP_STA, 6 );
  mesh.onReceive(&receivedCallback);

  mesh.stationManual(WIFI_SSID, WIFI_PASSWORD);
  mesh.setHostname(HOSTNAME);

  // Bridge node, should (in most cases) be a root node. See [the wiki](https://gitlab.com/painlessMesh/painlessMesh/wikis/Possible-challenges-in-mesh-formation) for some background
  mesh.setRoot(true);
  // This node and all other nodes should ideally know the mesh contains a root, so call this on all nodes
  mesh.setContainsRoot(true);

  wifiConnectHandler = WiFi.onStationModeGotIP(onWifiConnect);
  wifiDisconnectHandler = WiFi.onStationModeDisconnected(onWifiDisconnect);

  mqttClient.onConnect(onMqttConnect);
  mqttClient.onDisconnect(onMqttDisconnect);
  mqttClient.onSubscribe(onMqttSubscribe);
  mqttClient.onUnsubscribe(onMqttUnsubscribe);
  mqttClient.onMessage(onMqttMessage);
  mqttClient.onPublish(onMqttPublish);
  mqttClient.setServer(mqttBroker, MQTT_PORT );
  mqttClient.setCredentials("admin", "pajero999");

#if ASYNC_TCP_SSL_ENABLED
  mqttClient.setSecure(MQTT_SECURE);
  if (MQTT_SECURE) {
    mqttClient.addServerFingerprint((const uint8_t[])MQTT_SERVER_FINGERPRINT);
  }
#endif

  connectToWifi();
}

void loop() {
  mesh.update();
  //mqttClient.loop();

  // if(myIP != getlocalIP()){
  //   myIP = getlocalIP();
  //   Serial.println("My IP is " + myIP.toString());
  // }
}