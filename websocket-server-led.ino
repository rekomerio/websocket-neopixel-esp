/*
  Websocket WS2812 LED server software for ESP module
  By Reko Meriö

  Message protocol:
  '*'     Means the following string is name of usable effect
  '!'     Means the following string contains information about controller
  '-*'    Request to get controller effects
  '-!'    Request to get controller information
  '-e<n>' Request to set effect on
  '-t<n>  Request to set speed for effects
  '-s<n>' Request to set sleep
  '-h<n>' Request to set hue
  '-b<n>' Request to set brightness
  '-a'    Request to change hue rotation state
*/

#include <ESP8266WiFi.h>
#include <WiFiClient.h>
#include <WebSocketsServer.h>
#include <Hash.h>
#include <FS.h>
#include <FastLED.h>

#define PORT          81
#define NUM_LEDS      150
#define DATA_PIN      12      // D6 for Wemos D1 mini
#define LED_TYPE      WS2812B
#define COLOR_ORDER   GRB
#define MIN_SPEED     10
#define MAX_SPEED     80
#define BEATS_PER_MIN 62
#define ARRAY_SIZE(A) (sizeof(A) / sizeof((A)[0]))

WebSocketsServer webSocket = WebSocketsServer(PORT);
CRGB leds[NUM_LEDS];

const char *ssid       = "WS2812B";
const char *password   = "lightshow";

uint8_t hue            = 0;
uint8_t brightness     = 100;
uint8_t program        = 0;
uint8_t palettePos     = 0;
uint8_t currentPalette = 0;

uint16_t effectSpeed   = 20;

uint32_t sleep         = 0;
uint32_t lastUpdate    = 0;

bool hueRotation = false;

struct Effect {
  void (*func)();
  char* desc;
};

void solidColor();
void confetti();
void rainbow();
void trail();
void bpm();
void juggle();
void colorFromPalette();

Effect effects[] = {
  {solidColor,       "Solid color"},
  {colorFromPalette, "Color palette"},
  {confetti,         "Confetti"},
  {rainbow,          "Rainbow"},
  {trail,            "Trail"},
  {bpm,              "BPM"},
  {juggle,           "Juggle"}
};

void setup() {
  Serial.begin(115200);
  Serial.println("Connecting");

  WiFi.softAP(ssid, password);

  IPAddress myIP = WiFi.softAPIP();
  Serial.println("Connected!");
  Serial.print("AP IP address: ");
  Serial.println(myIP);

  FastLED.addLeds<LED_TYPE, DATA_PIN, COLOR_ORDER>(leds, NUM_LEDS).setCorrection(TypicalLEDStrip);
  FastLED.setBrightness(brightness);

  webSocket.begin();
  webSocket.onEvent(onWebSocketEvent);
}

void loop() {
  webSocket.loop();
  // Cast to uint32_t to overcome overflow of millis, which happens approximately after 49 days
  // For example: 0 - 50 = -50, cast to uint32_t and value becomes 4294967246
  //              51 - 50 = 1, cast to uint32_t and value becomes 1.
  // YES, there is 50 ms error, but it is does not matter in this case.
  if (sleep && (uint32_t)(millis() - sleep) <= 50) {
    FastLED.setBrightness(0);
    brightness = 0;
    sleep = 0;
  }
  if ((uint32_t)(millis() - lastUpdate) >= effectSpeed) {
    effects[program].func();
    lastUpdate = millis();
    FastLED.show();
    palettePos++;
  }
  if (hueRotation) {
    EVERY_N_MILLISECONDS(50) {
      hue++;
    }
  }
}

void onWebSocketEvent(uint8_t connection, WStype_t type, uint8_t * payload, size_t length) {
  switch (type) {
    case WStype_DISCONNECTED:
      Serial.printf("[%u] Disconnected!\n", connection);
      break;
    case WStype_CONNECTED: {
        IPAddress ip = webSocket.remoteIP(connection);
        Serial.printf("[%u] Connected from %d.%d.%d.%d url: %s\n", connection, ip[0], ip[1], ip[2], ip[3], payload);

        webSocket.sendTXT(connection, "Connected");
      }
      break;
    case WStype_TEXT:
      Serial.printf("[%u] get Text: %s\n", connection, payload);

      if (payload[0] == '-') {
        switch (payload[1]) {
          case '*': // Request for effects
            sendEffects(connection);
            break;
          case '!': // Request for info
            sendInfo(connection);
            break;
          case 'e': // Request to set effect on
            program = strtoul((const char *) &payload[2], NULL, 10);
            program = constrain(program, 0, ARRAY_SIZE(effects) - 1);
            nextPalette();
            break;
          case 't': // Request to set effect speed
            effectSpeed = strtoul((const char *) &payload[2], NULL, 16);
            effectSpeed = constrain(effectSpeed, MIN_SPEED, MAX_SPEED);
            break;
          case 's': // Request to set sleep
            sleep = strtoul((const char *) &payload[2], NULL, 16);
            sleep += sleep ? millis() : sleep; // If sleep is other than zero, add millis to it
            break;
          case 'h': // Request to set hue
            hue = strtoul((const char *) &payload[2], NULL, 16);
            break;
          case 'b': // Request to set brightness
            brightness = strtoul((const char *) &payload[2], NULL, 16);
            FastLED.setBrightness(brightness);
            break;
          case 'a':
            hueRotation = !hueRotation;
            break;
        }
      }
      break;
  }
}

