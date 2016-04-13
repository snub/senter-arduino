#include <aJSON.h>
#include <Time.h> 
#include <Ethernet.h>
#include <EthernetUdp.h>
#include <SPI.h>
#include <PubSubClient.h>
#include <OneWire.h>
#include <DallasTemperature.h>

#define ONE_WIRE_BUS 2
//#define TEMPERATURE_PRECISION 9
#define UPDATE_INTERVAL 60

// Callback function header
void callback(char* topic, byte* payload, unsigned int length);
String byteArrayToHexStr(byte* a, int len);
void pubMsg(String topic, String msg);
void subTopic(String topic);
String deviceAddressToStr(DeviceAddress deviceAddress);

// Setup a oneWire instance to communicate with any OneWire devices (not just Maxim/Dallas temperature ICs)
OneWire oneWire(ONE_WIRE_BUS);
// Pass our oneWire reference to Dallas Temperature. 
DallasTemperature sensors(&oneWire);

// Number of temperature devices found
int numberOfDevices;
// We'll use this variable to store a found device address
DeviceAddress tempDeviceAddress;

// MAC
byte mac[] = { 0x90, 0xA2, 0xDA, 0x0E, 0x9A, 0x9E };
String hexMac = byteArrayToHexStr(mac, 6);

// MQTT server
byte mqttServer[] = { 192, 168, 1, 100 };

// NTP server
IPAddress timeServer(195, 80, 105, 226);

// Time Zone UTC
const byte timeZone = 0;

EthernetUDP Udp;
unsigned int localPort = 8888;  // local port to listen for UDP packets

// NTP time is in the first 48 bytes of message
const int NTP_PACKET_SIZE = 48;
//buffer to hold incoming & outgoing packets
byte packetBuffer[NTP_PACKET_SIZE];

EthernetClient ethClient;
PubSubClient mqttClient(mqttServer, 1883, callback, ethClient);

void setup() {
  Serial.begin(9600);
  while (!Serial) ; // Needed for Leonardo only
  delay(250);
  
  Serial.println("sensor controller: " + hexMac);
  
  // ethernet
  if (Ethernet.begin(mac) == 0) {
    // no point in carrying on, so do nothing forevermore:
    while (1) {
      Serial.println("failed to configure Ethernet using DHCP");
      delay(10000);
    }
  }
  
  Serial.print(" ip: ");
  Serial.println(Ethernet.localIP());

  // udp and ntp
  Udp.begin(localPort);
  Serial.println("ntp sync");
  setSyncProvider(getNtpTime);

  // mqtt
  String clientId = "sc" + hexMac;
  int clientIdLen = clientId.length() + 1;
  char clientIdArray[clientIdLen];
  clientId.toCharArray(clientIdArray, clientIdLen);
  
  if (!mqttClient.connect(clientIdArray)) {
    // no point in carrying on, so do nothing forevermore:
    while (1) {
      Serial.println("failed to connect MQTT server");
      delay(10000);
    }
  }

  String helloTopic = "/controller/" + hexMac + "/startup";
  
  char buffer[10];
  memset(buffer,'\0',10);
  ltoa(now(), buffer, 10);
 
  pubMsg(helloTopic, buffer);
    
  String incomingTopic = "/controller/" + hexMac + "/incoming";
  subTopic(incomingTopic);
  
  // sensors
  sensors.begin();
  numberOfDevices = sensors.getDeviceCount();
  
  String countTopic = "/controller/" + hexMac + "/sensors/count";
  
  memset(buffer,'\0',10);
  itoa(numberOfDevices, buffer, 10);
  pubMsg(countTopic, buffer);
  
  for(int i = 0; i < numberOfDevices; i++) {
    if(sensors.getAddress(tempDeviceAddress, i)) {
      
      Serial.print("found sensor ");
      Serial.print(i, DEC);
      Serial.print(" with address: ");
      String dA = deviceAddressToStr(tempDeviceAddress);
      Serial.println(dA);
		
      //Serial.print(" setting resolution to ");
      //Serial.println(TEMPERATURE_PRECISION, DEC);
		
      //sensors.setResolution(tempDeviceAddress, TEMPERATURE_PRECISION);
      
      String sensorTopic = "/controller/" + hexMac + "/sensors/discovery";
      pubMsg(sensorTopic, dA);
    }
  }
}

time_t lastUpdate = 0;

