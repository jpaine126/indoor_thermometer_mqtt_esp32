/***************************************************
  Indoor Thermometer w/ MQTT for Adafruit Feather ESP32 V2
 ****************************************************/

// include writing to MDParola screen
// #define PAROLA_SCREEN



#include <stdlib.h>

#include <WiFi.h>
#include <PubSubClient.h>

#include <Wire.h>
#include "Adafruit_SHT4x.h"

#include <Arduino_JSON.h>

#ifdef PAROLA_SCREEN
#include <MD_Parola.h>
#include <MD_MAX72xx.h>
#include <SPI.h>
#endif

#include "secrets.h"

/************ Global State ******************/

// Create a WiFiClient class to connect to the MQTT server.
WiFiClient espClient;

// Setup the MQTT client class by passing in the WiFi client and MQTT server and login details.
PubSubClient client(espClient);
unsigned long lastMsg = 0;
#define MSG_BUFFER_SIZE (50)
char msg[MSG_BUFFER_SIZE];
int value = 0;

Adafruit_SHT4x sht4 = Adafruit_SHT4x();

#ifdef PAROLA_SCREEN
// display params
#define HARDWARE_TYPE MD_MAX72XX::FC16_HW
#define MAX_DEVICES 4

#define CLK_PIN 5
#define DATA_PIN 19
#define CS_PIN 13

MD_Parola P = MD_Parola(HARDWARE_TYPE, CS_PIN, MAX_DEVICES);

bool display_state = true;  // true = temp, false = hum
#endif

// Device Params

String clientId = "ThermostatClient-794782";

/****************************** Feeds ***************************************/

const char config_topic[] = "homeassistant/device/temp01_ae_t/config";
const char therm_topic[] = "thermostat_p/state";
const char onoff_topic[] = "homeassistant/device/temp01_ae_t/onoff";
const char thermostat_availability[] = "thermostat_p/state";
const char hass_availability[] = "homeassistant/status";

/*************************** Sketch Code ************************************/


void callback(char* topic, byte* payload, unsigned int length) {
  Serial.print("Message arrived [");
  Serial.print(topic);
  Serial.print("] ");
  for (int i = 0; i < length; i++) {
    Serial.print((char)payload[i]);
  }
  Serial.println();
}


void setup() {
  #ifdef PAROLA_SCREEN
  P.begin();
  P.setIntensity(0);  // Set brightness to 0 (minimum)
  P.displayClear();
  #endif

  Serial.begin(115200);
  delay(10);

  setup_thermostat();
  setup_wifi();
  setup_mqtt_client();

  mqtt_reconnect();
  mqtt_register();
}


void loop() {
  // Ensure the connection to the MQTT server is alive
  mqtt_reconnect();

  // waiting for i2c sensor to work
  if (!sht4.begin()) {
    Serial.println("Couldn't find SHT4x");
    while (1) delay(1);
  }

  // take reading
  Serial.println("Reading sensor...");
  sensors_event_t humidity, temp;

  uint32_t timestamp = millis();
  sht4.getEvent(&humidity, &temp);  // populate temp and humidity objects with fresh data

  Serial.print("Temperature: ");
  Serial.print(temp.temperature);
  Serial.println(" degrees C");
  Serial.print("Humidity: ");
  Serial.print(humidity.relative_humidity);
  Serial.println("% rH");

  #ifdef PAROLA_SCREEN
  // display reading
  if (display_state) {
    Serial.println("Displaying temperature");
    char temp_buff[5];
    dtostrf((temp.temperature * 1.8) + 32, 4, 1, temp_buff);
    String temp_display;
    temp_display = String(temp_buff) + " F";
    P.print(temp_display.c_str());
  } else {
    Serial.println("Displaying humidity");
    char hum_buff[5];
    dtostrf(humidity.relative_humidity, 4, 1, hum_buff);
    String hum_display;
    hum_display = String(hum_buff) + "%";
    P.print(hum_display.c_str());
  }

  display_state = !display_state;
  #endif

  // send to MQTT broker
  JSONVar packet_data;

  packet_data["temperature"] = temp.temperature;
  packet_data["humidity"] = humidity.relative_humidity;

  Serial.print("Sending MQTT msg...");
  const char* packet = JSON.stringify(packet_data).c_str();
  if (!client.publish(therm_topic, packet, strlen(packet))) {
    Serial.println(F("Failed"));
  } else {
    Serial.println(F("OK!"));
  }

  client.loop();
  delay(3000);
}


