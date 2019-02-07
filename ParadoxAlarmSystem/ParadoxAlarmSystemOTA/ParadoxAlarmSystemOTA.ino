#include <FS.h>   
//#include <SoftwareSerial.h>
#include <ESP8266WiFi.h>
#include <ESP8266mDNS.h>
#include <ESP8266SSDP.h>
#include <ESP8266WebServer.h>
#include <WiFiUdp.h>
#include <ArduinoOTA.h>
#include <DNSServer.h>
#include <PubSubClient.h>
#include <WiFiManager.h>
#include <ArduinoJson.h>
#include <NTPtimeESP.h>


#define mqtt_server       "192.168.4.225"
#define mqtt_port         "1883"

#define Hostname          "paradoxdCTL" //not more than 15 

#define Stay_Arm  0x01
#define Stay_Arm2 0x02
#define Sleep_Arm 0x03
#define Full_Arm 0x04
#define Disarm  0x05
#define Bypass 0x10
#define PGMon 0x32
#define PGMoff 0x33

#define MessageLength 37

#define LED LED_BUILTIN
#define Serial_Swap 1 //if 1 uses d13 d15 for rx/tx 0 uses default rx/tx

#define Hassio 1  // 1 enables 0 disables HAssio support
#define HomeKit 0 

/*
Characteristic.SecuritySystemCurrentState.STAY_ARM = 0;
Characteristic.SecuritySystemCurrentState.AWAY_ARM = 1;
Characteristic.SecuritySystemCurrentState.NIGHT_ARM = 2;
Characteristic.SecuritySystemCurrentState.DISARMED = 3;
Characteristic.SecuritySystemCurrentState.ALARM_TRIGGERED = 4;
*/



bool TRACE = 0;
bool OTAUpdate = 0;

NTPtime NTPch("gr.pool.ntp.org");
strDateTime dateTime;

 char *root_topicOut = "paradoxdCTL/out";
 char *root_topicStatus = "paradoxdCTL/status";
 char *root_topicIn = "paradoxdCTL/in";
 char *root_topicArmStatus = "paradoxdCTL/status/Arm";
 char *root_topicArmHomekit = "paradoxdCTL/HomeKit";
 char *root_topicZoneStatus = "paradoxdCTL/status/Zone";
//root_topicArmStatus

WiFiClient espClient;
// client parameters
PubSubClient client(espClient);

bool shouldSaveConfig = false;
bool ResetConfig = false;
bool PannelConnected =false;
bool PanelError = false;
bool RunningCommand=false;
bool JsonParseError=false;
 
char inData[38]; // Allocate some space for the string
char acData[38];
char outData[38];
byte pindex = 0; // Index into array; where to store the character

long lastStatusSent = 0;

ESP8266WebServer HTTP(80);

struct inPayload
{
  byte PcPasswordFirst2Digits;
  byte PcPasswordSecond2Digits;
  byte Command;
  byte Subcommand;
 } ;
 
typedef struct {
     byte armstatus;
     byte event;
     byte sub_event;    
     String dummy;
     
 } Payload;


 typedef struct {
     int intArmStatus;
     String stringArmStatus;
     int sent;
 } paradoxArm;

 paradoxArm hassioStatus;
 
 paradoxArm homekitStatus;
 
 Payload paradox;

  


//JsonObject& zones = root.createNestedObject("zones");

void setup() {
  pinMode(LED, OUTPUT);
  blink(100);
  delay(1000);
  WiFi.mode(WIFI_STA);

  
  Serial.begin(9600);
  Serial.flush(); // Clean up the serial buffer in case previous junk is there
  if (Serial_Swap)
  {
    Serial.swap();
  }

  Serial1.begin(9600);
  Serial1.flush();
  Serial1.setDebugOutput(true);
  trc("serial monitor is up");
  serial_flush_buffer();

  

  trc("Running MountFs");
  mountfs();

  setup_wifi();
  StartSSDP();
  

  ArduinoOTA.setHostname(Hostname);
  ArduinoOTA.begin();
  trc("Finnished wifi setup");
  delay(1500);
  digitalWrite(LED, HIGH);
  ArmState();
  
  NTPch.setRecvTimeout(5);
  dateTime = NTPch.getNTPtime(2.0, 1);
}

void loop() {
   
   readSerial();        


}


byte checksumCalculate(byte checksum) 
{
  while (checksum > 255) {
    checksum = checksum - (checksum / 256) * 256;
  }

  return checksum & 0xFF;
}

