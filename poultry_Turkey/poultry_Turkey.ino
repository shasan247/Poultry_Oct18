
#define ADC_resulation 4096 // for ESP32 4096 (12 bit ADC) , for Arduino 1024 (10 bit ADC)
#define watchDogBiteThresholdTime 3600 //if main loop functio does not complete one full cycle within 180 seconds, watchDog timer will restart the MCU 
//-----------------------------Including required libraries-------------------------------------------------//
#include <stdio.h> //calculate date Time DIfference
#include <time.h> //calculate date  TIme DIfference
#include <EEPROM.h>
#include "soc/soc.h"
#include "soc/rtc_cntl_reg.h"
//#include <String.h>
#include <PubSubClient.h>//MQTT
#include <ArduinoJson.h>// parsing JSON object from MQTT message
#include <Wire.h>//RTC module DS3231
#include "RTClib.h"//RTC module DS3231
#include <MQ135.h>// Library of Co2 and NH4
#include "DHT.h"        // including the library of DHT11 temperature and humidity sensor
#include "WiFi.h"  //Library for connecting Wifi
// -----------------------------------------begin of OTA section------------------------------------
//#include "OTA_ESP32.h"

int EEPROMAddressInitFlag=190;
int EEPROMAddressCurrentFirmwareVersion=191;
int EEPROM_SIZE=200;

char* OTA_WiFi_SSID="Pheonix Cloud";// = "DataSoft_WiFi";
char* OTA_WiFi_Password="shs123456";// = "support123";
char* OTA_url_binFile="/OTA/poultry_Turkey/poultry_Turkey.ino.esp32.bin";
char* OTA_url_versionFile="/OTA/poultry_Turkey/version.txt";
char* OTA_host = "182.163.112.207"; // Host url of the server where update file reside in
int OTA_port= 80; // Non https. For HTTPS 443. As of today, HTTPS doesn't work.
WiFiClient wificlient;//this WiFiClient type object named "wificlient" is extern into OTA_ESP32.cpp file
// ----------------------------------------- end of OTA section ------------------------------------
PubSubClient client(wificlient);
//..........................................Custom Watchdog timer......................................
#include<ESP32Ticker.h>
Ticker secondTick;
volatile int watchdogCount=0;
void ISRwatchdog(){
  watchdogCount++;
  if(watchdogCount>=watchDogBiteThresholdTime){
    Serial.println();
    Serial.print("The watch dog bites......");
    ESP.restart();}}
//-----------------------------Defining required pins-------------------------------------------------------//

#define Light_ON LOW// AS relays are active LOW
#define Light_OFF HIGH
#define Fan_ON LOW// AS relays are active LOW
#define Fan_OFF HIGH
#define builtin_LED 2
#define dht_dpin 32 //GPIO16 digital pin
#define co2_NH4_sensor 35  //ADC7
#define Fan_Pin 17

byte Light[] = {14, 27, 26, 25};//L34, 14, 27, 26

//..........................Defining Constants...
#define EEPROM_Address_startYear_Bytes_L 5//From LSB, hold the lower 8 bit //6 to 10
#define EEPROM_Address_startYear_Bytes_H 6// hold the next 8 bit  
#define EEPROM_Address_startMonth_Bytes 7
#define EEPROM_Address_startDay_Bytes 8
#define EEPROM_Address_Mode_Bytes 9
#define EEPROM_Address_LightLevel_Bytes 10
#define EEPROM_Address_FanStatus_Bytes 11
#define EEPROM_Address_RestartCounter_Bytes 12
#define data_publishing_interval 60000
#define TimeOut_lightLevel 30000//120000
#define threshold_Level_Co2 60000
#define systemRestartThresholdTime 300  //if can't connect to wifi or MQTT then Restart system after trying 300 sec (5 minute)
//...........................Variable Decleration and Initialization ---------------------
 volatile int startYear = 0, startMonth = 0, startDay = 0;
volatile int current_minute=0; 
volatile float data1_temp = 0;
volatile float data2_hum = 0;
volatile float data3_Co2 = 0;
volatile float data4_NH4 = 0;
unsigned char totalLight= 0;
volatile unsigned char Current_elapsedTime = 0;
volatile unsigned char prev_elapsedTime = 0;
volatile int thresholdTemp_Lower_Limit = 0;
volatile int thresholdTemp_Upper_Limit = 0;
volatile long int prev_published_time = 0;
volatile unsigned char lightLevel = 0;
volatile unsigned char prev_lightLevel = 0;
volatile long int prev_lightLevel_Time = 0;
 unsigned char currentFanStatus = 0;
 volatile boolean fanChangeFlag=true;
