/*
  Copyright 2017 Andreas Spiess

  Permission is hereby granted, free of charge, to any person obtaining a copy of this software and associated documentation files (the "Software"),
  to deal in the Software without restriction, including without limitation the rights to use, copy, modify, merge, publish, distribute, sublicense,
  and/or sell copies of the Software, and to permit persons to whom the Software is furnished to do so, subject to the following conditions:

  The above copyright notice and this permission notice shall be included in all copies or substantial portions of the Software.

  THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
  MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE
  FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION
  WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.

  This software is based on the work of Ray Burnette: https://www.hackster.io/rayburne/esp8266-mini-sniff-f6b93a
*/

#include <ESP8266WiFi.h>
#include <ESP8266HTTPClient.h>
#include <ArduinoJson.h>

#include <set>
#include <string>
#include "./functions.h"

#define disable 0
#define enable  1
#define SENDTIME 30000
#define MAXDEVICES 60
#define PURGETIME 600000
#define MINRSSI -82

unsigned int channel = 1;
int clients_known_count_old, aps_known_count_old;
unsigned long sendEntry, deleteEntry;

String device[MAXDEVICES];
int nbrDevices = 0;
int usedChannels[15];

const char* scannerID = "1001";

const char* mySSID = "projectHeatmap";
const char* myPASSWORD = "nuc12345";

// Variables will change:
int lastState = HIGH; // the previous state from the input pin
int currentState;     // the current reading from the input pin
int calibration = 4.0;

uint8_t BUTTON_PIN = 7;

String lastSendTime;

String serverName = "http://192.168.88.2:5000/";
WiFiClient client;
HTTPClient http;

void setup() {
	Serial.begin(115200);
	Serial.printf("Booting up as scanner %s", scannerID);

	wifi_set_opmode(STATION_MODE);            // Promiscuous works only with station mode
	wifi_set_channel(channel);
	wifi_promiscuous_enable(disable);
	wifi_set_promiscuous_rx_cb(promisc_cb);   // Set up promiscuous callback
	wifi_promiscuous_enable(enable);
}




void loop() {
  	channel = 1;
  	boolean sendMQTT = false;
  	wifi_set_channel(channel);
  	while (true) {
		nothing_new++;                          // Array is not finite, check bounds and adjust if required
		if (nothing_new > 200) {                // monitor channel for 200 ms
			nothing_new = 0;
			channel++;
			if (channel == 15) break;             // Only scan channels 1 to 14
			wifi_set_channel(channel);
		}
		delay(1);  // critical processing timeslice for NONOS SDK! No delay(0) yield()

		if (clients_known_count > clients_known_count_old) {
			clients_known_count_old = clients_known_count;
			sendMQTT = true;
		}
		if (aps_known_count > aps_known_count_old) {
			aps_known_count_old = aps_known_count;
			sendMQTT = true;
		}
		if (millis() - sendEntry > SENDTIME) {
			sendEntry = millis();
			sendMQTT = true;
		}
	}
	purgeDevice();
	if (sendMQTT) {
		showDevices();
		sendDevices();
	}
}

void connectToWiFi() {
	delay(10);
	// We start by connecting to a WiFi network
	Serial.println();
	Serial.print("Connecting to ");
	Serial.println(mySSID);

	WiFi.mode(WIFI_STA);
	WiFi.begin(mySSID, myPASSWORD);

	while (WiFi.status() != WL_CONNECTED) {
		delay(500);
		Serial.print(".");
	}

	Serial.println("");
	Serial.println("WiFi connected");
	Serial.println("IP address: ");
	Serial.println(WiFi.localIP());
}