void StartSSDP()
{
  if (WiFi.waitForConnectResult() == WL_CONNECTED) {

    Serial1.printf("Starting HTTP...\n");
    HTTP.on("/index.html", HTTP_GET, []() {
      HTTP.send(200, "text/plain", Hostname);
    });
    HTTP.on("/", HTTP_GET, []() {
      HTTP.send(200, "text/plain", Hostname);
    });

    HTTP.on("/description.xml", HTTP_GET, []() {
      SSDP.schema(HTTP.client());
    });
    HTTP.begin();

    Serial1.printf("Starting SSDP...\n");
    SSDP.setSchemaURL("description.xml");
    SSDP.setDeviceType("upnp:rootdevice");
    SSDP.setHTTPPort(80);
    SSDP.setName(Hostname);
    SSDP.setSerialNumber(WiFi.macAddress());
    SSDP.setURL(String("http://") + WiFi.localIP().toString().c_str() +"/index.html");
    SSDP.setModelName("ESP8266Wemos");
    SSDP.setModelNumber("WEMOSD1");
    SSDP.setModelURL("https://github.com/maragelis/ParadoxRs232toMqtt");
    SSDP.setManufacturer("PM ELECTRONICS");
    SSDP.setManufacturerURL("https://github.com/maragelis/");
    SSDP.begin();

    if (!MDNS.begin(Hostname)) {
    trc("Error setting up MDNS responder!");
    while (1) {
      delay(1000);
    }
  }
    trc("mDNS responder started");

  

  // Add service to MDNS-SD
    MDNS.addService("http", "tcp", 80);

    trc("Ready!\n");
  }
}



void updateArmStatus()
{
  bool datachanged = false;
  if (paradox.event == 2)
  {
    switch (paradox.sub_event)
    {
      case 4:
        hassioStatus.stringArmStatus = "triggered";
        homekitStatus.stringArmStatus = "ALARM_TRIGGERED";
        homekitStatus.intArmStatus=4;
        datachanged=true;
        
      break;

      case 11:
        hassioStatus.stringArmStatus = "disarmed";
        homekitStatus.stringArmStatus = "DISARMED";
        homekitStatus.intArmStatus = 3;
        datachanged=true;
        
        break;

      case 12:
         hassioStatus.stringArmStatus = "armed_away";
         homekitStatus.stringArmStatus = "AWAY_ARM";
         homekitStatus.intArmStatus = 1;
         
         datachanged=true;
        break;

      default : break;
    }
  }
  else if (paradox.event == 6)
  {
    if (paradox.sub_event == 3)
    {
      datachanged=true;
      hassioStatus.stringArmStatus = "armed_home";
      homekitStatus.stringArmStatus = "STAY_ARM";
      homekitStatus.intArmStatus = 0;
      
    }
    else if ( paradox.sub_event == 4)
    {
      datachanged=true;
      hassioStatus.stringArmStatus = "armed_home";
      homekitStatus.stringArmStatus = "NIGHT_ARM";
      homekitStatus.intArmStatus = 2;
      
    }
    
  }
  
        
}

void sendArmStatus()
{
  char output[128];
  StaticJsonBuffer<128> jsonBuffer;
  JsonObject& root = jsonBuffer.createObject();
        if (Hassio)
        {
          sendMQTT(root_topicArmStatus,hassioStatus.stringArmStatus);  
        }
        if (HomeKit)
        {
          root["Armstatus"]=homekitStatus.intArmStatus;
          root["ArmStatusD"]=homekitStatus.stringArmStatus ;
          root.printTo(output);
          sendCharMQTT(root_topicArmHomekit,output); 
        }
}


void SendJsonString(byte armstatus, byte event, byte sub_event, String dummy )
{
  if ((Hassio || HomeKit) && (event == 2 || event == 6))
  {
    updateArmStatus(); 
    if (( homekitStatus.sent != homekitStatus.intArmStatus) )
    {
      sendArmStatus();
      homekitStatus.sent = homekitStatus.intArmStatus;
    }
    
  }    

  if ((Hassio ) && (event == 1 || event == 0))
  {
    char ZoneTopic[80];
    char stateTopic[80];

    String zone = String(root_topicOut) + "/zone";
    zone.toCharArray(ZoneTopic, 80);

    String state_topic = String(root_topicOut) + "/state";
    state_topic.toCharArray(stateTopic, 80);

    if (event == 1 || event == 0)
    {

      zone = String(ZoneTopic) + String(sub_event);
      zone.toCharArray(ZoneTopic, 80);

      String zonestatus="OFF";

      if (event==1 )
      {
        zonestatus = "ON";
      }

      sendMQTT(ZoneTopic, zonestatus);
    }
    
  }
  
  if ((HomeKit ) && (event == 1 || event == 0))
  {
    char output[128];
    StaticJsonBuffer<128> jsonBuffer;
    JsonObject& homekitmsg = jsonBuffer.createObject();
    homekitmsg["zone"]=sub_event;
    dummy.trim();
    homekitmsg["zoneName"]=String(dummy);
    homekitmsg["state"]=event==1?true:false;
    homekitmsg.printTo(output);
    sendCharMQTT(root_topicArmHomekit,output); 
  }

      char outputMQ[256];
     StaticJsonBuffer<256> jsonBuffer;
    JsonObject& root = jsonBuffer.createObject();
    //root["armstatus"]=homekitStatus.intArmStatus;
    //root["armstatusD"]=homekitStatus.stringArmStatus;
    root["event"]=event;
    root["eventD"]=getEvent(event);
    
    root["sub_event"]=sub_event;
    root["sub_eventD"]=getSubEvent(event,sub_event);
    root["data"]=dummy;
    
    root.printTo(outputMQ);
    sendCharMQTT(root_topicOut,outputMQ); 
    
     //String retval = "{ \"armstatus\":" + String(homekitStatus.intArmStatus) + ", \"event\":" + String(event) + ", \"sub_event\":" + String(sub_event) + ", \"dummy\":\"" + String(dummy) + "\"}";
    //sendMQTT(root_topicOut,retval);   
}