volatile unsigned char prev_FanStatus = 0,backupFanStatus=0;
volatile long int prev_Fan_Time = 0;
volatile byte control_Mode = 1; // 1 means Auto and 0 means manual
volatile byte backup_control_Mode = 1;
volatile char messagee = 0;
unsigned long int reStartTimeHolder=0;
volatile byte clockErrorCounter=0;
volatile byte RestartCounter=0;
volatile int CYear=0;
volatile byte CMonth=0;
volatile byte CDay=0;
volatile byte CHour=0;
volatile byte CMinute=0;
volatile byte CSecond=0;
/*/----------------------------DHT ............................................
  #define DHTTYPE DHT22  // DHT 11
  DHT dht(dht_dpin, DHTTYPE);
*/
DHT dht;
//-----------------------------RTC
#define SECONDS_PER_DAY ( 24 * 60 * 60 )
RTC_DS3231 rtc;

//--------------------------------WiFi and MQTT credentials-----------------------------------------------//

#define UniqueMask "turkey"
const char* ssid = "DataSoft_WiFi";
const char* password = "support123";
const char* mqtt_server = "182.163.112.205";
const char* mqttUser = "sme";
const char* mqttPassword = "Sme@123@Mqtt";
const char* mqtt_topic_pub_firmwareVersion=UniqueMask"/fwVersion/87:01:ED";//{"fwVersion":EEPROReadCurrentVersion()}
const char* mqtt_topic_Pub_ADC = UniqueMask"/adc"; //{"ADC_Val:":ADC_Val}
const char* mqtt_topic_Pub_data = UniqueMask"/data";//{"Temp":temp,"Hum":hum,"Co2":Co2,"NH3":NH3}
const char* mqtt_topic_Pub_LightStatus = UniqueMask"/LightStatus/87:01:ED";//{"LightStatus":LightStatus}
const char* mqtt_topic_Pub_elapsedTime = UniqueMask"/ElapsedTime/87:01:ED";//{"ElapsedTime":ElapsedTime}
const char*  mqtt_topic_pub_FanStatus = UniqueMask"/FanStatus/87:01:ED"; //{"FanStatus":FanStatus}
const char*  mqtt_topic_pub_RestartStatus = UniqueMask"/RestartStatus/87:01:ED";//{\"ReStartNo\":" + RestartCounter +",\"H\":" + CHour +",\"M\":"+CMinute+",\"S\":"+CSecond+"}
const char* mqtt_topic_Pub_BatchStartDate=UniqueMask"/BatchStartDate/87:01:ED";//{\"SY\":"+startYear+",\"SM\":"+startMonth+",\"SD\":"+startDay+"};
const char* mqtt_topic_Pub_CurrentDateTime=UniqueMask"/CurrentDateTime/87:01:ED";//{\"y\":"+CYear+",\"m\":"+CMonth+",\"d\":"+CDay+",\"H\":"+CHour+",\"M\":"+CMinute+",\"S\":"+CSecond+"};
const char*  mqtt_topic_sub_date = UniqueMask"/date/87:01:ED";//{"Day":day,"Month":month,"Year":year}
const char*  mqtt_topic_sub_Current_date = UniqueMask"/currentDate/87:01:ED";//{"Day":day,"Month":month,"Year":year,"Hour":hour,"Min":min}
const char*  mqtt_topic_sub_mode = UniqueMask"/mode/87:01:ED";//{"Mode":mode}
const char*  mqtt_topic_sub_light = UniqueMask"/light/87:01:ED"; //{"LightLevel":lightLevel}
const char*  mqtt_topic_sub_restart = UniqueMask"/restart/87:01:ED"; //{"Restart":1}
const char*  mqtt_topic_sub_FanStatus = UniqueMask"/ComFanStatus/87:01:ED";//{"FanStatus":FanStatus}
const char* mqtt_topic_sub_ReqFromClient=UniqueMask"/ClientReq/87:01:ED";//{"ClientReq":1}
const int mqttPort = 1883;