void sendInfo(uint8_t connection) {
  String message;
  String s = sleep ? String((uint32_t)(sleep - millis())) : "0";
  String t = String(effectSpeed);
  String e = String(program);
  String h = String(hue);
  String b = String(brightness);
  message = "!" + s + "," + t + "," + e + "," + h + "," + b;
  webSocket.sendTXT(connection, message);
}

void solidColor() {
  fill_solid(leds, NUM_LEDS, CHSV(hue, 255, 255));
}

void confetti() {
  fadeToBlackBy(leds, NUM_LEDS, 10);
  uint8_t pos = random16(NUM_LEDS);
  leds[pos] += CHSV(hue + random8(64), 200, 255);
}

void rainbow() {
  fill_rainbow(leds, NUM_LEDS, palettePos, 7);
}

void trail() {
  fadeToBlackBy(leds, NUM_LEDS, 20);
  uint8_t pos = beatsin8(15, 0, NUM_LEDS - 1);
  leds[pos] += CHSV(hue, 255, 192);
}

void juggle() {
  fadeToBlackBy(leds, NUM_LEDS, 20);
  uint8_t dothue = 0;
  for ( int i = 0; i < 8; i++) {
    leds[beatsin8( i + 7, 0, NUM_LEDS - 1 )] |= CHSV(dothue, 200, 255);
    dothue += 32;
  }
}

void bpm() {
  CRGBPalette16 palette = PartyColors_p;
  uint8_t beat = beatsin8(BEATS_PER_MIN, 64, 255);
  for (uint8_t i = 0; i < NUM_LEDS; i++) {
    leds[i] = ColorFromPalette(palette, hue + (i * 2), beat - hue + (i * 10));
  }
}

void rotatePalette(CRGBPalette16 &palette) {
  for (uint8_t i = 0; i < NUM_LEDS; i++) {
    leds[i] = ColorFromPalette(palette, palettePos + i);
  }
}

void sendEffects(uint8_t connection) {
  char msg[20] = "*";
  for (uint8_t i = 0; i < ARRAY_SIZE(effects); i++) {
    strcpy(&msg[1], effects[i].desc);
    webSocket.sendTXT(connection, msg);
  }
}

DEFINE_GRADIENT_PALETTE( bhw3_21_gp ) {
  0,   1, 40, 98,
  48,   1, 65, 68,
  76,   2, 161, 96,
  104,   0, 81, 25,
  130,  65, 182, 82,
  153,   0, 86, 170,
  181,  17, 207, 182,
  204,  17, 207, 182,
  255,   1, 23, 46
};


DEFINE_GRADIENT_PALETTE( bhw2_turq_gp ) {
  0,   1, 33, 95,
  38,   1, 107, 37,
  76,  42, 255, 45,
  127, 255, 255, 45,
  178,  42, 255, 45,
  216,   1, 107, 37,
  255,   1, 33, 95
};

DEFINE_GRADIENT_PALETTE( bhw3_11_gp ) {
  0, 192, 252, 49,
  20, 171, 252, 15,
  53,  82, 241, 13,
  86, 153, 248, 88,
  109,  92, 248, 64,
  137, 229, 255, 160,
  155, 161, 250, 32,
  188,  54, 244, 34,
  216,  66, 246, 46,
  247,  69, 248, 21,
  255,  69, 248, 21
};

DEFINE_GRADIENT_PALETTE( bhw3_02_gp ) {
  0, 121,  1,  1,
  63, 255, 57,  1,
  112, 255, 79, 25,
  145, 255, 79, 25,
  188, 244, 146,  3,
  255, 115, 14,  1
};

DEFINE_GRADIENT_PALETTE( bhw3_61_gp ) {
  0,  14,  1, 27,
  48,  17,  1, 88,
  104,   1, 88, 156,
  160,   1, 54, 42,
  219,   9, 235, 52,
  255, 139, 235, 233
};

DEFINE_GRADIENT_PALETTE( rgi_15_gp ) {
  0,   4,  1, 31,
  31,  55,  1, 16,
  63, 197,  3,  7,
  95,  59,  2, 17,
  127,   6,  2, 34,
  159,  39,  6, 33,
  191, 112, 13, 32,
  223,  56,  9, 35,
  255,  22,  6, 38
};

DEFINE_GRADIENT_PALETTE( bhw1_purplered_gp ) {
  0, 255,  0,  0,
  255, 107,  1, 205
};

DEFINE_GRADIENT_PALETTE( wave_gp ) {
  0, 255, 22, 16,
  42,  88, 104,  0,
  84,  14, 255, 16,
  127,   0, 104, 92,
  170,  14, 22, 255,
  212,  88,  0, 92,
  255, 255, 22, 16
};

const TProgmemRGBGradientPalettePtr colorPalettes[] = {
  wave_gp,
  bhw1_purplered_gp,
  rgi_15_gp,
  bhw3_61_gp,
  bhw3_02_gp,
  bhw3_11_gp,
  bhw2_turq_gp,
  bhw3_21_gp
};

const uint8_t colorPaletteCount = sizeof(colorPalettes) / sizeof(TProgmemRGBGradientPalettePtr);

void colorFromPalette() {
  CRGBPalette16 palette = colorPalettes[currentPalette];
  rotatePalette(palette);
}

void nextPalette() {
  ++currentPalette %= colorPaletteCount;
}
