//#define dbg

#include <Arduino.h>
#include "ESP8266WiFi.h"
#include "ESPAsyncTCP.h"
#include <AsyncElegantOTA.h>
#include <ESPAsyncWebServer.h>
#include <LittleFS.h>
//#include <Adafruit_Sensor.h>
#include <DHTesp.h>
#include <index.h>
#include <string.h>

#define DHTPIN    2     // Digital pin connected to the DHT sensor 
#define DHTTYPE   DHT11  // DHT 11
#define HUMID_PIN 0
#define HEAT_PIN  1
#define delayMS   3000

#ifdef dbg
  #define DEBUG_PRINT Serial.printf("%s : %s : %i\n", __FILE__, __FUNCTION__, __LINE__);Serial.flush();
#else
 #define DEBUG_PRINT
#endif


  DHTesp dht;
  uint32_t lastMillis = millis();

  typedef enum {readings=1, settingsRefresh, settingsSet, wifiRefresh, wifiSet}wsSendType;

  const char *SSID = "AP T/H: 10.0.1.14/wifi"; //ssid must be <= 32chars
  const uint32_t wsDelay=10000;  // WebSock send delay and last send times

  IPAddress  // soft AP IP info
          ip_AP(10,0,1,14)
        , ip_AP_GW(10,0,1,14)
        , ip_subNet(255,255,255,128);

  // objects
  AsyncWebServer server(80);
  AsyncWebSocket webSock("/ws");

typedef struct WifiCreds_t{
      String    SSID;
      String    PWD;
      bool      isDHCP=false;
      IPAddress IP;
      IPAddress GW;
      IPAddress MASK;

      std::string toStr(){
        char s[150];
        sprintf(s, "{\"SSID\":\"%s\",\"PWD\":\"%s\",\"isDHCP\":%s,\"IP\":\"%s\",\"GW\":\"%s\",\"MASK\":\"%s\"}", SSID.c_str(), PWD.c_str(), isDHCP?"true":"false", IP.toString().c_str(), GW.toString().c_str(), MASK.toString().c_str());
        return(s);
      }
} WifiCreds_t;
WifiCreds_t creds;

typedef struct systemValues_t{   // temp/humid & threshold values
        float t1;
        float   h1;
        bool  heat1;
        bool  humid1;
        float t1_on;
        float t1_off;
        float h1_on;
        float h1_off;
        unsigned int delay = delayMS;

        std::string toStr(){    // make JSON string
            char c[200];
            int n = sprintf(c, "[{\"t1\":%.1f,\"h1\":%.1f,\"heat1\":%i,\"humid1\":%i},{\"t1_on\":%.1f,\"t1_off\":%.1f,\"h1_on\":%.1f,\"h1_off\":%.1f,\"delay\":%u}]", t1, h1, heat1, humid1, t1_on, t1_off, h1_on, h1_off, delay);
            return std::string(c, n);
        }
} systemValues_t;
systemValues_t sv;

/******  PROTOTYPES  *******/
void onWsEvent(AsyncWebSocket * server, AsyncWebSocketClient * client, AwsEventType type, void * arg, uint8_t *data, size_t len);
void notFound(AsyncWebServerRequest *request);
void initLocalStruct();
bool wifiConnect(WiFiMode m);