void purgeDevice() {
 	for (int u = 0; u < clients_known_count; u++) {
    if ((millis() - clients_known[u].lastDiscoveredTime) > PURGETIME) {
		Serial.print("purge Client" );
		Serial.println(u);
		for (int i = u; i < clients_known_count; i++) memcpy(&clients_known[i], &clients_known[i + 1], sizeof(clients_known[i]));
		clients_known_count--;
		break;
		}
	}
  	for (int u = 0; u < aps_known_count; u++) {
		if ((millis() - aps_known[u].lastDiscoveredTime) > PURGETIME) {
		Serial.print("purge Beacon" );
		Serial.println(u);
		for (int i = u; i < aps_known_count; i++) memcpy(&aps_known[i], &aps_known[i + 1], sizeof(aps_known[i]));
		aps_known_count--;
		break;
		}
  	}
}


void showDevices() {
	Serial.println("");
	Serial.println("");
	Serial.println("-------------------Device DB-------------------");
	Serial.printf("%4d Devices + Clients.\n",aps_known_count + clients_known_count); // show count

	// show Beacons
	for (int u = 0; u < aps_known_count; u++) {
		Serial.printf( "%4d ",u); // Show beacon number
		Serial.print("B ");
		Serial.print(formatMac1(aps_known[u].bssid));
		Serial.print(" RSSI ");
		Serial.print(aps_known[u].rssi);
		Serial.print(" channel ");
		Serial.println(aps_known[u].channel);
	}

  	// show Clients
	for (int u = 0; u < clients_known_count; u++) {
		Serial.printf("%4d ",u); // Show client number
		Serial.print("C ");
		Serial.print(formatMac1(clients_known[u].station));
		Serial.print(" RSSI ");
		Serial.print(clients_known[u].rssi);
		Serial.print(" channel ");
		Serial.println(clients_known[u].channel);
	}
}

void sendDevices() {
	// Disable scanner to connect to WiFi
	wifi_promiscuous_enable(disable);
	connectToWiFi();

	String serverAdressJson = serverName + "/json-post";
	String serverAdressTime = serverName + "/current-time";

	String deviceMac;

	JsonDocument doc;
	JsonDocument subdoc;

	// add Beacons
	for (int u = 0; u < aps_known_count; u++) {
		deviceMac = formatMac1(aps_known[u].bssid);
		if (aps_known[u].rssi > MINRSSI) {
			subdoc["scannerID"] = scannerID;
			subdoc["mac"] = deviceMac;
			subdoc["rssi"] = aps_known[u].rssi;
			subdoc["calibration"] = calibration;
			doc.add(subdoc);
			subdoc.clear();
		}
	}

	// Add Clients
	for (int u = 0; u < clients_known_count; u++) {
		deviceMac = formatMac1(clients_known[u].station);
		if (clients_known[u].rssi > MINRSSI) {
			subdoc["scannerID"] = scannerID;
			subdoc["mac"] = deviceMac;
			subdoc["rssi"] = clients_known[u].rssi;
			subdoc["calibration"] = calibration;
			doc.add(subdoc);
			subdoc.clear();
		}
	}

	String requestBody;
	Serial.println();
	// Json for Serial print
	serializeJsonPretty(doc, Serial);
	// Json for HTTP POST
	serializeJson(doc, requestBody);
	Serial.println();

	// HTTP request
	http.begin(client, serverAdressJson);
	http.addHeader("Content-Type", "application/json");

	int httpResponseCodePost = http.POST(requestBody);

	// Free resources
	doc.clear();

	if(httpResponseCodePost<=0) {
		Serial.printf("Error posting JSON:");
		Serial.println(httpResponseCodePost);
	}

	http.end();
	http.begin(client, serverAdressTime);

	int httpResponseCodeGet = http.GET();

	if(httpResponseCodeGet>0) {
		lastSendTime = http.getString();
		Serial.println(lastSendTime);
	}
	else {
		Serial.printf("Error getting time:");
		Serial.println(httpResponseCodeGet);
	}

	// Free resources
	http.end();

	// read the state of the switch/button:
	// currentState = digitalRead(BUTTON_PIN);

	// if(currentState == HIGH)
	// 	Serial.println("Button pressed!");

	delay(100);
	wifi_promiscuous_enable(enable);
	sendEntry = millis();
}