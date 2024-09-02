#include <WiFi.h>
#include <WiFiClient.h>
#include <WebServer.h>
#include <DNSServer.h>
#include <Preferences.h>
#include "SerialDebug.h"
#include <regex.h>
#include <stdio.h>
#include "config.h"

struct ErrorMsgs{
  String wifi_psk="";
  String lora_dev_eui="";
  String lora_app_eui="";
  String lora_app_key="";
  String sleep_m="";
  String sml_obis_ids="";
  String sml_pin="";
};


String wifi_ssid ;
String wifi_psk ;

DNSServer dnsServer;
WebServer server(80);
Preferences prefs;
const byte DNS_PORT = 53;
IPAddress apIp(192, 168, 4, 1);
unsigned long startup;
boolean validConfig=false;
const int led = 13;
const unsigned long CONFIG_DUR=25000;
const uint16_t DEFAULT_TIME_TO_SLEEP_M=30;

bool configValidator(ErrorMsgs* e=NULL){
  bool retval=true;
  ErrorMsgs dummy;
  ErrorMsgs* em;
  if(e!=NULL){
    em=e;
  }else{
    em=&dummy;  
  }
  
  //lora
  String ldeveui= prefs.getString("lora_dev_eui", "");
  if(!isHexString(ldeveui,8)){
      em->lora_dev_eui = "Bitte als Hex-String xx xx xx... angeben";
      retval=false;
  }
  String lappeui=prefs.getString("lora_app_eui", "");
  if(!isHexString(lappeui,8)){
      em->lora_app_eui = "Bitte als Hex-String xx xx xx... angeben";
      retval=false;
  }
  String lappkey=prefs.getString("lora_app_key", "");
  if(!isHexString(lappkey,16)){
      em->lora_app_key = "Bitte als Hex-String xx xx xx... angeben";
      retval=false;
  }

  //OBIS
  String stype= "sml";
  String opt1= "";

  String lobis=prefs.getString("sml_obis_ids", "");
  if( !formObisToList(lobis, NULL)){
        em->sml_obis_ids = "Zu lesende OBIS-Ids durch Leerzeichen getrennt eingeben!";
        retval=false;  
  }
    


  //Sending interval
  String linterval=prefs.getString("sleep_m", ""+DEFAULT_TIME_TO_SLEEP_M);
  if(!isNumeric(linterval) || linterval.toInt()<5 ){
      em->sleep_m= "Sendeintervall in  (min. 5) angeben";
      retval=false;
  }

  //datapins
  String lpin=prefs.getString("sml_pin", "");
  if(!isNumeric(lpin)){
      em->sml_pin = "Receive-Pin d. Lesekopfes eingeben ";
      retval=false;
  }

  return retval; 
  
    

  
}



String genFormField(String name, String lbl, String val, String msg=""){
  return "<tr><td>"+lbl+":</td><td><input type=\"text\" name=\""+name+"\" value=\""+val+"\"></td><td>"+msg+"</td></tr>";
}

String genRootPage(ErrorMsgs* em){
  String page="<html><head></head><body>";
  page+= "<h1>sml2lora configuration</h1>";
  page+= "<form method=\"post\" action=\"//"+apIp.toString()+"/save\"><table>";
  page+= genFormField("wifi_psk", "wifi passphrase", get_psk(), em->wifi_psk);
  page+= genFormField("lora_dev_eui", "lora dev-EUI:<br>8 Byte, little-endian", prefs.getString("lora_dev_eui", ""), em->lora_dev_eui);
  page+= genFormField("lora_app_eui", "lora app-EUI:<br>8 Byte, little-endian", prefs.getString("lora_app_eui", ""), em->lora_app_eui);
  page+= genFormField("lora_app_key", "lora app-key:<br>16 Byte, big-endian", prefs.getString("lora_app_key", ""), em->lora_app_key);
  page+= genFormField("sleep_m","readings every (minutes)", prefs.getString("sleep_m", ""+DEFAULT_TIME_TO_SLEEP_M), em->sleep_m);
  page+= genFormField("sml_obis_ids","OBIS-Ids <br> (space-separated)", prefs.getString("sml_obis_ids", ""), em->sml_obis_ids);
  page+= genFormField("sml_pin","pin to sml-reading-head", prefs.getString("sml_pin", ""), em->sml_pin);
  page+= "<tr><td colspan=3><button type=\"submit\">Speichern</button></td></tr>";
  page+= "</table></form></body></html>";
  return page;
}