void setup()
{
  WRITE_PERI_REG(RTC_CNTL_BROWN_OUT_REG, 1); //disable brownout detector
  Wire.begin();
  reStartTimeHolder=millis();
  pinMode(builtin_LED, OUTPUT);
  digitalWrite(builtin_LED, Light_OFF);
  delay(2000);
  Serial.begin(9600);
  secondTick.attach(1,ISRwatchdog);
  Serial.print("\n\n\nSystem Booting. please wait");
  //-------------------------------begin of OTA Section............................. 
  for(char wt=0;wt<10;wt++){ Serial.print("."); delay(300); }
 // OTA_checkAndUpdateFirmware();
   //-------------------------------end of OTA Section............................. 
  Serial.println("\n\n\r Configuring System...");
  if (!EEPROM.begin(EEPROM_SIZE))
  {
    Serial.println("failed to initialise EEPROM"); delay(1000);
  }
   //EEPROM.write(EEPROM_Address_RestartCounter_Bytes,  0);
  digitalWrite(builtin_LED, Light_OFF);
  dht.setup(dht_dpin);
  
  // I/O Config
  pinMode(Fan_Pin, OUTPUT);
  pinMode(co2_NH4_sensor, INPUT);
  for (byte i = 0; i < 4; i++)
    pinMode(Light[i], OUTPUT);
  for (byte i = 0; i < sizeof(Light); i++)
    digitalWrite(Light[i], Light_OFF);
  //  dht.begin();
  //MQ135 sensor init
  volatile int ADC_value =108; //analogReadAdc(co2_NH4_sensor);
  Serial.println("Init ADC Value:" + String(ADC_value));
  MQ135_init(ADC_resulation,ADC_value);// Again calibrating

  //----------------- SET CURRENT TIME HERE---------------------//
    
   RTC_Init();
  // January 21, 2014 at 3am you would call:
  // rtc.adjust(DateTime(2018, 1, 28, 19, 25, 0));//yyyy,mm,dd,hh,mm,ss
  delay(2000);
  EEPROMRead_AndUpdateVar();
  control_light();
  control_Fan();
  delay(2000);
  //Wifi and MQTT INIT
   setup_wifi();
  client.setServer(mqtt_server, mqttPort);
  client.setCallback(callback);
  Serial.println("\n\n\r System has been configured .!!");
  //'''''''''''''''web time
  configTime(6 * 3600, 0, "bd.pool.ntp.org", "bsti1.time.gov.bd");// 6 for UTC zone code (dhaka UTC-6) 
  Serial.println("\nWaiting for time");
  int prevMilValue=millis();
  while ((!time(nullptr)) && ((millis()-prevMilValue)<120000)) {
    Serial.print(".");
    delay(1000);
    }
    delay(10000);
    if((millis()-prevMilValue)<120000)
     CTimeFromServer();
     //restart notification
    if (!client.connected()) {
    reconnect();
  }
     EEPROM.write(EEPROM_Address_RestartCounter_Bytes,  ++RestartCounter);
     EEPROM.commit();
     String msg="";
     pub_message(mqtt_topic_pub_RestartStatus, msg + "{\"ReStartNo\":" + RestartCounter +",\"H\":" + CHour +",\"M\":"+CMinute+",\"S\":"+CSecond+"}");
     //publishing current firmware version   to MQTT broker
     msg="";
    // pub_message(mqtt_topic_pub_firmwareVersion, msg + "{\"fwVersion\":" + String(EEPROReadCurrentVersion())+"}");
     EEPROMRead_AndUpdateVar();

}
//----------------------------------------Main Loop------------------------------------//


void loop() {
   //watchdogCount=0;
  if (!client.connected()) {
    reconnect();
  }
  client.loop();
  sensor_data_publish();
  control_light();
  control_Fan();
  if (millis()== 3600000)
  {
    ESP.restart();    
  }
}


//-----------------------------WiFi-------------------------------------------//

boolean setup_wifi() {
   if(WiFi.status() != WL_CONNECTED)
  {
  volatile int returnCounter=0;
  backup_control_Mode=control_Mode;
  delay(100);
  // We start by connecting to a WiFi network
  Serial.print("Connecting to ");
  Serial.println(ssid);
  WiFi.begin(ssid, password);
  Serial.println("callled wifi.begin()");
  
  while (WiFi.status() != WL_CONNECTED)
  {
    digitalWrite(builtin_LED, Light_ON);
    delay(500);
    digitalWrite(builtin_LED, Light_OFF);
    delay(500);
    Serial.print(".");
    if(++returnCounter>25)
     {
      Serial.println("Return From Reconnect()");
      control_Mode=1;
      return false;
     }
    if(((millis()-reStartTimeHolder)/1000)>systemRestartThresholdTime)
    {
      Serial.println("Can't connect to MQTT: Restarting with in 1 Sec");
      ESP.restart();
      reStartTimeHolder=millis();
    }
  }
  delay(8000);
  randomSeed(micros());
  Serial.println("");
  Serial.println("WiFi connected");
  control_Mode=backup_control_Mode;
  reStartTimeHolder=millis();
  digitalWrite(builtin_LED, Light_ON);
  Serial.println("IP address: ");
  Serial.println(WiFi.localIP());
  }
}

