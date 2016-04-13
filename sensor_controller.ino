#include <DallasTemperature.h>
#include <Dns.h>
#include <Ethernet.h>
#include <EthernetUdp.h>
#include <OneWire.h>
#include <PubSubClient.h>
#include <SPI.h>
#include <Time.h>

#define ONE_WIRE_BUS 2

#define DEFAULT_UPDATE_INTERVAL 60

#define CMD_GET_IP 1
#define CMD_SET_IP 2
#define CMD_GET_UPDATE_INTERVAL 3
#define CMD_SET_UPDATE_INTERVAL 4
#define CMD_GET_NTP_SERVER 5
#define CMD_SET_NTP_SERVER 6


// Time Zone UTC
const byte TIME_ZONE = 0;

// NTP time is in the first 48 bytes of message
const byte NTP_PACKET_SIZE = 48;

// Local port to listen for UDP packets
const unsigned int UDP_LOCAL_PORT = 8888;


// Setup a oneWire instance to communicate with any OneWire devices (not just Maxim/Dallas temperature ICs)
OneWire oneWire(ONE_WIRE_BUS);

// Pass our oneWire reference to Dallas Temperature. 
DallasTemperature sensors(&oneWire);

// Number of temperature devices found
int numberOfDevices;

// We'll use this variable to store a found device address
DeviceAddress tempDeviceAddress;

// MAC address
byte mac[] = { 0x90, 0xA2, 0xDA, 0x0E, 0x9A, 0x9E };

// MAC address hex string
String hexMac = byteArrayToHexStr(mac, 6);

// MQTT server
byte mqttServer[] = { 192, 168, 1, 100 };

// NTP server
IPAddress timeServer(195, 80, 123, 154);

// UDP for NTP
EthernetUDP udp;

// buffer to hold incoming & outgoing packets
byte packetBuffer[NTP_PACKET_SIZE];

// For internet access
EthernetClient ethClient;

// MQTT client
PubSubClient mqttClient(mqttServer, 1883, callback, ethClient);

// Publishing interval of sensor readings
int updateInterval = DEFAULT_UPDATE_INTERVAL;

// Time of the last MQTT message
time_t lastUpdate = 0;


void setup() {
    Serial.begin(9600);
    while (!Serial) ; // Needed for Leonardo only
    delay(250);

    if (Ethernet.begin(mac) == 0) {
        // no point in carrying on, so do nothing forevermore:
        while (1) {
            Serial.println("failed to configure Ethernet using DHCP");
            delay(10000);
        }
    }

    // udp and ntp
    udp.begin(UDP_LOCAL_PORT);
    setSyncProvider(getNtpTime);

    // start MQTT client
    startMqtt();

    String helloTopic = "/controller/" + hexMac + "/hello";

    char buffer[11];
    memset(buffer,'\0',11);
    ultoa((unsigned long)now(), buffer, 10);

    pubMsg(helloTopic, buffer);

    // sensors
    sensors.begin();
    numberOfDevices = sensors.getDeviceCount();

    String countTopic = "/controller/" + hexMac + "/sensors/count";

    memset(buffer,'\0',10);
    itoa(numberOfDevices, buffer, 10);
    pubMsg(countTopic, buffer);

    for(int i = 0; i < numberOfDevices; i++) {

        if(sensors.getAddress(tempDeviceAddress, i)) {
            String dA = deviceAddressToStr(tempDeviceAddress);

            String sensorTopic = "/controller/" + hexMac + "/sensors/discovery";
            pubMsg(sensorTopic, dA);
        }
    }
}

void startEthernet(IPAddress ip) {
    Ethernet.begin(mac, ip);
    delay(5000);
}

void startMqtt() {
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
    
    String incomingTopic = "/controller/" + hexMac + "/cmd";
    subTopic(incomingTopic);
}

