/***************************************************
  Indoor Thermometer w/ MQTT for Adafruit Feather ESP32 V2
 ****************************************************/
#include <WiFi.h>
#include "Adafruit_MQTT.h"
#include "Adafruit_MQTT_Client.h"

#include <Wire.h>
#include "Adafruit_SHT4x.h"

#include <Arduino_JSON.h>

/************************* WiFi Access Point *********************************/

#define WLAN_SSID       ""
#define WLAN_PASS       ""

/************************* Adafruit.io Setup *********************************/

#define AIO_SERVER      ""
#define AIO_SERVERPORT  1883                   // use 8883 for SSL
#define AIO_USERNAME    ""
#define AIO_KEY         ""

/************ Global State ******************/

// Create a WiFiClient class to connect to the MQTT server.
WiFiClient client;
// or... use WiFiClientSecure for SSL
//WiFiClientSecure client;

// Setup the MQTT client class by passing in the WiFi client and MQTT server and login details.
Adafruit_MQTT_Client mqtt(&client, AIO_SERVER, AIO_SERVERPORT, AIO_USERNAME, AIO_KEY);

Adafruit_SHT4x sht4 = Adafruit_SHT4x();

/****************************** Feeds ***************************************/

// Setup a feed for registering device.
Adafruit_MQTT_Publish registration = Adafruit_MQTT_Publish(&mqtt, "homeassistant/device/temp01_ae_t/config");

// Setup a feed for publishing.
Adafruit_MQTT_Publish thermometer = Adafruit_MQTT_Publish(&mqtt, "thermostat_p/state");

// Setup a feed called 'onoff' for subscribing to changes.
Adafruit_MQTT_Subscribe onoffbutton = Adafruit_MQTT_Subscribe(&mqtt, "homeassistant/device/temp01_ae_t/onoff");

/*************************** Sketch Code ************************************/

// Bug workaround for Arduino 1.6.6, it seems to need a function declaration
// for some reason (only affects ESP8266, likely an arduino-builder bug).
void MQTT_connect();

void setup() {
  Serial.begin(115200);
  delay(10);

  // setting up sht4x
  Wire.setPins(22, 20);
  sht4.setPrecision(SHT4X_HIGH_PRECISION);
  sht4.setHeater(SHT4X_NO_HEATER);

  pinMode(2, INPUT);
  delay(1);
  bool polarity = digitalRead(2);
  pinMode(2, OUTPUT);
  digitalWrite(2, !polarity);

  Serial.println(F("Adafruit MQTT demo"));

  // Connect to WiFi access point.
  Serial.println(); Serial.println();
  Serial.print("Connecting to ");
  Serial.println(WLAN_SSID);

  WiFi.begin(WLAN_SSID, WLAN_PASS);
  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
  }
  Serial.println();

  Serial.println("WiFi connected");
  Serial.println("IP address: "); Serial.println(WiFi.localIP());

  // Setup MQTT subscription for onoff feed.
  mqtt.subscribe(&onoffbutton);

  MQTT_connect();
  MQTT_register();
}

void loop() {
  // Ensure the connection to the MQTT server is alive (this will make the first
  // connection and automatically reconnect when disconnected).  See the MQTT_connect
  // function definition further below.
  MQTT_connect();

  // this is our 'wait for incoming subscription packets' busy subloop
  // try to spend your time here

  Adafruit_MQTT_Subscribe *subscription;
  while ((subscription = mqtt.readSubscription(5000))) {
    if (subscription == &onoffbutton) {
      Serial.print(F("Got: "));
      Serial.println((char *)onoffbutton.lastread);
    }
  }

  // waiting for i2c sensor to work
  Serial.println("Adafruit SHT4x test");
  if (! sht4.begin()) {
    Serial.println("Couldn't find SHT4x");
    while (1) delay(1);
  }

  sensors_event_t humidity, temp;
  
  uint32_t timestamp = millis();
  sht4.getEvent(&humidity, &temp);// populate temp and humidity objects with fresh data

  Serial.print("Temperature: "); Serial.print(temp.temperature); Serial.println(" degrees C");
  Serial.print("Humidity: "); Serial.print(humidity.relative_humidity); Serial.println("% rH");

  JSONVar packet_data;

  packet_data["temperature"] = temp.temperature;
  packet_data["humidity"] = humidity.relative_humidity;

  // Now we can publish stuff!
  Serial.print(F("\nSending thermometer val "));
  Serial.print(temp.temperature);
  Serial.print("...");
  const char* packet = JSON.stringify(packet_data).c_str();
  if (! thermometer.publish(packet, strlen(packet))) {
    Serial.println(F("Failed"));
  } else {
    Serial.println(F("OK!"));
  }

  // ping the server to keep the mqtt connection alive
  // NOT required if you are publishing once every KEEPALIVE seconds
  /*
  if(! mqtt.ping()) {
    mqtt.disconnect();
  }
  */
  delay(1000);
}