//---------------------------While client not conncected---------------------------------//

boolean reconnect() {
   Serial.println("..........Reconnect called..............");
  // Loop until we're reconnected
  volatile int returnCounter=0;
  backup_control_Mode=control_Mode;
  while (!client.connected())
  {
    Serial.print("Attempting MQTT connection..");
    //................
    digitalWrite(builtin_LED, Light_ON);
    delay(300);
    digitalWrite(builtin_LED, Light_OFF);
    delay(300);
    Serial.print(".");
    //......................
  if(++returnCounter>=3)
  {
    Serial.println("Return From Reconnect()");
    control_Mode=1;
    return false;
  }
    if(((millis()-reStartTimeHolder)/1000)>systemRestartThresholdTime)
    {
      Serial.println("Can't connect to MQTT: Restarting with in 1 Sec");
      ESP.restart();
      reStartTimeHolder=millis();
    }
    // Create a random client ID
    String clientId = "ESP32-";
    clientId += String(random(0xffff), HEX);
    // Attempt to connect
    //if your MQTT broker has clientID,username and password
    //please change following line to    if (client.connect(clientId,userName,passWord))
    if (client.connect(clientId.c_str(), mqttUser, mqttPassword))
    {
      Serial.println("connected");
      digitalWrite(builtin_LED, Light_ON);
      reStartTimeHolder=millis();
      control_Mode=backup_control_Mode;
      //once connected to MQTT broker, subscribe command if any
      //----------------------Subscribing to required topics-----------------------//
      client.subscribe(mqtt_topic_sub_restart);
      Serial.println("Subsribed to topic:" + String(mqtt_topic_sub_restart));
      client.subscribe(mqtt_topic_sub_date);
      Serial.println("Subsribed to topic:" + String(mqtt_topic_sub_date));
      client.subscribe(mqtt_topic_sub_mode);
      Serial.println("Subsribed to topic:" + String(mqtt_topic_sub_mode));
      client.subscribe(mqtt_topic_sub_light);
      Serial.println("Subsribed to topic:" + String(mqtt_topic_sub_light));
      client.subscribe(mqtt_topic_sub_FanStatus);
      Serial.println("Subsribed to topic:" + String(mqtt_topic_sub_FanStatus));
      client.subscribe(mqtt_topic_sub_Current_date);
      Serial.println("Subsribed to topic:" + String(mqtt_topic_sub_Current_date));
      client.subscribe(mqtt_topic_sub_ReqFromClient);
      Serial.println("Subsribed to topic:" + String(mqtt_topic_sub_ReqFromClient));
    } else {
      Serial.print("failed, rc=");
      Serial.print(client.state());
      Serial.println(" try again in 5 seconds");
      // Wait 6 seconds before retrying
      delay(5000);
    }
  }
} //end reconnect()