void sendMQTT(String topicNameSend, String dataStr){
    handleMqttKeepAlive();
    char topicStrSend[40];
    topicNameSend.toCharArray(topicStrSend,26);
    char dataStrSend[200];
    dataStr.toCharArray(dataStrSend,200);
    boolean pubresult = client.publish(topicStrSend,dataStrSend);
    trc("sending ");
    trc(dataStr);
    trc("to ");
    trc(topicNameSend);

}

void sendCharMQTT(char* topic, char* data)
{
  handleMqttKeepAlive();
  
  boolean pubresult = client.publish(topic, data);
  
}


void readSerial(){
  while (Serial.available()<37  )  
     { 
      
      if (OTAUpdate)
      {
        ArduinoOTA.handle();
      }
      handleMqttKeepAlive();
      HTTP.handleClient();
       
       
      
     }                            
     {
       readSerialData();       
     }

}

void readSerialQ(){
  while (Serial.available()<37  )  
     { }                            
     {
       pindex=0;
        
        while(pindex < 37) // Paradox packet is 37 bytes 
        {
            acData[pindex++]=Serial.read();  
                     
        }   
         acData[++pindex]=0x00;  
     }

}


void readSerialData() {
         pindex=0;
        
        while(pindex < 37) // Paradox packet is 37 bytes 
        {
            inData[pindex++]=Serial.read();  
        }
       
            inData[++pindex]=0x00; // Make it print-friendly
           
              if ((inData[0] & 0xF0) == 0xE0)
              { // Does it look like a valid packet?
                paradox.armstatus = inData[0];
                paradox.event = inData[7];
                paradox.sub_event = inData[8];
                String zlabel = String(inData[15]) + String(inData[16]) + String(inData[17]) + String(inData[18]) + String(inData[19]) + String(inData[20]) + String(inData[21]) + String(inData[22]) + String(inData[23]) + String(inData[24]) + String(inData[25]) + String(inData[26]) + String(inData[27]) + String(inData[28]) + String(inData[29]) + String(inData[30]);
                if (inData[14]!= 1){
                paradox.dummy = zlabel;
                }
                SendJsonString(paradox.armstatus, paradox.event, paradox.sub_event, paradox.dummy);
                if (inData[7] == 48 && inData[8] == 3)
                {
                  PannelConnected = false;
                  trc("panel logout");
                  // sendMQTT(root_topicStatus, "{\"status\":\"Panel logout\"}");
                   
                }
                else if (inData[7] == 48 && inData[8] == 2 )
                {
                  PannelConnected = true;
                  trc("panel Login");
                  //sendMQTT(root_topicStatus, "{\"status\":\"Panel Login Success\"}");
                }
                
              }else
              {
                 serial_flush_buffer();
              }
              
}

void blink(int duration) {
   
  digitalWrite(LED_BUILTIN,LOW);
  delay(duration);
  digitalWrite(LED_BUILTIN,HIGH);
 
}
void saveConfigCallback () {
  trc("Should save config");
  shouldSaveConfig = true;
}

