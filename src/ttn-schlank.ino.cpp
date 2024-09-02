#include <WiFi.h>
#include <WiFiClient.h>
#include <WebServer.h>
#include <DNSServer.h>
#include <arduino_lmic_hal_boards.h>
#include <Preferences.h>
#include "SerialDebug.h"

#include <lmic.h>
#include <hal/hal.h>
#include <SPI.h>
#include <sml/sml_file.h>

#include <regex.h>
#include <stdio.h>
#include "config.h"
#include "arduino_lmic_hal_configuration.h"

struct ErrorMsgs{
  String wifi_psk="";
  String lora_dev_eui="";
  String lora_app_eui="";
  String lora_app_key="";
  String sleep_m="";
  String sml_obis_ids="";
  String sml_pin="";
};

static uint8_t* loraSendBuffer;
static uint16_t loraSendBufferSize;
std::list<String> obisIds;
Sensor * smlSensor;
unsigned long tryingSince;

static osjob_t sendjob;

const Arduino_LMIC::HalConfiguration_t myConfig;
const lmic_pinmap *pPinMap = Arduino_LMIC::GetPinmap_ThisBoard();

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

//static const u1_t PROGMEM APPEUI[8]={ 0x77, 0x6D, 0x54, 0xDF, 0x56, 0x54, 0x54, 0x56 };
//void os_getArtEui (u1_t* buf) { memcpy_P(buf, APPEUI, 8);}
void os_getArtEui (u1_t* buf) { 
  u1_t APPEUI[8];
  String s=prefs.getString("lora_app_eui", "");
  s.toLowerCase();
  formStringToByteArray(s.c_str(), APPEUI, 8);
  memcpy(buf, APPEUI, 8);
}


// This should also be in little endian format, see above.
//static const u1_t PROGMEM DEVEUI[8]={ 0x34, 0x2F, 0x06, 0xD0, 0x7E, 0xD5, 0xB3, 0x70 };
//void os_getDevEui (u1_t* buf) { memcpy_P(buf, DEVEUI, 8);}
void os_getDevEui (u1_t* buf) { 
  u1_t DEVEUI[8];
  String s=prefs.getString("lora_dev_eui", "");
  s.toLowerCase();
  formStringToByteArray(s.c_str(), DEVEUI, 8);
  memcpy(buf, DEVEUI, 8);
}

// This key should be in big endian format (or, since it is not really a
// number but a block of memory, endianness does not really apply). In
// practice, a key taken from ttnctl can be copied as-is.
//static const u1_t PROGMEM APPKEY[16] = { 0x85, 0x38, 0x35, 0x03, 0x15, 0x10, 0x11, 0x3E, 0x64, 0x12, 0xD9, 0xDD, 0xA9, 0xA1, 0x48, 0xFF };
//void os_getDevKey (u1_t* buf) {  memcpy_P(buf, APPKEY, 16);}