//------------------------------Publishing sensor data every 10 secs----------------------------------//
void sensor_data_publish() {
   String msg = "";
  volatile long int  current_time = millis();

  if ((current_time - prev_published_time > data_publishing_interval))
  {
    Serial.println();
     prev_published_time = current_time;
    //1. publishing Temp, Hum, Co2,NH3 ....
    if (readAllData())
    {
//      msg = msg + "{\"Temp\":" +((5*(data1_temp-32))/9)+ ",\"Hum\":" + data2_hum + ",\"Co2\":" + data3_Co2 + ",\"NH3\":" + data4_NH4 + "}";
//      pub_message(mqtt_topic_Pub_data, msg);
    }

     String mac= WiFi.macAddress();
     String ID=mac.substring(9, 17);
     
    if (readAllData())
    {
      msg = "";
//      msg = msg + "{\"Temp\":" +((5*(data1_temp-32))/9)+ ",\"Hum\":" + data2_hum + ",\"Co2\":" + data3_Co2 + ",\"NH3\":" + data4_NH4 + "}";
      msg = msg + ID +","+ data1_temp +","+ data2_hum+","+ data3_Co2+","+ data4_NH4+","+ lightLevel+","+ prev_FanStatus+","+ control_Mode;
      pub_message(mqtt_topic_Pub_data, msg);
    }
    
    //2. publishing elapsed Time ....
    msg = "";
    pub_message(mqtt_topic_Pub_elapsedTime, msg + "{\"ElapsedTime\":" + elapsedTime() + "}");//Current_elapsedTime
    //3. publishing Fan Status
    msg = "";
    pub_message(mqtt_topic_pub_FanStatus, msg + "{\"FanStatus\":" + currentFanStatus + "}");
    // 4. publishing Light Status
    msg = "";
    pub_message(mqtt_topic_Pub_LightStatus, msg + "{\"LightStatus\":" + lightLevel + ",\"Mode\":"+control_Mode+"}");
    Serial.println();
  }
  //publishing elapsed Time in days based on change event
  if (Current_elapsedTime != prev_elapsedTime)
  {
    prev_elapsedTime = Current_elapsedTime;
    msg = "";
    pub_message(mqtt_topic_Pub_elapsedTime, msg + "{\"ElapsedTime\":" + Current_elapsedTime + "}");
  }
  //publishing Light ON/OFF Status  based on change event
  if (lightLevel != prev_lightLevel)
  {
    prev_lightLevel = lightLevel;
    msg = "";
    pub_message(mqtt_topic_Pub_LightStatus, msg + "{\"LightStatus\":" + lightLevel + ",\"Mode\":"+control_Mode+"}");
  }  
  
}
void pub_message(const char*  mqtt_topic_Pub, String msg)
{
  char message[68];
  msg.toCharArray(message, 68);
  //Serial.print(msg+"   ");
  Serial.println("Publishing--->Data: " + msg + "\t topic: " + mqtt_topic_Pub);
  client.publish(mqtt_topic_Pub, message);
  delay(1000);
}
unsigned char readAllData()
{
  volatile unsigned char flag = 0, error_Counter = 0;
  //...................
  while (!flag)
  {
    delay(dht.getMinimumSamplingPeriod());
    data2_hum = dht.getHumidity();
    data1_temp =dht.getTemperature();
    data1_temp=dht.toFahrenheit(data1_temp);
    if (isnan(data1_temp) || isnan(data1_temp))
    {
      Serial.println("Temp and Humidity reading NaN type");
      flag = 0;
    }
    else
      flag = 1;
    if (++error_Counter > 10)
      break;
  }
  //..............
  volatile int ADC_value = analogReadAdc(co2_NH4_sensor);
  Serial.println("ADC Value:" + String(ADC_value));
  data3_Co2 = read_CO2(ADC_resulation,ADC_value);
  data4_NH4 = read_NH4(ADC_resulation,ADC_value);
  return flag;
}


//-----------------------Callback function-------------------------------------//