void callback(char* topic, byte* payload, unsigned int length) {
  // In order to republish this payload, a copy must be made
  // as the orignal payload buffer will be overwritten whilst
  // constructing the PUBLISH packet.
   if (RunningCommand){
     trc("Command already Running exiting");
      return;
    }
  trc("Hey I got a callback ");
  // Conversion to a printable string
  payload[length] = '\0';
  inPayload data;
  
  trc("JSON Returned! ====");
  String callbackstring = String((char *)payload);
  if (callbackstring == "Trace=1")
  {
    TRACE=1;
    Serial1.println("Trace is ON");
    return ;
  }
  else if (callbackstring == "Trace=0")
  {
    TRACE=0;
    Serial1.println("Trace is OFF");
    return ;
  }
  else if (callbackstring == "OTA=0")
  {
      OTAUpdate=0;
      Serial1.println("OTA update is OFF");
  }
  else if (callbackstring == "OTA=1")
  {
    OTAUpdate=1;
      Serial1.println("OTA update is ON");
  }
  else if (callbackstring=="")
  {
    trc("No payload data");
    return;
  }
    else
    {
      trc("parsing Recievied Json Data");
      data = Decodejson((char *)payload);
      if (JsonParseError)
      {
        trc("Error parsing Json Command") ;
        JsonParseError=false;
        return;
      }
      trc("Json Data is ok ");
      PanelError = false;
      trc("Start login");
      if (!PannelConnected)
        doLogin(data.PcPasswordFirst2Digits, data.PcPasswordSecond2Digits);
      trc("end login");
      
    }

  RunningCommand=true;
  if (!PannelConnected)
  {
    trc("Problem connecting to panel");
    sendMQTT(root_topicStatus, "{\"status\":\"Problem connecting to panel\"}");
  }else if (data.Command == 0x90  ) 
  {
    
    trc("Running panel status command");
    if (data.Subcommand==0)
    {
     PanelStatus0(false,0);
    }
    if (data.Subcommand==1)
    {
     PanelStatus1(false);
    }
  }
  else if (data.Command == 0x91  )  {
    trc("Running ArmState");
    ArmState();
  }
  else if (data.Command == 0x30)
  {
    trc("Running Setdate");
    panelSetDate();
  }
  else if (data.Command == 0x92)
  {
    trc("Running ZoneState");
      ZoneState(data.Subcommand);
  } 
   else if (data.Command != 0x00  )  {
     trc("Running Command");
      ControlPanel(data);
  } 
  else  {
    trc("Bad Command ");
    sendMQTT(root_topicStatus, "{\"status\":\"Bad Command\" }");
  }
  
  RunningCommand=false;
  
}


byte getPanelCommand(String data){
  byte retval=0x00;

  data.toLowerCase();
  if (data == "stay" || data=="0")
  {
    
    retval = Stay_Arm;
    
  }

  else if (data == "arm" || data=="1")
  {    
    retval= Full_Arm;
  }
  else if (data == "sleep" || data=="2")

  {
    
    retval= Sleep_Arm;
    
  }

  else if (data == "disarm" || data == "3")

  {
    
    retval=Disarm;
    
  }


  else if (data == "bypass" || data == "10")

  {
    
    retval=Bypass;
    
  }

  else if (data == "pgm_on" || data == "pgmon")
  {
    retval = PGMon;
  }

  else if (data == "pgm_off" || data == "pgmoff")
  {
    retval = PGMoff;
  }

  else if (data == "panelstatus" )
  {
    retval=0x90;
    trc("PAnelStatus command ");
    
  }

  else if (data == "setdate")
  {
    retval=0x30;
    
  }
  else if (data == "armstate")
  {
    retval=0x91;
    
  }
   else if (data == "zonestate")
  {
    retval=0x92;
    
  }

  else if (data == "disconnect" || data == "99")
  {
    retval=0x00;
    //PanelDisconnect();
  }
    if(TRACE)
    {
      Serial1.print("returned command = ");
      Serial1.println(retval , HEX);
    }
  return retval;
}



void panelSetDate(){

  dateTime = NTPch.getNTPtime(2.0, 1);
  
  if (dateTime.valid)
  {
    
    byte actualHour = dateTime.hour;
    byte actualMinute = dateTime.minute;
    byte actualyear = (dateTime.year - 2000) & 0xFF ;
    byte actualMonth = dateTime.month;
    byte actualday = dateTime.day;
  

    byte data[MessageLength] = {};
    byte checksum;
    memset(data, 0, sizeof(data));

    data[0] = 0x30;
    data[4] = 0x21;         //Century
    data[5] = actualyear;   //Year
    data[6] = actualMonth;  //Month
    data[7] = actualday;    //Day
    data[8] = actualHour;   //Time
    data[9] = actualMinute; // Minutes
    data[33] = 0x05;

    checksum = 0;
    for (int x = 0; x < MessageLength - 1; x++)
    {
      checksum += data[x];
    }

    data[36] = checksumCalculate(checksum);
    serialCleanup();
    Serial.write(data, MessageLength);
    readSerialQ();
    
   
    
  }else
  {
   sendMQTT(root_topicStatus,"{\"status\":\"ERROR getting NTP Date  \" }");
  }
}