void update_sMsgFromString( const std::string &scaleChar, systemValues_t& p);
std::string valFromJson(const std::string &json, const std::string &element);
bool saveStruct(); // write the local systemValues_t struct string to LittleFS
void setRelays();
bool initCreds();
void setCreds(const std::string& s);
bool saveCreds();


  void setup() {
#ifdef dbg
  Serial.begin(76800);delay(500);
  Serial.setDebugOutput(true);
  Serial.printf("%i :  SETUP\n", __LINE__);Serial.flush();
  if(!LittleFS.begin())Serial.printf("%i : LittleFS init Failed\n", __LINE__);
#else
  LittleFS.begin();
#endif

  if(initCreds() == false)wifiConnect(WIFI_AP);
  if(wifiConnect(WIFI_STA))wifiConnect(WIFI_AP);

#ifdef dbg
  WiFi.printDiag(Serial); Serial.flush();
#endif

  initLocalStruct();              // update sv using LittleFs's JSON if available
  pinMode(HEAT_PIN, OUTPUT);
  pinMode(HUMID_PIN, OUTPUT);

  dht.setup(DHTPIN, DHTesp::DHT11);

  webSock.onEvent(onWsEvent);
  server.addHandler(&webSock);
 
  // Route for root
  server.on("/"        , HTTP_GET, [](AsyncWebServerRequest *request){request->send_P(200, "text/html", INDEX_HTML);});
  server.on("/wifi"    , HTTP_GET, [](AsyncWebServerRequest *request){request->send_P(200, "text/html", WIFI_HTML);});
  server.on("/settings", HTTP_GET, [](AsyncWebServerRequest *request){request->send_P(200, "text/html", SETTINGS_HTML);});
 
  AsyncElegantOTA.begin(&server);
  server.begin();
}

  void loop() {
  delay(100);
DEBUG_PRINT
  if(millis()-lastMillis > sv.delay){ // check DHT reading every sv.delay ms
DEBUG_PRINT
    sv.t1 = dht.getTemperature()*1.8+32;
    sv.h1 = dht.getHumidity();
    setRelays();
    webSock.textAll(sv.toStr().c_str());
    webSock.cleanupClients();

#ifdef dbg
  Serial.printf("\n%i : IP: %s\nLoop: sv: %s\n", __LINE__, WiFi.localIP().toString().c_str(), sv.toStr().c_str());Serial.flush();
#endif
    lastMillis = millis();
  }
}
  void setRelays(){
    // current state and between on/off temps or above/below absolute on/off boundary
    if(sv.t1<sv.t1_on)sv.heat1=true;
    if(sv.t1>sv.t1_off)sv.heat1=false;

    if(sv.h1>sv.h1_on)sv.humid1=true;
    if(sv.h1<sv.h1_off)sv.humid1=false;

    digitalWrite(HEAT_PIN, !sv.heat1);
    digitalWrite(HUMID_PIN , sv.humid1);
  }
  void notFound(AsyncWebServerRequest *request){request->send_P(200, "text/html", INDEX_HTML);}
  void onWsEvent(AsyncWebSocket * server, AsyncWebSocketClient * client, AwsEventType type, void * arg, uint8_t *data, size_t len){
  DEBUG_PRINT
    ESP.wdtFeed();
  DEBUG_PRINT
    switch(type){
      case WS_EVT_CONNECT: return;
      case WS_EVT_DISCONNECT: return;
      case WS_EVT_ERROR: return;
      case WS_EVT_PONG:return;
      case WS_EVT_DATA:
  DEBUG_PRINT
        AwsFrameInfo * info = (AwsFrameInfo*)arg;
        if(info->final && !info->index && info->len == len && info->opcode == WS_TEXT){
  DEBUG_PRINT
          std::string const s=std::string((char*)data+'\0', len+1);
  DEBUG_PRINT
          int src = ::atoi(valFromJson(s, "src").c_str());
  DEBUG_PRINT
          switch(src){
            case readings:
  DEBUG_PRINT
              sv.t1 = dht.getTemperature()*1.8+32;
              sv.h1 = dht.getHumidity();
  DEBUG_PRINT
              setRelays();
  DEBUG_PRINT
              client->text(sv.toStr().c_str());
  DEBUG_PRINT
              break;
            case settingsRefresh: client->text(sv.toStr().c_str());break;
            case settingsSet: update_sMsgFromString(s, sv); if(saveStruct())client->text(sv.toStr().c_str());break;
            case wifiRefresh: client->text(creds.toStr().c_str());break;
            case wifiSet: setCreds(s); if(saveCreds())client->text(creds.toStr().c_str()); delay(3000); ESP.restart();break;
          }
      }
    }
  }

  // load LittleFS copy of JSON into the local scale's systemValues_t
  void initLocalStruct(){
        File f = LittleFS.open("/systemValues_t.json", "r");
        if(f){
                const std::string s = f.readString().c_str();
                f.close();
                update_sMsgFromString(s, sv);
        }
#ifdef dbg
  Serial.printf("%i : initLocalStruct : sv:%s\n", __LINE__, sv.toStr().c_str());Serial.flush();
#endif       
  }
  bool saveStruct(){
        File f = LittleFS.open(F("/systemValues_t.json"), "w");
        if(f){
  #ifdef dbg
    Serial.printf("%i : %s : saveValues Success: sv: %s\n", __LINE__, __FUNCTION__, sv.toStr().c_str());Serial.flush();
  #endif
                f.print(sv.toStr().c_str());
                f.close();
                return true;
        }
        else{
  #ifdef dbg
    Serial.printf("%i : %s : saveValues FAIL: sv: %s\n", __LINE__, __FUNCTION__, sv.toStr().c_str());Serial.flush();
  #endif
            return false;
          }
  }
  bool saveCreds(){
    File f = LittleFS.open(F("/creds.json"), "w");
    if(f){
          f.print(creds.toStr().c_str());
          f.close();
  #ifdef dbg
    Serial.printf("%i : %s : saveCreds Success: creds: %s\n", __LINE__, __FUNCTION__, creds.toStr().c_str());Serial.flush();
  #endif
          return true;
    }
    else{
  #ifdef dbg
    Serial.printf("%i : saveCreds FAILED: creds: %s\n", __LINE__, creds.toStr().c_str());Serial.flush();
  #endif
        return(false);
    }
  }
  bool initCreds(){
          File f = LittleFS.open(F("/creds.json"), "r");
          if(f){
                  std::string s = f.readString().c_str();
                  f.close();
                  setCreds(s);
                  return(true);
          }
  #ifdef dbg
    Serial.printf("\n%i : Failed init creds from LittleFS: %s\n", __LINE__, creds.toStr().c_str());Serial.flush();
  #endif
    return(false);
  }
  void setCreds(const std::string& s){
    creds.SSID=valFromJson(s, "SSID").c_str();
    creds.PWD=valFromJson(s, "PWD").c_str();
    creds.isDHCP=::atoi(valFromJson(s, "isDHCP").c_str());
    creds.IP.fromString(valFromJson(s, "IP").c_str());
    creds.GW.fromString(valFromJson(s, "GW").c_str());
    creds.MASK.fromString(valFromJson(s, "MASK").c_str());
  }
  void update_sMsgFromString(const std::string &json, systemValues_t &p){
#ifdef dbg
  Serial.printf("%i : %s : JSON: %s\n", __LINE__, __FUNCTION__, json.c_str());Serial.flush();
#endif
    p.t1_on     =::atof(valFromJson(json, "t1_on").c_str());
    p.t1_off    =::atof(valFromJson(json, "t1_off").c_str());
    p.h1_on     =::atoi(valFromJson(json, "h1_on").c_str());
    p.h1_off    =::atoi(valFromJson(json, "h1_off").c_str());
    p.delay     =::atoi(valFromJson(json, "delay").c_str());
    
  }
  std::string valFromJson(const std::string &json, const std::string &element){// got stack dumps with ArduinoJson
  DEBUG_PRINT
    size_t start, end;
    start = json.find(element);
  DEBUG_PRINT
    start = json.find(":", start)+1;
  DEBUG_PRINT
    if(json.substr(start,1) =="\"")start++;
    end  = json.find_first_of(",]}\"", start);
  DEBUG_PRINT
    return(json.substr(start, end-start));
  }
  bool wifiConnect(WiFiMode m)
  {

    WiFi.disconnect();
    WiFi.softAPdisconnect(false);

    if(WiFi.getMode() != m)WiFi.mode(m);// WIFI_AP_STA,WIFI_AP; WIFI_STA;

DEBUG_PRINT
    switch(m){
      case WIFI_STA:  // mode == 1
                      if(creds.isDHCP == false)
                      WiFi.config(creds.IP, creds.GW, creds.MASK);
                      WiFi.begin(creds.SSID.c_str(), creds.PWD.c_str(),2);
                      break;
      case WIFI_AP:   // mode == 2
                      WiFi.softAPConfig(ip_AP, ip_AP_GW, ip_subNet);
                      WiFi.softAP(SSID, "");
                      WiFi.begin();
                      break;
      case WIFI_AP_STA: break;  // mode == 3
      case WIFI_OFF:    break;
    }
    unsigned int startup = millis();
    while(WiFi.status() != WL_CONNECTED){
          delay(250);
#ifdef dbg
          Serial.print(WiFi.status());
#endif
          if(millis() - startup >= 15000) break;
    }
#ifdef dbg
  Serial.printf("\n");Serial.flush();
  Serial.printf("%i : %s : IP: %s, GW: %s, Mask: %s\n"
          , __LINE__, __FUNCTION__, WiFi.localIP().toString().c_str(), WiFi.gatewayIP().toString().c_str()
          , WiFi.subnetMask().toString().c_str());Serial.flush();
#endif
    return(WiFi.status() ^ WL_CONNECTED);
  }
  String mac2string(const uint8_t *mac){
    char macStr[18];
    snprintf(macStr, 18, "%02X:%02X:%02X:%02X:%02X:%02X",
         mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
    return(macStr);
  }
