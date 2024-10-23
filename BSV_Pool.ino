#include <ArduinoOTA.h>
#include <ESP8266WiFi.h>
#include <ESP8266mDNS.h>
#include <TelnetStream.h>
#include <SoftwareSerial.h>
#include <SerialWebLog.h>
#include "WifiPass.h"  //define wifi SSID & pass

#define HOST_NAME "BSV_Pool"

// PROTOCOL P906-transmision-protocol_R10.pdf
// Uses RS485 MOdbus to TTL Serial

#define rxPin D1
#define txPin D2
//#define SERIAL_OBJ Serial
#define SERIAL_OBJ mySerial

SerialWebLog mylog;
String buffer;
EspSoftwareSerial::UART SERIAL_OBJ;

void sentTest(){
	String msg = "_HELLO_";
	int numW = SERIAL_OBJ.write(msg.c_str(), msg.length());
	SERIAL_OBJ.flush();
	mylog.printf("Sent %s\n", msg.c_str());
}

void setup() {

	mylog.setup(HOST_NAME, ssid, password);

	mylog.getServer()->on("/sendTest", sentTest);
	mylog.addHtmlExtraMenuOption("SendTest", "/sendTest");

	mylog.print("----------------------------------------------\n");
	String ID = String(ESP.getChipId(), HEX);
	mylog.printf("%s\n",HOST_NAME);
	mylog.printf("Booting %s ......\n", ID.c_str());

	mylog.print("Serial Connection...\n");
	pinMode(rxPin, INPUT);
	pinMode(txPin, OUTPUT);
	SERIAL_OBJ.begin(19200, SWSERIAL_8N1, rxPin, txPin, false);
	delay(500);

	mylog.print("Watchdog setup...\n");
	ESP.wdtDisable();
	ESP.wdtEnable(WDTO_8S);

	mylog.print("Starting Arduino OTA...\n");
	ArduinoOTA.setHostname(HOST_NAME);
	ArduinoOTA.setRebootOnSuccess(true);
	ArduinoOTA.begin();
	
	mylog.print("Starting Telnet Server...\n");
	TelnetStream.begin();
	mylog.print("ready!\n");
}

void sendMessage(byte b, const String & txt){
	byte msg[] = { 0x3F, 0x79, 0x04 };
	msg[1] = b;
	TelnetStream.print("Sending " + txt + " - 0x" + String((int)b, HEX) + " '" + String((char)b) + "'\n");
	SERIAL_OBJ.write(msg, 3);
	SERIAL_OBJ.flush();
}

void loop() {

	mylog.update();
	ArduinoOTA.handle();
	ESP.wdtFeed(); //feed watchdog frequently

	switch (TelnetStream.read()) {

		case '1':
			TelnetStream.print("Print Buffer:\n");
			TelnetStream.print(buffer);
			buffer = "";
			TelnetStream.print("ok\n");
			break;

		case '2': sendMessage(0x79, "Soft. Version"); break;
		case '3': sendMessage(0x4C, "Language"); break;
		case '4': sendMessage(0x5A, "Chlorinator Size"); break;
		case '5': sendMessage(0x4F, "ORP 0-800 Target"); break;
		case '6': sendMessage(0x6F, "Current ORP"); break; //got '0xab 0x02' which is 683
		case '7': sendMessage(0x50, "PH Measurement"); break; // got '0xee 0x02 ' which is 750 (signed int)
		case '8': sendMessage(0x4E, "Salt Concentration"); break;
		case '9': sendMessage(0x48, "Time"); break;

	}

	rxSerial();
	delay(7);
}

// COMMANDS ///////////////////////////////////////////////////////
// * SEND (always 3 bytes)
// B1: ? 		>> 0x3f 	(ascii 63)
// B2: CMD 		:
//		y			>> 0x79	(ascii 121) >> software version
// B3: EOT 		>> 0x04 	(ascii 4)

// * RECEIVE (always 3 bytes)
// B1: B2 from TX
// B2: 
// B3: 
///////////////////////////////////////////////////////////////////

void rxSerial() {
	if (SERIAL_OBJ.available()) {
		String localBuf = "";
		static byte data[256] = {};
		int c = 0;
		while (SERIAL_OBJ.available()) {
			data[c] = SERIAL_OBJ.read();
			c++;
		}
		for( int i = 0; i < c; i++){
			char aux[4];
			sprintf(aux, "%02x", (char)data[i]);
			localBuf += "0x" + String(aux) + " ";
		}
		buffer += " << '" + localBuf + "'\n'";
		mylog.printf("Got Serial Data (%d) '%s'\n", c, localBuf.c_str());
		TelnetStream.printf("Got Serial Data (%d) '%s'\n", c, localBuf.c_str());
	}
}