void serialCleanup()
{
   while (Serial.available()>37)
  {
    trc("serial cleanup");
    readSerial();
  }
}

void ControlPanel(inPayload data){
  byte armdata[MessageLength] = {};
  byte checksum;
  memset(armdata,0, sizeof(armdata));

  armdata[0] = 0x40;
  armdata[2] = data.Command;
  armdata[3] = data.Subcommand;;
  armdata[33] = 0x05;
  armdata[34] = 0x00;
  armdata[35] = 0x00;
  checksum = 0;
  for (int x = 0; x < MessageLength - 1; x++)
  {
    checksum += armdata[x];
  }
  armdata[36] = checksumCalculate(checksum);
  

  trc("sending Data");
  serialCleanup();
  Serial.write(armdata, MessageLength);
  readSerialQ();

  if ( acData[0]  >= 40 && acData[0] <= 45)
  {
    sendMQTT(root_topicStatus, "{\"status\":\"Command success\"} ");
    trc(" Command success ");
    }
  

}

void PanelDisconnect(){
  byte data[MessageLength] = {};
  byte checksum;
  memset(data, 0, sizeof(data));

  data[0] = 0x70;
  data[2] = 0x05;
  data[33] = 0x05;

  checksum = 0;
  for (int x = 0; x < MessageLength - 1; x++)
  {
    checksum += data[x];
  }

  

  data[36] = checksumCalculate(checksum);
  serialCleanup();
  Serial.write(data, MessageLength);
  

  
}

void PanelStatus0(bool showonlyZone ,int zone)
{
  byte data[MessageLength] = {};
  byte checksum;
  memset(data, 0, sizeof(data));

  serial_flush_buffer();
  data[0] = 0x50;
  data[1] = 0x00;
  data[2] = 0x80;
  data[3] = 0x00;
  data[33] = 0x05;
 checksum = 0;
  for (int x = 0; x < MessageLength - 1; x++)
  {
    checksum += data[x];
  }

  

  data[36] = checksumCalculate(checksum);
  serialCleanup();
  Serial.write(data, MessageLength);
  readSerialQ();

    bool Timer_Loss = bitRead(acData[4],7);
    bool PowerTrouble  = bitRead(acData[4],1);
    bool ACFailureTroubleIndicator = bitRead(acData[6],1);
    bool NoLowBatteryTroubleIndicator = bitRead(acData[6],0);
    bool TelephoneLineTroubleIndicator = bitRead(acData[8],0);
    int ACInputDCVoltageLevel = acData[15];
    int PowerSupplyDCVoltageLevel =acData[16];
    int BatteryDCVoltageLevel=acData[17];

    if (!showonlyZone)
    {
        StaticJsonBuffer<256> jsonBuffer;
        JsonObject& root = jsonBuffer.createObject();
        root["Timer_Loss"]=String(Timer_Loss);
        root["PowerTrouble"]=String(PowerTrouble);
        root["ACFailureTrouble"]=String(ACFailureTroubleIndicator);
        root["TelephoneLineTrouble"]=String(TelephoneLineTroubleIndicator);
        root["PSUDCVoltage"]=String(PowerSupplyDCVoltageLevel);
        root["BatteryDCVoltage"]=String(BatteryDCVoltageLevel);
        root["BatteryTrouble"]=String(NoLowBatteryTroubleIndicator);
        char output[256];
        root.printTo(output);
        sendCharMQTT(root_topicStatus,output);  
    }
    String retval = "";
    String Zonename ="";
    int zcnt = 0;

    
        
    for (int i = 19 ; i <= 22;i++)
    {
      StaticJsonBuffer<256> jsonBuffer;
        JsonObject& zonemq = jsonBuffer.createObject();
     for (int j = 0 ; j < 8;j++) 
       {
         Zonename = "Z" + String(++zcnt);

       
        zonemq[Zonename] =  bitRead(acData[i],j);
        
        //trc (retval);
       
       }
       char Zonemq[256];
        zonemq.printTo(Zonemq);
        sendCharMQTT(root_topicStatus,Zonemq); 
    }
    
    
}

void ZoneState(int zone)
{
  PanelStatus0(true,zone);
}

void ArmState()
{
  PanelStatus1(true);
}