void callback(char* topic, byte* payload, unsigned int length) {

  Serial.print("Message arrived in topic: ");
  Serial.println(topic);
  //------------------------user_input for manual load control-----------------//
  // mqtt_topic_sub_restart mqtt_topic_sub_date  mqtt_topic_sub_mode  mqtt_topic_sub_light
  //1................................receiving client startUp req.... 
   if (strcmp(topic, mqtt_topic_sub_ReqFromClient) == 0)
  {
    Serial.println();
    Serial.print("Message Client Req:");
    for (int i = 0; i < length; i++)
    {
      Serial.print((char)payload[i]);
    }
    Serial.println();
    //------------------------JSON...............
    StaticJsonBuffer<200> jsonBuffer;
    JsonObject& root = jsonBuffer.parseObject(payload);
    if (root.success())
    {
      if (root["ClientReq"] == 1) //{"ClientReq":1}
      prev_published_time=0;
      sensor_data_publish();
    }
  }
  //2. ..........................restart topic checking.................................................
  if (strcmp(topic, mqtt_topic_sub_restart) == 0)
  {
    Serial.println();
    Serial.print("Message date:");
    for (int i = 0; i < length; i++)
    {
      Serial.print((char)payload[i]);
    }
    Serial.println();
    //------------------------JSON...............
    StaticJsonBuffer<200> jsonBuffer;
    JsonObject& root = jsonBuffer.parseObject(payload);
    if (root.success())
    {
      if (root["Restart"] == 1) //{"Restart":1}
      {
        Serial.println("Restarting in 2 seconds");
        delay(1500);
        ESP.restart();
      }

    }
  }
  //checking topic date
  if (strcmp(topic, mqtt_topic_sub_date) == 0)
  {
    Serial.println();
    Serial.print("Message date:");
    for (int i = 0; i < length; i++)
    {
      Serial.print((char)payload[i]);
    }
    Serial.println();
    //------------------------JSON...............
    StaticJsonBuffer<200> jsonBuffer;
    JsonObject& root = jsonBuffer.parseObject(payload);
    if (root.success())
    {
      EEPROMwrite_setDate(root["Day"], root["Month"], root["Year"]);
    }
  }
  //checking topic adjust date
    if (strcmp(topic, mqtt_topic_sub_Current_date) == 0)
  {
    Serial.println();
    Serial.print("Message Current Date Time:");
    for (int i = 0; i < length; i++)
    {
      Serial.print((char)payload[i]);
    }
    Serial.println();
    //------------------------JSON...............
    StaticJsonBuffer<200> jsonBuffer;
    JsonObject& root = jsonBuffer.parseObject(payload);
    if (root.success())
    {
      rtc.adjust(DateTime(root["Year"], root["Month"], root["Day"], root["Hour"], root["Min"], 0));
      elapsedTime();
    }
  }
  //checking topic mode
  if (strcmp(topic, mqtt_topic_sub_mode) == 0)
  {
    Serial.println();
    Serial.print("Message Mode:");
    for (int i = 0; i < length; i++)
    {
      Serial.print((char)payload[i]);
    }
    Serial.println();
    //------------------------JSON...............
    StaticJsonBuffer<200> jsonBuffer;
    JsonObject& root = jsonBuffer.parseObject(payload);
    if (root.success())
    {
      EEPROMwrite_Mode(root["Mode"]);
    }
  }
  //topic checking manual light
  if (strcmp(topic, mqtt_topic_sub_light) == 0)
  {
    Serial.println();
    Serial.print("Message LightLevel:");
    for (int i = 0; i < length; i++)
    {
      Serial.print((char)payload[i]);
    }
    Serial.println();
    //------------------------JSON...............
    StaticJsonBuffer<200> jsonBuffer;
    JsonObject& root = jsonBuffer.parseObject(payload);
    if (root.success())
    {
        lightLevel=root["LightLevel"];
       Serial.println("this light level: "+String(lightLevel));
      EEPROMwrite_LightLevel(lightLevel);
    }
  }
  //Topic checking manual fan on/off
   //const char*  mqtt_topic_sub_FanStatus = "turkey/ComFanStatus";//{"FanStatus":FanStatus}
   if (strcmp(topic, mqtt_topic_sub_FanStatus) == 0)
  {
    Serial.println();
    Serial.print("Message Fan:");
    for (int i = 0; i < length; i++)
    {
      Serial.print((char)payload[i]);
    }
    Serial.println();
    //------------------------JSON...............
    StaticJsonBuffer<200> jsonBuffer;
    JsonObject& root = jsonBuffer.parseObject(payload);
    if (root.success())
    {
      currentFanStatus=root["FanStatus"];
      EEPROMwrite_Fan(currentFanStatus);
    }
  }
  //...........
}
// end of callback()

