#ifndef CONFIG_H
#define CONFIG_H

#include "SerialDebug.h"
#include "Arduino.h"
#include <list>
#include <regex.h>
#include <stdio.h>
#include "Sensor.h"


#define LONG_STRING_LEN 128
#define STRING_LEN 64
#define NUMBER_LEN 32



static char stypeVals[][STRING_LEN] = { "sml", "s0", "other" };
void printHex2(unsigned v) {
    v &= 0xff;
    if (v < 16)
        printD('0');
    debugD("%x",v);
}

boolean isHexString(String s, uint8_t num_bytes){
  regex_t reegex;
  char regstr[50];
  s.toLowerCase();
  sprintf(regstr, "^[0-9a-f]{2}( [0-9a-f]{2}){%d}$", num_bytes-1);
  char buf[STRING_LEN];
  s.toCharArray(buf, STRING_LEN);
  int v=regcomp( &reegex, regstr, REG_EXTENDED | REG_NOSUB);
  if(regexec(&reegex, buf, 0, NULL, 0)!=0) {
    debugD("notso hexy!");
    return false;
  }
  debugD("voll hexig!");
  return true;
}

bool formStringToByteArray(const char* fs, uint8_t* bytearray, uint8_t num_bytes){
    uint16_t s_len = strlen(fs);

    if(s_len != 3*num_bytes-1){
        return false;
    }

   // for(int i=0;i<s_len;i++){
    //    fs[i]=tolower(fs[i]);
    //}

  //  DEBUG(fs);
    


    for (int i = 0; i < num_bytes; i++) {
        sscanf( fs+ 3*i, "%2hhx", bytearray+i);
       //printf("bytearray %d: %02x\n", i, bytearray[i]);
    }
    return true;
}

bool formObisToList(String fs, std::list<String> *slist){
    bool retval=true;
    regex_t reegex;
    char buf[STRING_LEN];
    fs.toCharArray(buf, STRING_LEN);
    //regex for multiple, space separated obis identifier
    int v=regcomp( &reegex, "^([0-9]\\-[0-9]{1,2}:)?[0-9]\\.[0-9]\\.[0-9]( ([0-9]\\-[0-9]{1,2}:)?[0-9]\\.[0-9]\\.[0-9]){0,3}$", REG_EXTENDED | REG_NOSUB);
    if(regexec(&reegex, buf, 0, NULL, 0)!=0) {
        printD("invalid data format for obis ids");
        retval=false;  
    }else{
        if(slist != NULL){
            int a=0;
            String so="";
            for(int i=0;i < strlen(buf);i++){
                char c=buf[i];
                if(c != ' '){
                    so.concat(c);
                }
                if(c == ' ' || i == strlen(buf)-1){
                    debugD("Pushing ", so);
                    slist->push_back(so);
                    so="";
                }
            }
        }
    }
    return retval;
}

bool isNumeric(String fs){
    regex_t reegex;
    char buf[STRING_LEN];
    fs.toCharArray(buf, STRING_LEN);
    //regex for multiple, space separated obis identifier
    int v=regcomp( &reegex, "^[0-9]{1,4}$", REG_EXTENDED | REG_NOSUB);
    if(regexec(&reegex, buf, 0, NULL, 0)!=0) {
        return false;  
    } 
    return true;
}


#endif