void PanelStatus1(bool ShowOnlyState)
{
  byte data[MessageLength] = {};
  byte checksum;
  memset(data, 0, sizeof(data));

  serial_flush_buffer();
  data[0] = 0x50;
  data[1] = 0x00;
  data[2] = 0x80;
  data[3] = 0x01;
  data[33] = 0x05;

  checksum = 0;
  for (int x = 0; x < MessageLength - 1; x++)
  {
    checksum += data[x];
  }

  

  data[36] = checksumCalculate(checksum);
  serialCleanup();
  Serial.write(data, MessageLength);
    
    readSerial();

  bool Fire=bitRead(acData[17],7);
  bool Audible=bitRead(acData[17],6);
  bool Silent=bitRead(acData[17],5);
  bool AlarmFlg=bitRead(acData[17],4);
  bool StayFlg=bitRead(acData[17],2);
  bool SleepFlg=bitRead(acData[17],1);
  bool ArmFlg=bitRead(acData[17],0);


//sendMQTT(root_topicStatus,"Timer_Loss=" +String(inData[4]) );
String retval = "{ \"Fire\":\""  + String(Fire) + "\"" +
                  ",\"Audible\":\""  + String(Audible) + "\"" +
                  ",\"Silent\":\""  + String(Silent) + "\"" +
                  ",\"AlarmFlg\":\""  + String(AlarmFlg) + "\"" +
                  ",\"StayFlg\":\""  + String(StayFlg) + "\"" +
                  ",\"SleepFlg\":\""  + String(SleepFlg) + "\"" +
                  ",\"ArmFlg\":\"" + String(ArmFlg) + "\"}";


    trc(retval);
    if (!ShowOnlyState)
    {
    sendMQTT(root_topicStatus,retval);
    }

     if (AlarmFlg)
    {
       //retval = "{ \"Armstatus\":4,\"ArmstatusDescr\":\"ALARM_TRIGGERED\"}" ;
       hassioStatus.stringArmStatus="triggered";
       homekitStatus.stringArmStatus="ALARM_TRIGGERED";
       homekitStatus.intArmStatus=4;
    }
    else if (StayFlg)
    {
       //retval = "{ \"Armstatus\":0,\"ArmstatusDescr\":\"STAY_ARM\"}" ;
       hassioStatus.stringArmStatus="armed_home";
       homekitStatus.stringArmStatus="STAY_ARM";
       homekitStatus.intArmStatus=0;
    }else if (SleepFlg)
    {
       //retval = "{ \"Armstatus\":2,\"ArmstatusDescr\":\"NIGHT_ARM\"}" ;
        hassioStatus.stringArmStatus="armed_home";
       homekitStatus.stringArmStatus="NIGHT_ARM";
       homekitStatus.intArmStatus=2;
    }
    else if (ArmFlg)
    {
       //retval = "{ \"Armstatus\":1,\"ArmstatusDescr\":\"AWAY_ARM\"}" ;
        hassioStatus.stringArmStatus = "armed_away";
         homekitStatus.stringArmStatus = "AWAY_ARM";
         homekitStatus.intArmStatus = 1;
    }
    else if (!SleepFlg && !StayFlg && !ArmFlg)
    {
      //retval = "{ \"Armstatus\":3,\"ArmstatusDescr\":\"DISARMED\"}" ;
        hassioStatus.stringArmStatus = "disarmed";
        homekitStatus.stringArmStatus = "DISARMED";
        homekitStatus.intArmStatus = 3;
    }
    
    else
    {
       retval = "{ \"Armstatus\":99,\"ArmstatusDescr\":\"unknown\"}" ;
        hassioStatus.stringArmStatus = "unknown";
        homekitStatus.stringArmStatus = "unknown";
        homekitStatus.intArmStatus = 99;
    }
    //sendMQTT(root_topicArmStatus,retval);
    sendArmStatus();
    
  bool zoneisbypassed =bitRead(acData[18],3);
  bool ParamedicAlarm=bitRead(acData[19],7);
 



 retval = "{ \"zoneisbypassed\":\""  + String(zoneisbypassed) + "\"" +
                  ",\"ParamedicAlarm\":\"" + String(ParamedicAlarm) + "\"}";

    trc(retval);
    if (!ShowOnlyState)
    {
    sendMQTT(root_topicStatus,retval);
    }
  

}
void doLogin(byte pass1, byte pass2){
  byte data[MessageLength] = {};
  byte data1[MessageLength] = {};
  byte checksum;
if (TRACE)
{
  trc("Running doLogin Function");
  
}
  memset(data, 0, sizeof(data));
  memset(data1, 0, sizeof(data1));

  
  data[0] = 0x5f;
  data[1] = 0x20;
  data[33] = 0x05;
  data[34] = 0x00;
  data[35] = 0x00;

  checksum = 0;
  for (int x = 0; x < MessageLength - 1; x++)
  {
    checksum += data[x];
  }

  

  data[36] = checksumCalculate(checksum);

  if (TRACE)
  {
    for (int x = 0; x < MessageLength; x++)
    {
      //Serial1.print("Address-");
      //Serial1.print(x);
      //Serial1.print("=");
      //Serial1.println(data[x], HEX);
    }
  }
    serialCleanup();
    Serial.write(data, MessageLength);
    
    readSerialQ();
    if (TRACE)
    {
       for (int x = 0; x < MessageLength; x++)
       {
         //Serial1.print("replAddress-");
         //Serial1.print(x);
         //Serial1.print("=");
         //Serial1.println(acData[x], HEX);
         
       }
    }
      
      data1[0] = 0x00;
      data1[4] = acData[4];
      data1[5] = acData[5];
      data1[6] = acData[6];
      data1[7] = acData[7];
      data1[7] = acData[8];
      data1[9] = acData[9];
      //data1[10] = pass1; //panel pc password digit 1 & 2
      //data1[11] = pass2; //panel pc password digit 3 & 4
      data1[10] = 0x00;
      data1[11] = 0x00;
      data1[13] = 0x55;
      data1[14] = pass1; //panel pc password digit 1 & 2
      data1[15] = pass2; //panel pc password digit 3 & 4
      data1[33] = 0x05;

      checksum = 0;
      for (int x = 0; x < MessageLength - 1; x++)
      {
        checksum += data1[x];
      }
      

      data1[36] = checksumCalculate(checksum);

      if (TRACE)
      {
         for (int x = 0; x < MessageLength; x++)
         {
          // Serial1.print("SendinGINITAddress-");
          // Serial1.print(x);
          // Serial1.print("=");
          // Serial1.println(data1[x], HEX);
         }
      }
      serialCleanup();
      Serial.write(data1, MessageLength);
      readSerialQ();
      if (acData[0]==0x10  && acData[1]==0x25)
      {
        PannelConnected = true;
        trc("panel Login");
        //sendMQTT(root_topicStatus, "{\"status\":\"Panel NEware direct successfull connecting\"}");
      }else
      {
        trc("Login response number0");
            trc(String(acData[0],HEX));
                trc("Login response number1");
                    trc(String(acData[1],HEX));
      }
      if (TRACE)
      {
         for (int x = 0; x < MessageLength; x++)
         {
           Serial1.print("lastAddress-");
           Serial1.print(x);
           Serial1.print("=");
           Serial1.println(acData[x], HEX);
         }
      }
        
        
}

