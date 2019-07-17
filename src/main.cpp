#include <M5Stack.h>
#include <TinyGPS++.h>
#include <WiFiMulti.h>
#include <HTTPClient.h>
#include "HeadView.h"
#include "config.h"
#include "FunctionButton.h"

static const uint32_t GPSBaud = 9600;

const long gmtOffset_sec = 9 * 3600; //9時間の時差を入れる
const int daylightOffset_sec = 0;    //夏時間はないのでゼロ
// The TinyGPS++ object
TinyGPSPlus gps;

WiFiClient wc;
FunctionButton btnMode(&M5.BtnA);
FunctionButton btnB(&M5.BtnB);
FunctionButton btnC(&M5.BtnC);

const char *btnNames[][2] = {
    {"Map", "Type"},
    {"ZOOM UP", "ZOOM DOWN"},
};
const char *mapTypes[]{
    "roadmap",
    "satellite",
    "hybrid",
    "terrain",
};
#define MODE ("MODE")
#define MAP ("MAP")
#define ZOOM_UP ("ZOOM UP")
#define ZOOM_DOWN ("ZOOM DOWN")

int mapType = 0;
int mode = 0;
void setup()
{
  M5.begin();
  Serial.begin(115200);
  Serial2.begin(GPSBaud);

  Serial.print("Connecting to WIFI");
  WiFiMulti wm;
  wm.addAP(WIFI_SSID, WIFI_PASS);
  while (wm.run() != WL_CONNECTED)
  {
    Serial.print(".");
    delay(100);
  }
  Serial.println("WiFi connected");
  Serial.print("IP address: ");
  IPAddress addr = WiFi.localIP();
  Serial.println(addr);
  configTime(gmtOffset_sec, daylightOffset_sec, NTP_SERVER);

  headView.init();
  headView.setNwType("WiFi");
  headView.setIpAddress(addr);

  btnMode.enable(MODE);
  btnB.disable(btnNames[mode][0]);
  btnC.enable(mapTypes[mapType]);
}

// This custom version of delay() ensures that the gps object
// is being "fed".
static void smartDelay(unsigned long ms)
{
  unsigned long start = millis();
  do
  {
    while (Serial2.available())
      gps.encode(Serial2.read());
  } while (millis() - start < ms);
}

HTTPClient http;
char msg[1024];
void request(double latitude, double longitude, int zoom, const char *key, int maptype)
{
  snprintf(msg, sizeof(msg), "http://maps.google.com/maps/api/staticmap?center=%f,%f&size=320x200&sensor=false&key=%s&zoom=%d&format=jpg-baseline&maptype=%s&scale=1&markers=size:mid|color:red|label:A|%f,%f", latitude, longitude, key, zoom, mapTypes[maptype], latitude, longitude);
  http.begin(msg);
  int statusCode = http.GET();
  int size = http.getSize();

  Serial.print("Status code: ");
  Serial.println(statusCode);
  Serial.print("Size: ");
  uint8_t *buf = (uint8_t *)malloc(size);
  Serial.printf("%d(%p)", size, buf);
  Serial.println();
  http.getStream().readBytes(buf, size);
  M5.Lcd.drawJpg(buf, size, 0, 10, 320, 200);
  free(buf);
}

boolean valid = false;
int zoom = 15;
void loop()
{
  smartDelay(1000);

  if (millis() > 5000 && gps.charsProcessed() < 10)
    Serial.println(F("No GPS data received: check wiring"));

  double lat = gps.location.lat();
  double lng = gps.location.lng();
  double alt = gps.altitude.meters();

  if (btnMode.getButton()->wasPressed())
  {
    if (++mode > 2 - 1)
      mode = 0;
    Serial.print("mode: ");
    Serial.println(mode);
    switch (mode)
    {
    case 0:
      if (valid)
        btnB.enable(btnNames[mode][0]);
      else
        btnB.disable(btnNames[mode][0]);
      btnC.enable(mapTypes[mapType]);
      break;
    case 1:
      if (zoom <= 1)
        btnC.disable(btnNames[mode][1]);
      else if (zoom >= 22)
        btnB.disable(btnNames[mode][0]);
      else
      {
        btnB.enable(btnNames[mode][0]);
        btnC.enable(btnNames[mode][1]);
      }
      break;
    }
  }
  else if (btnB.getButton()->wasPressed())
  {
    if (valid && btnB.isEnable())
    {
      btnB.disable(btnNames[mode][0]);
      switch (mode)
      {
      case 0:
        break;
      case 1:
        zoom++;
        Serial.print("zoom: ");
        Serial.println(zoom);
        btnC.enable(btnNames[mode][1]);
        break;
      }
      request(lat, lng, zoom, API_KEY, mapType);
      if (zoom < 22)
        btnB.enable(btnNames[mode][0]);
    }
  }
  else if (btnC.getButton()->wasPressed())
  {
    switch (mode)
    {
    case 0:
      if (++mapType > 4 - 1)
        mapType = 0;
      btnC.enable(mapTypes[mapType]);
      break;
    case 1:
      if (valid && btnC.isEnable())
      {
        btnC.disable(btnNames[mode][1]);
        request(lat, lng, --zoom, API_KEY, mapType);
        Serial.print("zoom: ");
        Serial.println(zoom);
        if (zoom > 1)
          btnC.enable(btnNames[mode][1]);
        btnB.enable(btnNames[mode][0]);
      }
      break;
    }
  }

  char buf[64];
  M5.Lcd.setTextColor(WHITE);
  int y = 239 - 25;
  if (gps.location.isValid() && gps.altitude.isValid() && gps.satellites.isValid())
  {
    M5.Lcd.fillRect(0, y, 320, 10, BLACK);
    int sats = gps.satellites.value();
    snprintf(buf, sizeof(buf), "Lat.:%11.6f, Lon.:%12.6f, Alt.:%6.2f (%2d)", lat, lng, alt, sats);
    M5.Lcd.drawString(buf, 0, y, 1);

    if (!valid)
    {
      switch (mode)
      {
      case 0:
        btnB.enable(btnNames[mode][0]);
        break;
      case 1:
        btnB.enable(btnNames[mode][0]);
        btnC.enable(btnNames[mode][1]);
        break;
      }
      valid = true;
    }
  }
  else
  {
    if (valid)
    {
      btnB.disable(btnNames[mode][0]);
      btnC.disable(btnNames[mode][1]);
      valid = false;
    }
    if (gps.satellites.isValid())
    {
      int sats = gps.satellites.value();
      snprintf(buf, sizeof(buf), "Lat.:****.******, Lon.:*****.******, Alt:***.** (%2d)", sats);
      M5.Lcd.drawString(buf, 0, y, 1);
    }
    else
    {
      M5.Lcd.drawString("Lat.:****.******, Lon.:*****.******, Alt:***.** (**)", 0, y, 1);
    }
  }
  headView.update();

  M5.update();
}