void loop() {
  timeStatus_t tStatus = timeStatus();
  if (timeStatus() != timeSet ) {
    Serial.println("time not set");
    return;
  }
  time_t time = now();
  if (time - lastUpdate > UPDATE_INTERVAL) {
    sensors.requestTemperatures();
    Serial.print("timestamp: ");
    Serial.println(time);
    for (int i = 0; i < numberOfDevices; i++) {
      if(sensors.getAddress(tempDeviceAddress, i)) {
        float temp = sensors.getTempC(tempDeviceAddress);
        String deviceAddress = deviceAddressToStr(tempDeviceAddress);
        
        Serial.print(" sensor: ");
        Serial.print(deviceAddress);
        Serial.print(" tmp: ");
        Serial.print(temp);
        Serial.println();

        String tempTopic = "/controller/" + hexMac + "/sensor/" + deviceAddress + "/temp";
        
        char tempChar[10];
        memset(tempChar,'\0',10);
        dtostrf(temp, 4, 2, tempChar);

        char timeChar[10];
        memset(timeChar,'\0',10);
        ltoa(now(), timeChar, 10);

        String tempMsg = String(timeChar) + "," + tempChar;

        pubMsg(tempTopic, tempMsg);
      }
    }
    lastUpdate = time;
  }
  mqttClient.loop();
}

void pubMsg(String topic, String msg) {
    int topicLen = topic.length() + 1;
    char topicArray[topicLen];
    topic.toCharArray(topicArray, topicLen);

    int msgLen = msg.length() + 1;
    char msgArray[msgLen];
    msg.toCharArray(msgArray, msgLen);
    
    if (!mqttClient.publish(topicArray, msgArray)) {
      Serial.println("publishing failed");
    }
}

void subTopic(String topic) {
    int topicLen = topic.length() + 1;
    char topicArray[topicLen];
    topic.toCharArray(topicArray, topicLen);
    
    if (!mqttClient.subscribe(topicArray)) {
      Serial.println("subscription failed");
    }
}

String byteArrayToHexStr(byte* a, int len) {
  String str = "";
  for (int i = 0; i < len; i++) {
    if (a[i] < 16) str.concat(String("0"));
    str.concat(String(a[i], HEX));
  }
  return str;
}

String deviceAddressToStr(DeviceAddress deviceAddress) {
  String str = ""; 
  for (uint8_t i = 0; i < 8; i++)
  {
    if (deviceAddress[i] < 16) str.concat(String("0"));
    str.concat(String(deviceAddress[i], HEX));
  }
  return str;
} 

// Callback function
void callback(char* topic, byte* payload, unsigned int length) {
  int i;
  char* message = new char[length];
  for(i = 0; i < length; i++) {
     message[i] = payload[i]; 
  }
  message[i] = '\0';
  
  Serial.print("topic: ");
  Serial.println(topic);
  Serial.print("payload: ");
  Serial.println(message);
}

time_t getNtpTime() {
  while (Udp.parsePacket() > 0) ; // discard any previously received packets
  Serial.println(" transmit ntp request");
  sendNTPpacket(timeServer);
  uint32_t beginWait = millis();
  while (millis() - beginWait < 1500) {
    int size = Udp.parsePacket();
    if (size >= NTP_PACKET_SIZE) {
      Serial.println(" receive ntp response");
      Udp.read(packetBuffer, NTP_PACKET_SIZE);  // read packet into the buffer
      unsigned long secsSince1900;
      // convert four bytes starting at location 40 to a long integer
      secsSince1900 =  (unsigned long)packetBuffer[40] << 24;
      secsSince1900 |= (unsigned long)packetBuffer[41] << 16;
      secsSince1900 |= (unsigned long)packetBuffer[42] << 8;
      secsSince1900 |= (unsigned long)packetBuffer[43];
      return secsSince1900 - 2208988800UL + timeZone * SECS_PER_HOUR;
    }
  }
  Serial.println("No NTP Response :-(");
  return 0; // return 0 if unable to get the time
}

// send an NTP request to the time server at the given address
void sendNTPpacket(IPAddress &address)
{
  // set all bytes in the buffer to 0
  memset(packetBuffer, 0, NTP_PACKET_SIZE);
  // Initialize values needed to form NTP request
  // (see URL above for details on the packets)
  packetBuffer[0] = 0b11100011;   // LI, Version, Mode
  packetBuffer[1] = 0;     // Stratum, or type of clock
  packetBuffer[2] = 6;     // Polling Interval
  packetBuffer[3] = 0xEC;  // Peer Clock Precision
  // 8 bytes of zero for Root Delay & Root Dispersion
  packetBuffer[12]  = 49;
  packetBuffer[13]  = 0x4E;
  packetBuffer[14]  = 49;
  packetBuffer[15]  = 52;
  // all NTP fields have been given values, now
  // you can send a packet requesting a timestamp:                 
  Udp.beginPacket(address, 123); //NTP requests are to port 123
  Udp.write(packetBuffer, NTP_PACKET_SIZE);
  Udp.endPacket();