//-------------------------------------------------------Light Control--------------------------------------------------------
void control_light()
{
  if (control_Mode == 1) // 1 means auto mode
  {
    light_autoMode();
  }
  if (control_Mode == 0) // 0 means manual mode
  {
    light_manualMode();
  }
}
void  light_autoMode()
{
  //Serial.println("Auto mode running");
  volatile long int currentTime = millis();
  if ((currentTime - prev_lightLevel_Time) > TimeOut_lightLevel)
  {
    prev_lightLevel_Time = currentTime;
    if (data1_temp > thresholdTemp_Upper_Limit)
    {
      if ((data1_temp - thresholdTemp_Upper_Limit) > 2)
        if (lightLevel >= 2)
        lightLevel = lightLevel - 2;
        if (lightLevel >0)
        lightLevel--;
      if (lightLevel < 0)
        lightLevel = 0;
    }
    if (data1_temp < thresholdTemp_Lower_Limit)
    {
      if ((thresholdTemp_Lower_Limit - data1_temp) > 2)
        lightLevel = lightLevel + 2;
       lightLevel++;
      if (lightLevel > 15)
        lightLevel = 15;
    }
    LightOnOFF();
  }
}
void  LightOnOFF()
{
  for (byte i = 0; i < 4; i++) //selecting light No from 0 to 3
  {
    if (lightLevel & (1 << i))
      digitalWrite(Light[i], Light_ON);
    else
      digitalWrite(Light[i], Light_OFF);
  }
}
void light_manualMode()
{
  // Serial.println("Manual mode running");
  for (byte i = 0; i < 4; i++) //selecting light No from 0 to 3
  {
    if (lightLevel & (1 << i))
      digitalWrite(Light[i], Light_ON);
    else
      digitalWrite(Light[i], Light_OFF);
  }
}
//---------------------------------------------Fan Control.........................
void control_Fan()
{
  if((CMinute>=58) && (CMinute<=59))
  {
    if(fanChangeFlag)
     backupFanStatus=currentFanStatus;
     currentFanStatus = 1;
     fanChangeFlag=false;
  }
  else
  {
    fanChangeFlag=true;
    currentFanStatus =backupFanStatus;
  if (control_Mode == 1) // 1 means auto mode
  {

  if (/*(data3_Co2 > threshold_Level_Co2)||*/(data1_temp > thresholdTemp_Upper_Limit))
    currentFanStatus = 1;
  if (/*data3_Co2 < threshold_Level_Co2 ||*/ (data1_temp < thresholdTemp_Lower_Limit))
    currentFanStatus = 0;
   if((data1_temp<thresholdTemp_Upper_Limit) && (data1_temp>thresholdTemp_Lower_Limit))
   {
    currentFanStatus = 0;
   }
    
    
  }
  }
   if(currentFanStatus==1)
    digitalWrite(Fan_Pin, Fan_ON);
    else
    digitalWrite(Fan_Pin, Fan_OFF);
  if (prev_FanStatus != currentFanStatus)
  {
    prev_FanStatus = currentFanStatus;
   
    String msg = "";
    pub_message(mqtt_topic_pub_FanStatus, msg + "{\"FanStatus\":" + currentFanStatus + "}");
  }
  
}
//....................................................RTC....................................................
void RTC_Init()
{
  if (! rtc.begin()) {
    Serial.println("Couldn't find RTC");
    // while (1);
  }

  if (rtc.lostPower()) {
    Serial.println("RTC lost power, lets set the time!");
    // following line sets the RTC to the date & time this sketch was compiled
    //rtc.adjust(DateTime(F(__DATE__), F(__TIME__)));
    // This line sets the RTC with an explicit date & time, for example to set
    // January 21, 2014 at 3am you would call:
     //rtc.adjust(DateTime(2014, 1, 21, 3, 0, 0));
     
  }
  // rtc.adjust(DateTime(2018, 2, 28, 1, 22, 0));
}
// ...................read current time from time server NTP ........................
void CTimeFromServer()
{
  time_t now = time(nullptr);
  //Serial.println(ctime(&now));
  struct tm *ptm = localtime(&now);
  CYear=ptm->tm_year;
  CYear+=1900;// as count since 1900
  CMonth=ptm->tm_mon;
  CMonth+=1;// as count from 0 to 11
  CDay=ptm->tm_mday;
  CHour=ptm->tm_hour;
  CMinute=ptm->tm_min;
  CSecond=ptm->tm_sec;
  if(CYear!=1970)
  rtc.adjust(DateTime(CYear, CMonth, CDay, CHour, CMinute,CSecond ));
  Serial.println("Server's Current Date Time: "+String(CDay)+"/"+String(CMonth)+"/"+String(CYear)+"   "+String(CHour)+":"+String(CMinute)+":"+String(CSecond));
}
//........................
time_t timeFromDate( int year, int month, int day ) {
  time_t rawtime;
  struct tm * my_time;

  // Create a filled in time structure
  time( &rawtime );
  my_time = localtime( &rawtime );

  // Reassign our date
  my_time->tm_year     = year - 1900; // Different sources say 1900 and 1970?
  my_time->tm_mon        = month - 1;    // tm uses uses january + months [0..11]
  my_time->tm_mday     = day;

  // Return it as seconds since epoch
  return ( mktime( my_time ) );
}

long int elapsedTime() {
 int DateReadingInitTime=millis();
  volatile int days = 0;
   while(1)
    {
     DateTime now = rtc.now();
  CYear =now.year();
  CMonth = now.month();
  CDay = now.day();
  CHour=now.hour();
  CMinute=now.minute();
  CSecond=now.second();
  if(!((CMonth<=12) && (CDay<=31)))
  {
     clockErrorCounter++;
     CTimeFromServer();
  }
  Serial.println("System's Current Date Time: "+String(CDay)+"/"+String(CMonth)+"/"+String(CYear)+"   "+String(CHour)+":"+String(CMinute)+":"+String(CSecond));
  //5 publishing all data
     if((CMinute%1==0) && (CSecond<=50))
     {
       String msg;
       msg=""; 
       pub_message(mqtt_topic_Pub_CurrentDateTime,msg+"{\"y\":"+CYear+",\"m\":"+CMonth+",\"d\":"+CDay+",\"H\":"+CHour+",\"M\":"+CMinute+",\"S\":"+CSecond+"}");
       Serial.println();
     }
      if(millis()-DateReadingInitTime>(60000))//1 minute
        break;
       if(clockErrorCounter>=30)
      {
        Serial.println("Clock Reading Error. That's why Restarting Device...");
        ESP.restart();
      }
      if((CMonth<=12) && (CDay<=31))
      {
         clockErrorCounter=0;
         break;  
      } 
         delay(5000);   
    }
  //.......publishing end
 
  time_t start_date, end_date;
  start_date     = timeFromDate( startYear, startMonth, startDay );
  end_date       = timeFromDate( CYear, CMonth, CDay );

  days = difftime( end_date, start_date) / SECONDS_PER_DAY;
  Current_elapsedTime = days;

  return days;
}