void loop() {
    // TODO: what to do if time is wrong
    //timeStatus_t tStatus = timeStatus();
    //if (timeStatus() != timeSet ) {
    //    Serial.println("time not set");
    //    return;
    //}

    time_t time = now();
    if (time - lastUpdate >= updateInterval) {
        sensors.requestTemperatures();

        for (int i = 0; i < numberOfDevices; i++) {
            if(sensors.getAddress(tempDeviceAddress, i)) {
                String deviceAddress = deviceAddressToStr(tempDeviceAddress);
                float temp = sensors.getTempC(tempDeviceAddress);

                String tempTopic = "/controller/" + hexMac + "/sensor/" + deviceAddress + "/tmp";

                char tempChar[10];
                memset(tempChar,'\0',10);
                dtostrf(temp, 5, 2, tempChar);

                char timeChar[11];
                memset(timeChar,'\0',11);
                ultoa((unsigned long)time, timeChar, 10);

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

void callback(char* topic, byte* payload, unsigned int length) {
    int i;
    char message[length + 1];
    for(i = 0; i < length; i++) {
        message[i] = payload[i]; 
    }
    message[i] = '\0';
    
    int cmd = 0;
    char* arg = NULL;
    char* ptr = strchr(message, ',');
    if (ptr != NULL) {
      int pos = ptr - message;
      char cmdString[(pos + 1)];
      strncpy(cmdString, message, pos);
      cmdString[pos] = '\0';
      cmd = atoi(cmdString);
      
      int argLen = strlen((ptr + 1));
      arg = new char[argLen];
      strncpy(arg, (ptr + 1), argLen);
      arg[argLen] = '\0';
    } else {
      cmd = atoi(message);
    }

    switch (cmd) {
        case CMD_GET_IP: {
              char buffer[16];
              ipAddressToString(Ethernet.localIP(), buffer);
              pubMsg("/controller/" + hexMac + "/ip", buffer);
            }
            break;
        case CMD_SET_IP: {
              if (arg != NULL) {
                IPAddress ip;
                if (stringToIpAddress(arg, ip) == 1) {
                  mqttClient.disconnect();
                  udp.stop();
                  startEthernet(ip);
                  udp.begin(UDP_LOCAL_PORT);
                  startMqtt();
                }
              }
            }
            break;
        case CMD_GET_UPDATE_INTERVAL: {
              char buffer[11];
              memset(buffer,'\0', 11);
              itoa(updateInterval, buffer, 10);
              pubMsg("/controller/" + hexMac + "/interval", buffer);
            }
            break;
        case CMD_SET_UPDATE_INTERVAL:
            if (arg != NULL) {
              setUpdateInterval(atoi(arg));
            }
            break;
        case CMD_GET_NTP_SERVER: {
              char buffer[16];
              ipAddressToString(timeServer, buffer);
              pubMsg("/controller/" + hexMac + "/ntp", buffer);
            }
            break;
        case CMD_SET_NTP_SERVER:
            if (arg != NULL) {
              IPAddress ip;
              if (stringToIpAddress(arg, ip) == 1) {
                timeServer = ip;

                time_t t = getNtpTime();
                if (t != 0) {
                  setTime(t);
                }
              }
            }
            break;
        default: 
            Serial.print("unknown command: ");
            Serial.println(cmd);
    }
    
    if (arg != NULL) {
      delete arg;
    }
}

int ipAddressToString(IPAddress ip, char* ipAsString) {
  return sprintf(ipAsString, "%d.%d.%d.%d", ip[0], ip[1], ip[2], ip[3]);
}

int stringToIpAddress(char* ipAsString, IPAddress &ip) {
  DNSClient dnsClient;
  return dnsClient.inet_aton(ipAsString, ip);
}

void setUpdateInterval(int newInterval) {
    if (newInterval >= 5 && newInterval < 86400) {
        updateInterval = newInterval;
    }
}

time_t getNtpTime() {
  while (udp.parsePacket() > 0) ; // discard any previously received packets

  sendNTPpacket(timeServer);
  uint32_t beginWait = millis();
  while (millis() - beginWait < 1500) {
    int size = udp.parsePacket();
    if (size >= NTP_PACKET_SIZE) {
      udp.read(packetBuffer, NTP_PACKET_SIZE);  // read packet into the buffer
      unsigned long secsSince1900;
      // convert four bytes starting at location 40 to a long integer
      secsSince1900 =  (unsigned long)packetBuffer[40] << 24;
      secsSince1900 |= (unsigned long)packetBuffer[41] << 16;
      secsSince1900 |= (unsigned long)packetBuffer[42] << 8;
      secsSince1900 |= (unsigned long)packetBuffer[43];
      return secsSince1900 - 2208988800UL + TIME_ZONE * SECS_PER_HOUR;
    }
  }
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
  udp.beginPacket(address, 123); //NTP requests are to port 123
  udp.write(packetBuffer, NTP_PACKET_SIZE);
  udp.endPacket();
}