void handleRoot() {
  printD("HANDLE: root");
  ErrorMsgs e;
  server.send(200, "text/html", genRootPage(&e));
}

void handleSave(){
  printD("HANDLE: Saving config");
  debugD("args:%s", server.argName(0).c_str());
  debugD("new PSK:%s", server.arg("wifi_psk").c_str());
  
  ErrorMsgs e;
  //handle wifi psk separately as wifi begin wont accept less than 7char psk and using such a psk would make the esp unreachable
  if(server.arg("wifi_psk").length()>7){
    prefs.putString("wifi_psk", server.arg("wifi_psk"));
  }else{
    e.wifi_psk="unchanged, use more than 7 chars";  
  } 
  prefs.putString("lora_dev_eui", server.arg("lora_dev_eui"));
  prefs.putString("lora_app_eui", server.arg("lora_app_eui"));
  prefs.putString("lora_app_key", server.arg("lora_app_key"));
  prefs.putString("sleep_m", server.arg("sleep_m"));
  prefs.putString("sml_obis_ids", server.arg("sml_obis_ids"));
  prefs.putString("sml_pin", server.arg("sml_pin"));
  
  configValidator(&e);
  
  server.send(200, "text/html", genRootPage(&e));
}

String get_ssid(){
  uint8_t baseMac[6];
  String ssid="sml2lora-";
  String mac=WiFi.macAddress();
  return  ssid+mac.substring(12);
}
String get_psk(){
  return prefs.getString("wifi_psk", "sml-to-lora");
}

void doSleep(){
    uint16_t tts=prefs.getUInt("sleep_s", DEFAULT_TIME_TO_SLEEP_M);
    esp_sleep_enable_timer_wakeup( tts * 60 * 1000000ULL);
    debugD("Setup ESP32 to sleep for every %d Seconds. Going to sleep now.", tts);
    esp_deep_sleep_start();
}

void doLora(){

   doSleep();
}

void setup(void) {
  Serial.begin(115200);
  pinMode(led, OUTPUT);
  digitalWrite(led,LOW);
  startup = millis();
  validConfig=configValidator();
  debugSetLevel(DEBUG_LEVEL_VERBOSE);
  delay(200);
  
  printlnD("startup");

  
  prefs.begin("sml2lora");


  if(esp_sleep_get_wakeup_cause()==ESP_SLEEP_WAKEUP_TIMER){
    //printD("woke up from sleep, doin stuff!");
    //DO LORA STUFF
    doLora();
    
    
  }else{


  //Wifi-AP stuff
  wifi_psk=get_psk();
  wifi_ssid=get_ssid();

  debugD("SSID: %s",wifi_ssid.c_str());
  debugD("PASS: %s",wifi_psk.c_str());
  
  

  WiFi.mode(WIFI_AP);
  WiFi.softAPConfig(apIp, apIp, IPAddress(255, 255, 255, 0));
  if (!WiFi.softAP(wifi_ssid, wifi_psk)) {
    printlnD("Soft AP creation failed.");
    while(1);
  }
  
  
  debugD("AP @  %s", wifi_ssid.c_str());
  debugD("IP @  %s", apIp.toString().c_str());
  
  dnsServer.start(DNS_PORT, "*", apIp);

  server.on("/", handleRoot);
  server.on("/save", HTTP_POST, handleSave);
  server.onNotFound(handleRoot);
  server.begin();
  } 
}

void loop(void) {
  debugHandle();
  delay(2);//allow the cpu to switch to other tasks

  if(esp_sleep_get_wakeup_cause()==ESP_SLEEP_WAKEUP_TIMER){
    //printD("woke up from sleep, doin stuff!");
    //DO LORA STUFF
    //loraloopstuff!();
    
    
  }else{
      //printD("fresh boot");
      
      if(millis()-startup<=CONFIG_DUR || WiFi.softAPgetStationNum()>0 || !validConfig){
    
        dnsServer.processNextRequest();
        server.handleClient();

      }else{
          printlnI("Time for config is over, shutting down AP");
          digitalWrite(led,HIGH);
          WiFi.softAPdisconnect(true);
          doLora();
      }
  }

  

  
  
}
