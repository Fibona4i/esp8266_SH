#include <FS.h>                   //this needs to be first, or it all crashes and burns...
#include <ESP8266WiFi.h>          //https://github.com/esp8266/Arduino
//needed for library
//#include <DNSServer.h>
//#include <ESP8266WebServer.h>
#include <WiFiManager.h>          //https://github.com/tzapu/WiFiManager
#include <ArduinoJson.h>          //https://github.com/bblanchon/ArduinoJson
#include <PubSubClient.h>

#define PROTOCOL_NAMEv31      /* MQTT version 3.1 compatible with Mosquitto v0.15 */
#define STR_SIZE 32

//define your default values here, if there are different values in config.json, they are overwritten.
char mqtt_server[STR_SIZE] = "192.168.1.33";
char mqtt_port[STR_SIZE] = "1884";
char blynk_token[STR_SIZE] = "YOUR_BLYNK_TOKEN";
// After connecting, parameter.getValue() will get you the configured value // id/name placeholder/prompt default length
WiFiManagerParameter custom_mqtt_server("server", "mqtt server", mqtt_server, STR_SIZE);
WiFiManagerParameter custom_mqtt_port("port", "mqtt port", mqtt_port, STR_SIZE);
WiFiManagerParameter custom_blynk_token("blynk", "blynk token", blynk_token, STR_SIZE);

WiFiClient espClient;
PubSubClient client(espClient);
void mqtt_callback(char* topic, byte* payload, unsigned int length);

//callback notifying us of the need to save config
void ssaveConfigCallback () {
    Serial.println("Should save config. Saving ...");

    DynamicJsonBuffer jsonBuffer;
    JsonObject& json = jsonBuffer.createObject();

    json["mqtt_server"] = custom_mqtt_server.getValue();
    json["mqtt_port"] = custom_mqtt_port.getValue();
    json["blynk_token"] = custom_blynk_token.getValue();

    File configFile = SPIFFS.open("/config.json", "w");
    if (!configFile) {
      Serial.println("failed to open config file for writing");
    }

    json.printTo(Serial);
    json.printTo(configFile);
    configFile.close();
    delay(1000);
    //ESP.reset();
}

char *getWiFiMACaddr(void) {
  static char apPassword[18];
  byte mac_[6];

  WiFi.macAddress(mac_);  
  sprintf(apPassword, "%2X:%2X:%2X:%2X:%2X:%2X", mac_[0], mac_[1], mac_[2], mac_[3], mac_[4], mac_[5]);

  return apPassword;
}

void setup() {
  // put your setup code here, to run once:
  Serial.begin(115200);
  Serial.println();

  //clean FS, for testing  //SPIFFS.format();

  //read configuration from FS json
  Serial.println("mounting FS...");
  if (SPIFFS.begin()) {
    Serial.println("mounted file system");
    if (SPIFFS.exists("/config.json")) {
      //file exists, reading and loading
      Serial.println("reading config file");
      File configFile = SPIFFS.open("/config.json", "r");
      if (configFile) {
        Serial.println("opened config file");
        size_t size = configFile.size();
        // Allocate a buffer to store contents of the file.
        std::unique_ptr<char[]> buf(new char[size]);

        configFile.readBytes(buf.get(), size);
        DynamicJsonBuffer jsonBuffer;
        JsonObject& json = jsonBuffer.parseObject(buf.get());
        json.printTo(Serial);
        if (json.success()) {
          Serial.println("\nparsed json");

          custom_mqtt_server.setValue(json["mqtt_server"]);
          custom_mqtt_port.setValue(json["mqtt_port"]);
          custom_blynk_token.setValue(json["blynk_token"]);

        } else {
          Serial.println("failed to load json config");
        }
      }
    }
  } else {
    Serial.println("failed to mount FS");
  }
  //end read

  //WiFiManager. Local intialization. Once its business is done, there is no need to keep it around
  WiFiManager wifiManager;

  wifiManager.addParameter(&custom_mqtt_server);
  wifiManager.addParameter(&custom_mqtt_port);
  wifiManager.addParameter(&custom_blynk_token);

  //set config save notify callback
  wifiManager.setSaveConfigCallback(ssaveConfigCallback);

  //set static ip  //wifiManager.setSTAStaticIPConfig(IPAddress(10,0,1,99), IPAddress(10,0,1,1), IPAddress(255,255,255,0));
  //reset settings - for testing  //wifiManager.resetSettings();
  //set minimu quality of signal so it ignores AP's under that quality  //defaults to 8%  //wifiManager.setMinimumSignalQuality();
  //sets timeout until configuration portal gets turned off  //useful to make it all retry or go to sleep  //in seconds  //wifiManager.setTimeout(120);

  if (!wifiManager.autoConnect(getWiFiMACaddr(), "password")) {
    Serial.println("failed to connect and hit timeout");
    delay(3000);
    //reset and try again, or maybe put it to deep sleep
    ESP.reset();
    delay(5000);
  }

  //if you get here you have connected to the WiFi
  Serial.println("connected...yeey :)");
  Serial.println("local ip");
  Serial.println(WiFi.localIP());

  //Setup MQTT
  pinMode(BUILTIN_LED, OUTPUT);     // Initialize the BUILTIN_LED pin as an output
  client.setServer(custom_mqtt_server.getValue(), atoi(custom_mqtt_port.getValue()));
  client.setCallback(mqtt_callback);
}

void mqtt_callback(char* topic, byte* payload, unsigned int length) {
  Serial.print("Message arrived [");
  Serial.print(topic);
  Serial.print("] ");
  for (int i = 0; i < length; i++) {
    Serial.print((char)payload[i]);
  }
  Serial.println();

  // Switch on the LED if an 1 was received as first character
  if ((char)payload[0] == '1') {
    digitalWrite(BUILTIN_LED, LOW);   // Turn the LED on (Note that LOW is the voltage level
    // but actually the LED is on; this is because
    // it is acive low on the ESP-01)
  } else {
    digitalWrite(BUILTIN_LED, HIGH);  // Turn the LED off by making the voltage HIGH
  }
}

void mqtt_reconnect() {
  // Loop until we're reconnected
  while (!client.connected()) {
    Serial.print("Attempting MQTT connection...");
    // Attempt to connect
    if (client.connect("ESP8266Client")) {
      Serial.println("connected");
      // Once connected, publish an announcement...
      client.publish("outTopic", "hello world");
      // ... and resubscribe
      client.subscribe("inTopic");
    } else {
      Serial.print("failed, rc=");
      Serial.print(client.state());
      Serial.println(" try again in 5 seconds");
      // Wait 5 seconds before retrying
      delay(5000);
    }
  }
}

char msg[50];
int value = 0;
long lastMsg = 0;

void loop() {
  if (!client.connected()) {
    mqtt_reconnect();
  }
  client.loop();

  long now = millis();
  if (now - lastMsg > 2000) {
    lastMsg = now;
    ++value;
    snprintf (msg, 75, "hello world #%ld", value);
    Serial.print("Publish message: ");
    Serial.println(msg);
    client.publish("outTopic", msg);
  }
}
