/*  Pinout
 *  DATA = 13
 *  Buzzer = 2
 */
#include <NTPClient.h>
#include <DNSServer.h>
#include <WiFi.h>
#include <WiFiUdp.h>
#include <FastLED.h>
#include <ESPAsyncWebServer.h>
#include <AsyncTCP.h>
#include <AsyncJson.h>
#include <ArduinoJson.h>
#include <SPIFFS.h>

const int buzzer = 2;

char ssid[55] = "";
char pass[55] = "";
/* SPIFFS */
const char *path_configWifi = "/config_wifi.json";
void load_config() {
  File file = SPIFFS.open(path_configWifi, FILE_READ);
  StaticJsonDocument<400> doc;
  DeserializationError error = deserializeJson(doc, file);
  if (error)
    Serial.println(F("Failed to read file, using default configuration"));

  strlcpy(ssid, doc["ssid"] , sizeof(ssid));
  strlcpy(pass, doc["pass"], sizeof(pass));

  file.close();
}

/* AP, WiFi & WebServer */
DNSServer dnsServer;
AsyncWebServer server(80);

class CaptiveRequestHandler : public AsyncWebHandler {
public:
  CaptiveRequestHandler() {}
  virtual ~CaptiveRequestHandler() {}

  bool canHandle(AsyncWebServerRequest *request){
    //request->addInterestingHeader("ANY");
    return true;
  }

  void handleRequest(AsyncWebServerRequest *request) {
    request->redirect("http://" + WiFi.softAPIP().toString() + "/wifiman.html");
  }
};

void ap_init() {
  WiFi.softAP("LEDSTRIP Clock");

  IPAddress IP = WiFi.softAPIP();
  Serial.print("AP IP Address: ");
  Serial.println(IP);
  
  server.on("/wifiman", HTTP_GET, [](AsyncWebServerRequest *request){
    request->send(SPIFFS, "/wifimanager.html", "text/html");
  });
  server.on("/", HTTP_GET, [](AsyncWebServerRequest *request){
    request->send(SPIFFS, "/wifimanager.html", "text/html");
  });

  server.on("/", HTTP_POST, [](AsyncWebServerRequest *request) {
    int params = request->params();
    for(int i=0;i<params;i++){
      AsyncWebParameter* p = request->getParam(i);
      if (!p->isPost()) continue;

      bool changed = false;
      if (p->name() == "ssid") {
        changed = true;
        const char* ssid_new = p->value().c_str();
        strlcpy(ssid, ssid_new, sizeof(ssid));
      }
      if (p->name() == "pass") {
        changed = true;
        const char* pass_new = p->value().c_str();
        strlcpy(pass, pass_new, sizeof(pass));
      }

      if(!changed) continue;
      File file = SPIFFS.open(path_configWifi, FILE_WRITE);
      if(!file || file.isDirectory()){
        Serial.println("- failed to open file for reading");
        break;
      }
      StaticJsonDocument<400> doc;
      doc["ssid"] = ssid;
      doc["pass"] = pass;

      if (serializeJson(doc, file) == 0) {
        Serial.println(F("Failed to write to file"));
      }
      file.close();
    }
    request->send(200, "text/plain", "Done. ESP will restart, connect to your router");
    delay(3000);
    ESP.restart();
  });

  server.serveStatic("/", SPIFFS, "/").setDefaultFile("/wifiman.html");
  dnsServer.start(53, "*", WiFi.softAPIP());
  server.addHandler(new CaptiveRequestHandler()).setFilter(ON_AP_FILTER);

  server.begin();
}

bool wifi_init() {
  WiFi.mode(WIFI_STA);
  WiFi.begin(ssid, pass);

  unsigned long lastM_wifiCon = 0;
  while(WiFi.status() != WL_CONNECTED) {
    Serial.print(".");
    if (millis() - lastM_wifiCon >= 10000) {
      Serial.println("Failed to connect wifi");
      return false;
    }
    delay(100);
  }

  Serial.println(WiFi.localIP());
  return true;
}

