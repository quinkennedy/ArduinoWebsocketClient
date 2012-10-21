#include "Arduino.h"
#include <Ethernet.h>
#include <SPI.h>
#include <SoftwareSerial.h>
#include <WebSocketClient.h>

byte mac[] = { 0xDE, 0xAD, 0xBE, 0xEF, 0xFE, 0xED };
char server[] = "echo.websocket.org";
WebSocketClient client;
SoftwareSerial debug = SoftwareSerial(7,6);

void setup() {
debug.begin(9600);
Ethernet.begin(mac);
client.setDebug(&debug);
client.connect(server);
client.onMessage(onMessage);
client.onError(onError);
debug.println("Connected!");
client.send("Hello World!");
}

void loop() {
client.monitor();
}

void onMessage(WebSocketClient client, char* message) {
debug.print("Received: ");
debug.println(message);
}

void onError(WebSocketClient client, char* message) {
debug.print("ERROR: ");
debug.println(message);
}