struct inPayload Decodejson(char *Payload){
  inPayload indata;
  DynamicJsonBuffer jsonBuffer;
  JsonObject &root = jsonBuffer.parseObject(Payload);
  if (!root.success())
  {
    indata = {0x00,0x00,0x00,0x00};
    trc("JSON parsing failed!");
    JsonParseError=true;
    return indata;
  }
  else
  {
    char charpass1[4];
    char charpass2[4];
    char charsubcommand[4];
    
    String password = root["password"];
    String command = root["Command"];
    String subcommand = root["Subcommand"];

    String pass1 = password.substring(0, 2);
    String pass2 = password.substring(2, 4);

    // trc(pass1);
    // trc(pass2);

    pass1.toCharArray(charpass1, 4);
    pass2.toCharArray(charpass2, 4);
    subcommand.toCharArray(charsubcommand,4);

    
        
        // trc(password);
        // trc(command);

        // trc(charpass1);
        // trc(charpass2);

  unsigned long number1 = strtoul(charpass1, nullptr, 16);
  unsigned long number2 = strtoul(charpass2, nullptr, 16);
  unsigned long number3 = strtoul(charsubcommand, nullptr, 16);

  if (number2 < 10)
    number2 = number2 + 160;

  if (number1 < 10)
    number1 = number1 + 160;

  byte PanelPassword1 = number1 & 0xFF; 
  byte PanelPassword2 = number2 & 0xFF; 
  byte SubCommand = number3 & 0xFF;



  byte CommandB = getPanelCommand(command) ;

    // if (TRACE)
    // {
    //   Serial1.print("0x");
    //   Serial1.println(PanelPassword1, HEX);
    //   Serial1.print("0x");
    //   Serial1.println(PanelPassword2, HEX);
    // }
      inPayload data1 = {PanelPassword1, PanelPassword2, CommandB, SubCommand};

      return data1;
  }

  return indata;
}

void serial_flush_buffer(){
  while (Serial.read() >= 0)
  ;
}