/* NTP Program*/
WiFiUDP ntpUDP;
NTPClient timeClient(ntpUDP, "1.id.pool.ntp.org", 3600 * 7);

/* LED Program */
#define NUM_LEDS 58
#define LEDS_STRIP 2
#define DATA_LED 13

CRGBArray<NUM_LEDS> leds;
CRGBSet led_dot(leds(28,29), 2);

byte chars[] = {
  0b00111111, // 0
  0b00000110, // 1
  0b01011011, // 2
  0b01001111, // 3
  0b01100110, // 4
  0b01101101, // 5
  0b01111101, // 6
  0b00000111, // 7
  0b01111111, // 8
  0b01101111, // 9
  0b01100011, // °
  0b00111001, // C
};

enum CHARA {
  deg = 10,
  C,
};

byte hue = 0;
unsigned long lastM_blink = 0;
bool blinking = 0;
unsigned long lastM_hue = 0;

void setSegment(byte pos, int number) {
  int pos_start = LEDS_STRIP * 7 * pos;
  if (pos >= 2) pos_start += 2;
  for (int i=0; i<7; i++) {
    for(int j=0; j < LEDS_STRIP; j++) {
      int real_pos = LEDS_STRIP * i + j + pos_start;
      if (chars[number] >> i &1) {
        leds[real_pos] = CHSV(hue + real_pos + 5, 255, 255);
      } else {
        leds[real_pos] = CRGB::Black;
      }
    }
  }
}

void displayJam() {
  const int jam1 = (timeClient.getHours() < 10) ? 0 : timeClient.getHours() / 10;
  const int jam2 = (timeClient.getHours() < 10) ? timeClient.getHours() : timeClient.getHours() - (jam1 * 10);
  const int menit1 = (timeClient.getMinutes() < 10) ? 0 : timeClient.getMinutes() / 10;
  const int menit2 = (timeClient.getMinutes() < 10) ? timeClient.getMinutes() : timeClient.getMinutes() - (menit1 * 10);

  setSegment(3, jam1);
  setSegment(2, jam2);
  leds[28] = CHSV(hue + 28, 255, 255);
  leds[29] = CHSV(hue + 29, 255, 255);
  setSegment(1, menit1);
  setSegment(0, menit2);
  if (!blinking) led_dot = CRGB::Black;
}

/*
void displaySuhu() {
  RtcTemperature get_temp = Rtc.GetTemperature();
  const int temp = get_temp.AsFloatDegC();
  const int temp1 = (temp < 10) ? 0 : temp / 10;
  const int temp2 = (temp < 10) ? temp : temp - (temp1 * 10);

  setSegment(3, temp1);
  setSegment(2, temp2);
  setSegment(1, CHARA::deg);
  setSegment(0, CHARA::C);
}
*/
void setup() {
  // put your setup code here, to run once:
  Serial.begin(9600);
  if(!SPIFFS.begin()){
    Serial.println("SPIFFS Mount Failed");
    return;
  }
  load_config();

  pinMode(buzzer, OUTPUT);
  digitalWrite(buzzer, 1); delay(100);
  digitalWrite(buzzer, 0); delay(100);
  digitalWrite(buzzer, 1); delay(100);
  digitalWrite(buzzer, 0); delay(100);
  if (!wifi_init()) {
    ap_init();
  }
  timeClient.begin();
  FastLED.addLeds<NEOPIXEL, DATA_LED>(leds, NUM_LEDS);
}

void loop() {
  // put your main code here, to run repeatedly:
  timeClient.update();
  if (WiFi.status() != WL_CONNECTED) {
    dnsServer.processNextRequest();
  }
  if (millis() - lastM_blink >= 1000) {
    lastM_blink = millis();
    blinking = !blinking;
  }
  if (millis() - lastM_hue >= 30) {
    lastM_hue = millis();
    hue++;
  }
  
  const int second = timeClient.getSeconds();
//  if (second > 40) displayJam();
//  else if (second > 30) displaySuhu();
//  else if (second > 10) displayJam(); 
//  else displaySuhu();
  displayJam();
  FastLED.show(); 
}