void os_getDevKey (u1_t* buf) {  
  u1_t APPKEY[16];
  String s=prefs.getString("lora_app_key", "");
  s.toLowerCase();
  formStringToByteArray(s.c_str(), APPKEY, 16);
  memcpy(buf, APPKEY, 16);
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

void fillbufferwithu32(uint32_t v, uint8_t start){
    start+=1; //reserve one byte at the beginning for indication of given values as a mask
    loraSendBuffer[start+0] = v & 0xFF; // 0x78
    loraSendBuffer[start+1] = (v >> 8) & 0xFF; // 0x56
    loraSendBuffer[start+2] = (v >> 16) & 0xFF; // 0x34
    loraSendBuffer[start+3] = (v >> 24) & 0xFF; // 0x12

    debugD("Filled sendbuffer: %x %x %x %x",loraSendBuffer[start+0],loraSendBuffer[start+1],loraSendBuffer[start+2],loraSendBuffer[start+3]);
}

void do_send(osjob_t* j){
    // Check if there is not a current TX/RX job running
    if (LMIC.opmode & OP_TXRXPEND) {
        printlnD(F("OP_TXRXPEND, not sending"));
    } else {
        // Prepare upstream data transmission at the next possible time.
        LMIC_setTxData2(1, loraSendBuffer, loraSendBufferSize, 0);
        printlnD(F("Packet queued"));
    }
    // Next TX is scheduled after TX_COMPLETE event.
}


void doSleep(){
    uint16_t tts=prefs.getUInt("sleep_m", DEFAULT_TIME_TO_SLEEP_M);
    esp_sleep_enable_timer_wakeup( tts * 60 * 1000000ULL);
    debugD("Setup ESP32 to sleep for every %d Seconds. Going to sleep now.", tts);
    esp_deep_sleep_start();
}

void doLora(){

   doSleep();
}


boolean publish(Sensor *sensor, sml_file *file){
    for (int i = 0; i < file->messages_len; i++){
        sml_message *message = file->messages[i];
        if (*message->message_body->tag == SML_MESSAGE_GET_LIST_RESPONSE){
            sml_list *entry;
            sml_get_list_response *body;
            body = (sml_get_list_response *)message->message_body->data;
            for (entry = body->val_list; entry != NULL; entry = entry->next){
                if (!entry->value){ // do not crash on null value
                    continue;
                }

                char obisIdentifier[32];
                uint32_t intval=0;
                sprintf(obisIdentifier, "%d-%d:%d.%d.%d/%d",
                    entry->obj_name->str[0], entry->obj_name->str[1],
                    entry->obj_name->str[2], entry->obj_name->str[3],
                    entry->obj_name->str[4], entry->obj_name->str[5]);

               // String obisIds[]={"1-0:1.8.0","1-0:2.8.0"};

                
                

                int o=0;
                  for (std::list<String>::iterator it = obisIds.begin(); it != obisIds.end(); ++it, o++){
                    debugD("%s arrived " ,obisIdentifier);
                    debugD("checking for match with %s", it->c_str());
                    if(String(obisIdentifier).startsWith( (*it))){
                        debugD("OBIS-Id: %s",obisIdentifier);

                        if (((entry->value->type & SML_TYPE_FIELD) == SML_TYPE_INTEGER) ||
                        ((entry->value->type & SML_TYPE_FIELD) == SML_TYPE_UNSIGNED)){
                            double value = sml_value_to_double(entry->value);
                            int scaler = (entry->scaler) ? *entry->scaler : 0;
                            int prec = -scaler;
                            if (prec < 0)
                                prec = 0;
                            value = value * pow(10, scaler);
            
                            //ignore sign and store as 32bit-int with factor 10
                            // this allows vals from 0 to 429496729,5 to be stored
                            intval=abs(round(value*10));
                            fillbufferwithu32(intval,o*4);  //4bytes for uint32t
                            loraSendBuffer[0]= loraSendBuffer[0] | (uint8_t)(1 << o);
                            

                        }else if (false && !sensor->config->numeric_only){    //ignore for now
                            if (entry->value->type == SML_TYPE_OCTET_STRING){
                                char *value;
                                sml_value_to_strhex(entry->value, &value, true);
                                //publish(entryTopic + "value", value);
                                free(value);
                            }else if (entry->value->type == SML_TYPE_BOOLEAN){
                                //publish(entryTopic + "value", entry->value->data.boolean ? "true" : "false");
                            }
                        }   
                    }
                }
            }
            
        }
    }  
    //only send if at least one wanted sml-value could be obtained
    if(loraSendBuffer[0] > 0){
      do_send(&sendjob);
      return true;
    }
    return false;
}

boolean process_message(byte *buffer, size_t len, Sensor *sensor){
  // Parse
  sml_file *file = sml_file_parse(buffer + 8, len - 16);
  boolean ret = publish(sensor, file);
  // free the malloc'd memory
  sml_file_free(file);
  return ret;
}

void onEvent (ev_t ev) {
    printD(os_getTime());
    printD(": ");
    switch(ev) {
        case EV_SCAN_TIMEOUT:
            printlnD(F("EV_SCAN_TIMEOUT"));
            break;
        case EV_BEACON_FOUND:
            printlnD(F("EV_BEACON_FOUND"));
            break;
        case EV_BEACON_MISSED:
            printlnD(F("EV_BEACON_MISSED"));
            break;
        case EV_BEACON_TRACKED:
            printlnD(F("EV_BEACON_TRACKED"));
            break;
        case EV_JOINING:
            printlnD(F("EV_JOINING"));
            break;
        case EV_JOINED:
            printlnD(F("EV_JOINED"));
            {
              u4_t netid = 0;
              devaddr_t devaddr = 0;
              u1_t nwkKey[16];
              u1_t artKey[16];
              LMIC_getSessionKeys(&netid, &devaddr, nwkKey, artKey);
              debugD("netid: %d", netid);
              debugD("devaddr: %d ",devaddr);
              printD("AppSKey: ");
              for (size_t i=0; i<sizeof(artKey); ++i) {
                if (i != 0)
                  printD("-");
                printHex2(artKey[i]);
              }
              printD("NwkSKey: ");
              for (size_t i=0; i<sizeof(nwkKey); ++i) {
                      if (i != 0)
                              printD("-");
                      printHex2(nwkKey[i]);
              }
            }
            // Disable link check validation (automatically enabled
            // during join, but because slow data rates change max TX
      // size, we don't use it in this example.
            LMIC_setLinkCheckMode(0);
            //TODO ONLY TESTING, REMOVE SEND LATER
            //do_send(&sendjob);
            break;
        /*
        || This event is defined but not used in the code. No
        || point in wasting codespace on it.
        ||
        || case EV_RFU1:
        ||     DEBUG(F("EV_RFU1"));
        ||     break;
        */
        case EV_JOIN_FAILED:
            printlnD(F("EV_JOIN_FAILED"));
            break;
        case EV_REJOIN_FAILED:
            printlnD(F("EV_REJOIN_FAILED"));
            break;
        case EV_TXCOMPLETE:
            printlnD(F("EV_TXCOMPLETE (includes waiting for RX windows)"));
            if (LMIC.txrxFlags & TXRX_ACK)
              printlnD(F("Received ack"));
            if (LMIC.dataLen) {
              debugD("Received %d bytes of payload",LMIC.dataLen);
            }
            // Schedule next transmission

              
            doSleep();

            break;
        case EV_LOST_TSYNC:
            printlnD(F("EV_LOST_TSYNC"));
            break;
        case EV_RESET:
            printlnD(F("EV_RESET"));
            break;
        case EV_RXCOMPLETE:
            // data received in ping slot
            printlnD(F("EV_RXCOMPLETE"));
            break;
        case EV_LINK_DEAD:
            printlnD(F("EV_LINK_DEAD"));
            break;
        case EV_LINK_ALIVE:
            printlnD(F("EV_LINK_ALIVE"));
            break;
        /*
        || This event is defined but not used in the code. No
        || point in wasting codespace on it.
        ||
        || case EV_SCAN_FOUND:
        ||    DEBUG(F("EV_SCAN_FOUND"));
        ||    break;
        */
        case EV_TXSTART:
            printlnD(F("EV_TXSTART"));
            break;
        case EV_TXCANCELED:
            printlnD(F("EV_TXCANCELED"));
            break;
        case EV_RXSTART:
            /* do not print anything -- it wrecks timing */
            break;
        case EV_JOIN_TXCOMPLETE:
            printlnD(F("EV_JOIN_TXCOMPLETE: no JoinAccept"));
            break;

        default:
            debugD("Unknown event: %d ", (unsigned) ev);
            break;
    }
}

void smlAndLoraSetup(){


  static const SensorConfig* config = new SensorConfig{
    .pin = (uint8_t)atoi(prefs.getString("sml_pin", "").c_str()),
    .numeric_only = false
  };

  formObisToList(prefs.getString("sml_obis_ids", ""), &obisIds);
  loraSendBufferSize=obisIds.size()*4+1;
  loraSendBuffer=(uint8_t*)malloc(loraSendBufferSize); 
  loraSendBuffer[0]=0;

  smlSensor = new Sensor(config, process_message);

  printlnD(F("Sensor setup done.Starting"));
  os_init_ex(pPinMap);
  
 // LMIC.dn2Dr = DR_SF12;
 // LMIC_setDrTxpow(DR_SF12,14);
  LMIC_reset();
  tryingSince=millis();
}




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
  printlnD("HANDLE: root");
  ErrorMsgs e;
  server.send(200, "text/html", genRootPage(&e));
}

void handleSave(){
  printlnD("HANDLE: Saving config");
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
    smlAndLoraSetup();
    
    
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

void smlAndLoraLoop(){
    smlSensor->loop();
    os_runloop_once();
    //TODO: ensur, taht  loraInterval is bigger than the val timePassedMs is compared with
    unsigned long timePassedMs = millis()-tryingSince;
    if(timePassedMs>2*60*1000){
      doSleep();
    }
}


void loop(void) {
  debugHandle();
  delay(2);//allow the cpu to switch to other tasks

  if(esp_sleep_get_wakeup_cause()==ESP_SLEEP_WAKEUP_TIMER){
    //printD("woke up from sleep, doin stuff!");
    //DO LORA STUFF
    //loraloopstuff!();
    smlAndLoraLoop();
    
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