void setup_wifi(){
  
    
  
    WiFiManagerParameter custom_mqtt_server("server", "mqtt server", mqtt_server, 40);
    WiFiManagerParameter custom_mqtt_port("port", "mqtt port", mqtt_port, 6);

  
    WiFiManager wifiManager;
    if (ResetConfig)
    {
      trc("Resetting wifiManager");
      WiFi.disconnect();
      wifiManager.resetSettings();
    }
   
    
    if (mqtt_server=="" || mqtt_port=="")
    {
      trc("Resetting wifiManager");
      WiFi.disconnect();
      wifiManager.resetSettings();
      ESP.reset();
      delay(1000);
    }
    else
    {
      trc("values ar no null ");
    }


    wifiManager.setSaveConfigCallback(saveConfigCallback);
    wifiManager.setConfigPortalTimeout(180);
    
    
    wifiManager.addParameter(&custom_mqtt_server);
    wifiManager.addParameter(&custom_mqtt_port);
    

    
    if (!wifiManager.autoConnect(Hostname, "")) {
      trc("failed to connect and hit timeout");
      delay(3000);
      //reset and try again, or maybe put it to deep sleep
      ESP.reset();
      delay(5000);
    }
  
  
  
    
  
  
    //if you get here you have connected to the WiFi
    trc("connected...yeey :)");
  
    //read updated parameters
    strcpy(mqtt_server, custom_mqtt_server.getValue());
    strcpy(mqtt_port, custom_mqtt_port.getValue());
    
    //save the custom parameters to FS
    if (shouldSaveConfig) {
      trc("saving config");
      DynamicJsonBuffer jsonBuffer;
      JsonObject& json = jsonBuffer.createObject();
      json["mqtt_server"] = mqtt_server;
      json["mqtt_port"] = mqtt_port;
      
      File configFile = SPIFFS.open("/config.json", "w");
      if (!configFile) {
        trc("failed to open config file for writing");
      }
  
      json.printTo(Serial);
      json.printTo(configFile);
      configFile.close();
      //end save
    }
  
    //trc("local ip : ");
    //Serial1.println(WiFi.localIP());
  
    
    trc("Setting Mqtt Server values");
    trc("mqtt_server : ");
    trc(mqtt_server);
    trc("mqtt_server_port : ");
    trc(mqtt_port);

    trc("Setting Mqtt Server connection");
    unsigned int mqtt_port_x = atoi (mqtt_port); 
    client.setServer(mqtt_server, mqtt_port_x);
    
    client.setCallback(callback);
   
     reconnect();
    
    
    trc("");
    trc("WiFi connected");
    trc("IP address: ");
    
   trc((String)WiFi.localIP());
  
}

boolean reconnect() {
  // Loop until we're reconnected
  while (!client.connected()) {
    trc("Attempting MQTT connection...");
    String mqname =  WiFi.macAddress();
    char charBuf[50];
    mqname.toCharArray(charBuf, 50) ;

    if (client.connect(charBuf,root_topicStatus,0,false,"{\"status\":\"Paradox Disconnected\"}")) {
    // Once connected, publish an announcement...
      //client.publish(root_topicOut,"connected");
      trc("connected");
      sendMQTT(root_topicStatus, "{\"status\":\"Paradox connected\"}");
      //Topic subscribed so as to get data
      String topicNameRec = root_topicIn;
      //Subscribing to topic(s)
      subscribing(topicNameRec);
    } else {
      trc("failed, rc=");
      trc(String(client.state()));
      trc(" try again in 5 seconds");
      // Wait 5 seconds before retrying
      delay(5000);
    }
  }
  return client.connected();
}

void handleMqttKeepAlive()
{
  if (!client.connected())
  {
    reconnect();
  }
  client.loop();
}

void subscribing(String topicNameRec){ // MQTT subscribing to topic
  char topicStrRec[26];
  topicNameRec.toCharArray(topicStrRec,26);
  // subscription to topic for receiving data
  boolean pubresult = client.subscribe(topicStrRec);
  if (pubresult) {
    trc("subscription OK to");
    trc(topicNameRec);
  }
}

void mountfs(){
   if (SPIFFS.begin()) {
    trc("mounted file system");
    if (SPIFFS.exists("/config.json")) {
      //file exists, reading and loading
      trc("reading config file");
      File configFile = SPIFFS.open("/config.json", "r");
      if (configFile) {
        trc("opened config file");
        size_t size = configFile.size();
        // Allocate a buffer to store contents of the file.
        std::unique_ptr<char[]> buf(new char[size]);

        configFile.readBytes(buf.get(), size);
        DynamicJsonBuffer jsonBuffer;
        JsonObject& json = jsonBuffer.parseObject(buf.get());
        json.printTo(Serial);
        if (json.success()) {
          trc("\nparsed json");

          strcpy(mqtt_server, json["mqtt_server"]);
          strcpy(mqtt_port, json["mqtt_port"]);
          
        } else {
          trc("failed to load json config");
          
        }
      }
    }
    else
    {
      trc("File /config.json doesnt exist");
      //SPIFFS.format();
      trc("Formatted Spiffs");    
     

    }
  } else {
    trc("failed to mount FS");
  }
}




void trc(String msg){
  if (TRACE) {
  Serial1.println(msg);
 // sendMQTT(root_topicStatus,msg);
  }
}



 
