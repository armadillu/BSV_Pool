#include <ArduinoOTA.h>
#include <ESP8266WiFi.h>
#include <ESP8266mDNS.h>
#include <TelnetStream.h>
#include <SoftwareSerial.h>
#include <SerialWebLog.h>
#include "WifiPass.h"  //define wifi SSID & pass

#define HOST_NAME "BSV_Pool"
#define rxPin D1
#define txPin D2

// communicate with a BSV EVO BASIC pool chlorinator
// PROTOCOL P906-transmision-protocol_R10.pdf
// Uses RS232 TTL Serial, need an MAX3232 chip to connect to from an ESP8266

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

struct PoolData{
	int currentChlorine = 0;
	int targetChlorine = 0;
	int currentPH = 0;
	int targetPH = 0;
};

String ID;
SerialWebLog mylog;
EspSoftwareSerial::UART mySerial;
PoolData poolData;
unsigned int frameCounter = INT_MAX;

/////////////////////////////////////////////////////////////////////

void handleMetrics() {

	String idString = "{id=\"" + ID + "\",mac=\"" + WiFi.macAddress().c_str() + "\",host=\"" + HOST_NAME + "\"}";
	String message = "";

	message += "# HELP chlorineFree Free Chlorine\n";
	message += "# TYPE chlorineFree gauge\n";
	message += "chlorineFree" + idString + String(poolData.currentChlorine) + "\n";

	message += "# HELP chlorineTarget Chlorine Target\n";
	message += "# TYPE chlorineTarget gauge\n";
	message += "chlorineTarget" + idString + String(poolData.targetChlorine) + "\n";

	message += "# HELP phCurrent Current PH\n";
	message += "# TYPE phCurrent gauge\n";
	message += "phCurrent" + idString + String(poolData.currentPH) + "\n";

	message += "# HELP phTarget Target PH\n";
	message += "# TYPE phTarget gauge\n";
	message += "phTarget" + idString + String(poolData.targetPH) + "\n";

	mylog.getServer()->send(200, "text/plain", message);
}

void setup() {

	mylog.setup(HOST_NAME, ssid, password);

	//setup endpoints
	mylog.getServer()->on("/metrics", handleMetrics);
	mylog.addHtmlExtraMenuOption("Metrics", "/metrics");

	mylog.getServer()->on("/updateSensorData", updateSensorData);
	mylog.addHtmlExtraMenuOption("Update Sensor Data", "/updateSensorData");

	mylog.print("----------------------------------------------\n");
	ID = String(ESP.getChipId(), HEX);
	mylog.printf("%s\n",HOST_NAME);
	mylog.printf("Booting %s ......\n", ID.c_str());

	mylog.print("Serial Connection...\n");
	pinMode(rxPin, INPUT);
	pinMode(txPin, OUTPUT);
	mySerial.begin(19200, SWSERIAL_8N1, rxPin, txPin, false);
	delay(100);

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
	String logLine = "Sending " + txt + " - 0x" + String((int)b, HEX) + " '" + String((char)b) + "'\n" ;
	mylog.print(logLine);
	TelnetStream.print(logLine);
	
	//send data
	mySerial.write(msg, 3);
	mySerial.flush();
	
	//wait for an answer to be ready...
	int timeout = 0;
	bool gotResponse = true;
	while (mySerial.available() == 0) {
		delay(5);
		timeout++;
		if(timeout > 10){
			gotResponse = false;
			break;
		}
	}
	
	if(gotResponse){

		//read answer (should be 3 bytes)
		static byte data[64] = {0x00};
		int c = 0;
		while (mySerial.available()){ 
			data[c] = mySerial.read();
			c++;
		}

		int value = 0;
		if(data[0] == b){ //response must start with the requested field
			switch(b){
				case 0x4F: //Chlorine Target				
					value = poolData.targetChlorine = data[1] | data[2] << 8; break;
				case 0x6F: //Chlorine Current
					value = poolData.currentChlorine = data[1] | data[2] << 8; break;
				case 0x70: //PH Current
					value = poolData.currentPH = data[1] | data[2] << 8; break;
				case 0x50: //PH Target
					value = poolData.targetPH = data[1] | data[2] << 8; break;
			}
			mylog.printf("Got a response for msg 0x%02x - %d) \n", b, value);
		}else{
			mylog.printf("Error, unexpected respose from BSV! (for msg 0x%02x - %s) \n", b, txt.c_str());
		}
	}else{
		mylog.printf("No reponse! (for msg 0x%02x - %s) \n", b, txt.c_str());
	}
}

void updateSensorData(){
	sendMessage(0x4F, "ORP 0-800 Target");
	sendMessage(0x6F, "Current ORP");
	sendMessage(0x50, "PH Target");
	sendMessage(0x70, "PH Measurement");
}

void loop() {

	mylog.update();
	ArduinoOTA.handle();
	ESP.wdtFeed(); //feed watchdog frequently

	//handle interactive telnet session
	switch (TelnetStream.read()) {
		case '1': sendMessage(0x48, "Time"); break;
		case '2': sendMessage(0x79, "Soft. Version"); break;
		case '3': sendMessage(0x4C, "Language"); break;
		case '4': sendMessage(0x5A, "Chlorinator Size"); break;
		case '5': sendMessage(0x4F, "ORP 0-800 Target"); break;
		case '6': sendMessage(0x6F, "Current ORP"); break; //got '0xab 0x02' which is 683
		case '7': sendMessage(0x50, "PH Target"); break; // got '0xee 0x02 ' which is 750 (signed int)
		case '8': sendMessage(0x70, "PH Measurement"); break; // got '0xee 0x02 ' which is 750 (signed int)
		case '9': sendMessage(0x4E, "Salt Concentration"); break;		
	}
	delay(7);

	//update sensor data
	frameCounter++;
	if(frameCounter > 15000 || frameCounter < 0){
		frameCounter = 0;
		updateSensorData();
	}
}