void updateThresholdTemp()
{
  volatile byte week=(Current_elapsedTime / 7);
  if (week == 0) //1st week
  {
    thresholdTemp_Lower_Limit = 95;
    thresholdTemp_Upper_Limit = 98;
  }
  if (week == 1) //2nd week
  {
    thresholdTemp_Lower_Limit = 90;
    thresholdTemp_Upper_Limit = 93;
  }
  if (week == 2) //3rd week
  {
    thresholdTemp_Lower_Limit = 85;
    thresholdTemp_Upper_Limit = 88;
  }
  if (week == 3) //4th week
  {
    thresholdTemp_Lower_Limit = 80;
    thresholdTemp_Upper_Limit = 83;
  }
   if (week == 4) //5th week
  {
    thresholdTemp_Lower_Limit = 75;
    thresholdTemp_Upper_Limit = 78;
 
  }
   if (week>4 && control_Mode == 1) //5th week
  {
    thresholdTemp_Lower_Limit = 0;
    thresholdTemp_Upper_Limit = 0;
       lightLevel=0;
    
  }
  
}
//EEPROM
void EEPROMwrite_setDate(int d, int m, int y)
{
  EEPROM.write(EEPROM_Address_startYear_Bytes_L, lowByte(y));  //write the first half bytes of y
  EEPROM.write(EEPROM_Address_startYear_Bytes_H, highByte(y)); //write the second half
  EEPROM.write(EEPROM_Address_startMonth_Bytes, m);
  EEPROM.write(EEPROM_Address_startDay_Bytes, d);
  EEPROMRead_AndUpdateVar();
}
void EEPROMwrite_Mode(byte Mode)
{
  EEPROM.write(EEPROM_Address_Mode_Bytes, Mode);
  EEPROMRead_AndUpdateVar();
}
void EEPROMwrite_LightLevel(byte LightLevel)
{
  EEPROM.write(EEPROM_Address_LightLevel_Bytes, LightLevel);
  EEPROMRead_AndUpdateVar();
}
void EEPROMwrite_Fan(byte currentFanStatus)
{
  backupFanStatus=currentFanStatus;
  EEPROM.write(EEPROM_Address_FanStatus_Bytes, currentFanStatus);
  EEPROMRead_AndUpdateVar();
}

void EEPROMRead_AndUpdateVar()
{
  EEPROM.commit();
  startYear = byte(EEPROM.read(EEPROM_Address_startYear_Bytes_L));
  startYear |= byte(EEPROM.read(EEPROM_Address_startYear_Bytes_H)) << 8;
  startMonth = byte(EEPROM.read(EEPROM_Address_startMonth_Bytes));
  startDay = byte(EEPROM.read(EEPROM_Address_startDay_Bytes));
  control_Mode = byte(EEPROM.read(EEPROM_Address_Mode_Bytes));
  lightLevel = EEPROM.read(EEPROM_Address_LightLevel_Bytes);
  currentFanStatus = EEPROM.read(EEPROM_Address_FanStatus_Bytes); 
  RestartCounter= EEPROM.read(EEPROM_Address_RestartCounter_Bytes);  
  Serial.println("saved y:" + String(startYear) + "m:" + String(startMonth) + "d:" + String(startDay));
  Current_elapsedTime = elapsedTime();
 String msg="";
  pub_message(mqtt_topic_Pub_BatchStartDate,msg+"{\"SY\":"+startYear+",\"SM\":"+startMonth+",\"SD\":"+startDay+"}");
  updateThresholdTemp();
  light_manualMode();
  
}
int analogReadAdc(unsigned char ADC_Pin)
{
  volatile unsigned int ADC_val_SUM_AVG = 0;
  volatile char i = 0;
  for ( i = 0; i < 20; i++)
  {
    ADC_val_SUM_AVG += analogRead(ADC_Pin);
    delay(100);
  }
  ADC_val_SUM_AVG /= i;
  String msg = "";
  pub_message(mqtt_topic_Pub_ADC, msg + "{\"ADC_Val\":" + ADC_val_SUM_AVG + "}");
  return ADC_val_SUM_AVG;
}
