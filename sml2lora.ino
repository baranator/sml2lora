#include <WiFi.h>
#include <WiFiClient.h>
#include <WebServer.h>
#include <DNSServer.h>
#include <Preferences.h>
#include "SerialDebug.h"


String wifi_ssid ;
String wifi_psk ;

DNSServer dnsServer;
WebServer server(80);
Preferences prefs;
const byte DNS_PORT = 53;
IPAddress apIp(192, 168, 4, 1);

const int led = 13;

void handleRoot() {
  printD("HANDLE: root");
  String page="<html><head></head><body>";
  page+= "<h1>sml2lora configuration</h1>";
  page+= "<form method=\"post\" action=\"//"+apIp.toString()+"/save\">";
  page+= "<div>wifi psk: <input type=\"text\" name=\"wifi_psk\" value=\""+get_psk()+"\"></div>";
  page+= "<button type=\"submit\">Speichern</button>";
  page+= "</form></body></html>";
  server.send(200, "text/html", page);
}

void handleSave(){
  printD("HANDLE: Saving config");
  debugD("args:%s", server.argName(0).c_str());
  debugD("new PSK:%s", server.arg("wifi_psk").c_str());
    
}

String get_ssid(){
  uint8_t baseMac[6];
  String ssid="sml2lora-";
  String mac=WiFi.macAddress();
  return  ssid+mac.substring(12);
}
String get_psk(){
  return   prefs.getString("wifi_psk", "sml-to-lora");
}

void setup(void) {
  Serial.begin(115200);
  debugSetLevel(DEBUG_LEVEL_VERBOSE);
  delay(200);
  
  printlnD("startup");
  prefs.begin("sml2lora");

  //Wifi-AP stuff
  wifi_psk=get_psk();
  wifi_ssid=get_ssid();

  debugD("SSID: %s",wifi_ssid.c_str());
  debugD("PASS: %s",wifi_psk.c_str());
  
  pinMode(led, OUTPUT);

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

void loop(void) {
  debugHandle();
  dnsServer.processNextRequest();
  server.handleClient();
  delay(2);//allow the cpu to switch to other tasks
}