/************ Helpers ******************/


void setup_thermostat() {
  // setting up sht4x
  Wire.setPins(22, 20);
  sht4.setPrecision(SHT4X_HIGH_PRECISION);
  sht4.setHeater(SHT4X_NO_HEATER);

  pinMode(2, INPUT);
  delay(1);
  bool polarity = digitalRead(2);
  pinMode(2, OUTPUT);
  digitalWrite(2, !polarity);
}


/************ WiFi Helpers ******************/


void setup_wifi() {
  // Connect to WiFi access point.
  Serial.println();
  Serial.println();
  Serial.print("Connecting to ");
  Serial.println(WLAN_SSID);

  WiFi.begin(WLAN_SSID, WLAN_PASS);
  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
  }
  Serial.println();

  Serial.println("WiFi connected");
  Serial.print("IP address: ");
  Serial.println(WiFi.localIP());
}


/************ MQTT Helpers ******************/


void setup_mqtt_client() {
  client.setServer(MQTT_SERVER, MQTT_SERVERPORT);
  client.setBufferSize(800);
  client.setCallback(callback);
}

// Function to connect and reconnect as necessary to the MQTT server.
// Should be called in the loop function and it will take care of connecting.
void mqtt_reconnect() {
  // Loop until we're reconnected
  while (!client.connected()) {
    Serial.print("Attempting MQTT connection...");
    // Attempt to connect
    if (
      client.connect(
        clientId.c_str(),
        MQTT_USERNAME,
        MQTT_KEY)) {
      Serial.println("connected");
      client.subscribe(hass_availability);
      client.publish(thermostat_availability, "online");
    } else {
      Serial.print("failed, rc=");
      Serial.print(client.state());
      Serial.println(" try again in 5 seconds");
      delay(5000);
    }
  }
}


void mqtt_register() {
  JSONVar registration_data;

  registration_data["dev"]["ids"] = "1";
  registration_data["dev"]["name"] = "Thermostat";
  registration_data["dev"]["mf"] = "paine";
  registration_data["dev"]["mdl"] = "prototype";
  registration_data["dev"]["sw"] = "0.0";
  registration_data["dev"]["sn"] = "1";
  registration_data["dev"]["hw"] = "0.0";

  registration_data["o"]["name"] = "idk";
  registration_data["o"]["sw"] = "0.0";

  registration_data["cmps"]["temp_sensor1"]["p"] = "sensor";
  registration_data["cmps"]["temp_sensor1"]["device_class"] = "temperature";
  registration_data["cmps"]["temp_sensor1"]["expire_after"] = 600;
  registration_data["cmps"]["temp_sensor1"]["unit_of_measurement"] = "°C";
  registration_data["cmps"]["temp_sensor1"]["value_template"] = "{{ value_json.temperature }}";
  registration_data["cmps"]["temp_sensor1"]["unique_id"] = "temp01_ae_t";
  registration_data["cmps"]["temp_sensor1"]["suggested_display_precision"] = "1";

  registration_data["cmps"]["hum_sensor1"]["p"] = "sensor";
  registration_data["cmps"]["hum_sensor1"]["device_class"] = "humidity";
  registration_data["cmps"]["hum_sensor1"]["expire_after"] = 600;
  registration_data["cmps"]["hum_sensor1"]["unit_of_measurement"] = "%";
  registration_data["cmps"]["hum_sensor1"]["value_template"] = "{{ value_json.humidity }}";
  registration_data["cmps"]["hum_sensor1"]["unique_id"] = "temp01_ae_h";
  registration_data["cmps"]["hum_sensor1"]["suggested_display_precision"] = "1";

  registration_data["stat_t"] = therm_topic;
  registration_data["availability_topic"] = thermostat_availability;
  registration_data["qos"] = 1;

  Serial.println("Registering to MQTT... ");

  const char* packet = JSON.stringify(registration_data).c_str();
  Serial.println(strlen(packet));

  int8_t ret;
  uint8_t retries = 1;

  while (retries >= 0) {
    ret = client.publish(config_topic, JSON.stringify(registration_data).c_str());

    if (ret) {
      Serial.println("MQTT registered!");
      break;
    }

    Serial.println("MQTT registration failed, retrying...");
    delay(1000);
    retries--;

    if (retries == 0) {
      Serial.println("MQTT registration failed, moving on...");
    }
  }
}