void MQTT_register() {
  JSONVar registration_data;

  registration_data["dev"]["ids"] = "1";
  registration_data["dev"]["name"] = "thermostat_p";
  registration_data["dev"]["mf"] = "paine";
  registration_data["dev"]["mdl"] = "prototype";
  registration_data["dev"]["sw"] = "0.0";
  registration_data["dev"]["sn"] = "1";
  registration_data["dev"]["hw"] = "0.0";

  registration_data["o"]["name"] = "idk";
  registration_data["o"]["sw"] = "0.0";

  registration_data["cmps"]["temp_sensor1"]["p"] = "sensor";
  registration_data["cmps"]["temp_sensor1"]["device_class"] = "temperature";
  registration_data["cmps"]["temp_sensor1"]["unit_of_measurement"] = "°C";
  registration_data["cmps"]["temp_sensor1"]["value_template"] = "{{ value_json.temperature }}";
  registration_data["cmps"]["temp_sensor1"]["unique_id"] = "temp01_ae_t";

  // message too long for mqtt library
  // registration_data["cmps"]["hum_sensor1"]["p"] = "sensor";
  // registration_data["cmps"]["hum_sensor1"]["device_class"] = "humidity";
  // registration_data["cmps"]["hum_sensor1"]["unit_of_measurement"] = "%";
  // registration_data["cmps"]["hum_sensor1"]["value_template"] = "{{ value_json.humidity}}";
  // registration_data["cmps"]["hum_sensor1"]["unique_id"] = "temp01_ae_h";

  registration_data["state_topic"] = "thermostat_p/state";
  registration_data["qos"] = 1;

  Serial.println("Registering to MQTT... ");

  const char* packet = JSON.stringify(registration_data).c_str();
  Serial.println(packet);
  Serial.println(strlen(packet) * 4);

  int8_t ret;
  uint8_t retries = 1;
  while (
    (
      ret = registration.publish(packet, 550 * 4)//strlen(packet)*4)
    ) != 0
  ) {
    Serial.println("MQTT registration failed, retrying...");
    delay(1000);  // wait 5 seconds
    retries--;
    // if (retries == 0) {
    //   // basically die and wait for WDT to reset me
    //   while (1);
    // }
    break;
  }
  Serial.println("MQTT Registered!");
}


// Function to connect and reconnect as necessary to the MQTT server.
// Should be called in the loop function and it will take care if connecting.
void MQTT_connect() {
  int8_t ret;

  // Stop if already connected.
  if (mqtt.connected()) {
    return;
  }

  Serial.print("Connecting to MQTT... ");

  uint8_t retries = 3;
  while ((ret = mqtt.connect()) != 0) { // connect will return 0 for connected
       Serial.println(mqtt.connectErrorString(ret));
       Serial.println("Retrying MQTT connection in 5 seconds...");
       mqtt.disconnect();
       delay(5000);  // wait 5 seconds
       retries--;
       if (retries == 0) {
         // basically die and wait for WDT to reset me
         while (1);
       }
  }
  Serial.println("MQTT Connected!");
}