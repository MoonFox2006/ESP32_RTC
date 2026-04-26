#include <Arduino.h>
#include <WiFi.h>
#include "secret.h"

#define LED_PIN   LED_BUILTIN
#define LED_LEVEL LOW
#define LED_PULSE 50

static bool wifiConnect(const char *ssid, const char *pswd, uint32_t timeout = 30000) {
  uint32_t start;

  WiFi.persistent(false);
  WiFi.mode(WIFI_STA);
  WiFi.begin(ssid, pswd);
  start = millis();
  Serial.printf("Connecting to \"%s\"", ssid);
  while ((! WiFi.isConnected()) && (millis() - start < timeout)) {
#ifdef LED_PIN
    digitalWrite(LED_PIN, LED_LEVEL);
    delay(LED_PULSE);
    digitalWrite(LED_PIN, ! LED_LEVEL);
#endif
    Serial.print('.');
#ifdef LED_PIN
    delay(500 - LED_PULSE);
#else
    delay(500);
#endif
  }
  if (WiFi.isConnected()) {
    Serial.println(" OK");
    return true;
  } else {
    WiFi.disconnect(true);
    Serial.println(" FAIL!");
    return false;
  }
}

static uint32_t ntpUpdate(const char *server, int8_t tz, uint32_t timeout = 1000, uint8_t repeat = 1) {
  constexpr uint16_t LOCAL_PORT = 55123;

  WiFiUDP udp;

  if (udp.begin(LOCAL_PORT)) {
    do {
      uint8_t buffer[48];

      memset(buffer, 0, sizeof(buffer));
      // Initialize values needed to form NTP request
      buffer[0] = 0B11100011; // LI, Version, Mode
      buffer[1] = 0; // Stratum, or type of clock
      buffer[2] = 6; // Polling Interval
      buffer[3] = 0xEC; // Peer Clock Precision
      // 8 bytes of zero for Root Delay & Root Dispersion
      buffer[12] = 49;
      buffer[13] = 0x4E;
      buffer[14] = 49;
      buffer[15] = 52;
      // all NTP fields have been given values, now
      // you can send a packet requesting a timestamp
      if (udp.beginPacket(server, 123) && (udp.write(buffer, sizeof(buffer)) == sizeof(buffer)) && udp.endPacket()) {
        uint32_t time = millis();
        int cb;

        while ((! (cb = udp.parsePacket())) && (millis() - time < timeout)) {
          delay(1);
        }
        if (cb) {
          // We've received a packet, read the data from it
          if (udp.read(buffer, sizeof(buffer)) == sizeof(buffer)) { // read the packet into the buffer
            // the timestamp starts at byte 40 of the received packet and is four bytes,
            // or two words, long. First, esxtract the two words:
            time = (((uint32_t)buffer[40] << 24) | ((uint32_t)buffer[41] << 16) | ((uint32_t)buffer[42] << 8) | buffer[43]) - 2208988800UL;
            time += tz * 3600;
            return time;
          }
        }
      }
      if (repeat)
        delay(timeout / 2);
    } while (repeat--);
  }
  return 0;
}

void setup() {
  timeval tv;
  uint32_t time;

#ifdef LED_PIN
  pinMode(LED_PIN, OUTPUT);
  digitalWrite(LED_PIN, ! LED_LEVEL);
#endif

  Serial.begin(115200);
  while (! Serial) {
#ifdef LED_PIN
    digitalWrite(LED_PIN, LED_LEVEL);
    delay(LED_PULSE);
    digitalWrite(LED_PIN, ! LED_LEVEL);
    delay(250 - LED_PULSE);
#else
    delay(250);
#endif
  }

  time = 0;
  if ((! wifiConnect(WIFI_SSID, WIFI_PSWD, 30000)) || (! (time = ntpUpdate(NTP_SERVER, NTP_TZ)))) {
    if (esp_reset_reason() != ESP_RST_DEEPSLEEP) {
      Serial.println("Failed to start!");
      Serial.flush();
      esp_deep_sleep_start();
    }
  }

  if (time) {
    uint32_t secs = time % (3600 * 24);

    if (esp_reset_reason() != ESP_RST_DEEPSLEEP) {
      tv.tv_sec = time;
      tv.tv_usec = 0;
      settimeofday(&tv, NULL);
      Serial.printf("Setting RTC time to %u:%02u:%02u\r\n", (unsigned int)(secs / 3600), (unsigned int)((secs / 60) % 60), (unsigned int)(secs % 60));
    } else {
      gettimeofday(&tv, NULL);
      Serial.printf("NTP time: %u:%02u:%02u\r\n", (unsigned int)(secs / 3600), (unsigned int)((secs / 60) % 60), (unsigned int)(secs % 60));
      Serial.printf("RTC to NTP difference: %+d sec.\r\n", (int)(tv.tv_sec - time));
    }
    esp_sleep_enable_timer_wakeup(3600000000ULL); // 1 hr.
  } else { // NTP failure
    esp_sleep_enable_timer_wakeup(300000000ULL); // 5 min.
  }

  esp_deep_sleep_disable_rom_logging();
  Serial.flush();
  esp_deep_sleep_start();
}

void loop() {}
