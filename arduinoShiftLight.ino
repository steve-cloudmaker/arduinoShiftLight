bool verbose = false;
bool tachDisplay = true;

// eeprom
#include <EEPROMex.h>
#define CONFIG_VERSION "ls1"
#define MEMORY_BASE 32
int configAddress = 0;

// Thank you AdaFruit!
// neopixel
#include <Adafruit_NeoPixel.h>
// alpha-quad
#include <Wire.h>
#include "Adafruit_LEDBackpack.h"
#include "Adafruit_GFX.h"

// Thank you buxtronix!
// http://www.buxtronix.net/2011/10/rotary-encoders-done-properly.html
// https://github.com/buxtronix/arduino/tree/master/libraries/Rotary
#include <Rotary.h>

// Thank you Brian Park!
// https://github.com/bxparks/AceButton
#include <AceButton.h>
using namespace ace_button;

struct SettingsStruct {
  char version[4];
  byte brightness;
  unsigned int enable_rpm;
  unsigned int shift_rpm;
  byte color_primary;  // colors will have to be represented as 0xGGRRBB
  byte color_secondary;
  byte color_tertiary;
  byte color_shift_primary;
  byte color_shift_secondary;
} Settings = {
  CONFIG_VERSION,
  0x7F, 0xDAC, 0x1D4C, 0x50, 0x38, 0xC, 0xFC, 0xE0
  // 127,  3500,   7500, colors
};

#define NEO_PIN 5
const int numPixels = 24;
// NEO_KHZ800  800 KHz bitstream (most NeoPixel products w/WS2812 LEDs)
// NEO_GRB     Pixels are wired for GRB bitstream (most NeoPixel products)
Adafruit_NeoPixel strip = Adafruit_NeoPixel(numPixels, NEO_PIN, NEO_GRB + NEO_KHZ800);

// IMPORTANT: To reduce NeoPixel burnout risk, add 1000 uF capacitor across
// pixel power leads, add 300 - 500 Ohm resistor on first pixel's data input
// and minimize distance between Arduino and first pixel.  Avoid connecting
// on a live circuit...if you must, connect GND first.

// alpha4 is on I2C
Adafruit_AlphaNum4 alpha4 = Adafruit_AlphaNum4();
char displaybuffer[5] = "0000";

// rotary encoder on pins 0 and 1
Rotary rotary = Rotary(0, 1);
volatile int clickCounter = 0;
int clickCounterLast = 0;
volatile unsigned int fakeRpmCounter = 0;

// button on rotary encoder
#define buttonPin 8
AceButton button(buttonPin);
void handleEvent(AceButton*, uint8_t, uint8_t);

#define encoder0PinA 0 // int 2 is pin 0 in micro
#define encoder0PinB 1 // int 3 is pin 1 in micro

// menu state machine
boolean menuState = false;
boolean setItemState = false;
boolean prevSetItemState = false;
int menuPos = 0;
int lastMenuPos = -1;

// tachometer
#define tachPin 7 // int 3 is pin 7 in micro

#define RPM_ARRAY_SIZE 3
unsigned int rpmArray[RPM_ARRAY_SIZE];
byte rpmArrayIdx = 0;
byte rpmIdx = 0;
unsigned int rpm = 1000;
unsigned int rpmTotal = 0;
volatile unsigned int revs = 0;
unsigned long nowTime = micros();
unsigned long lastTime = 0;
unsigned long elapsedTime;
const int cylinderDivider = 2; // 4cyl fires twice per crank revolution

bool fakeRPM = false;
int displayStyle = 0; // 0 for dot, 1 for bar

unsigned int lastDisplayedRpm = 0xFFFF;
unsigned long shiftFlashTime = 0;
bool shiftFlashState = false;


/*

███████╗███████╗████████╗██╗   ██╗██████╗
██╔════╝██╔════╝╚══██╔══╝██║   ██║██╔══██╗
███████╗█████╗     ██║   ██║   ██║██████╔╝
╚════██║██╔══╝     ██║   ██║   ██║██╔═══╝
███████║███████╗   ██║   ╚██████╔╝██║
╚══════╝╚══════╝   ╚═╝    ╚═════╝ ╚═╝

 */
void setup() {

  if ( verbose ) {
    Serial.begin(115200);
    delay(1000);
    Serial.println(F("hello world"));
    delay(1000);
  }

  EEPROM.setMemPool(MEMORY_BASE, EEPROMSizeMicro);
  configAddress = EEPROM.getAddress(sizeof(SettingsStruct));
  loadConfig();

  alpha4.begin(0x70);
  alpha4.setBrightness(Settings.brightness / 16);
  alpha4DashChase();
  strcpy(displaybuffer, "boot");
  alpha4print();
  delay(100);

  strip.begin();
  strip.setBrightness(Settings.brightness);
  strip.show(); // Initialize all pixels to 'off'
  rainbow(5);
  delay(100);
  blackout();

  alpha4.clear();
  alpha4.writeDisplay();

  pinMode(buttonPin, INPUT_PULLUP);
  button.setEventHandler(handleEvent);

  pinMode(encoder0PinA, INPUT_PULLUP);
  pinMode(encoder0PinB, INPUT_PULLUP);
  pinMode(tachPin, INPUT_PULLUP);
  attachInterrupt(digitalPinToInterrupt(encoder0PinA), rotate, CHANGE);
  attachInterrupt(digitalPinToInterrupt(encoder0PinB), rotate, CHANGE);
  attachInterrupt(digitalPinToInterrupt(tachPin), tachISR, RISING);

  for (rpmArrayIdx = 0; rpmArrayIdx < RPM_ARRAY_SIZE; rpmArrayIdx++) {
    rpmArray[rpmArrayIdx] = 0;
  }
}


/*

███╗   ███╗ █████╗ ██╗███╗   ██╗
████╗ ████║██╔══██╗██║████╗  ██║
██╔████╔██║███████║██║██╔██╗ ██║
██║╚██╔╝██║██╔══██║██║██║╚██╗██║
██║ ╚═╝ ██║██║  ██║██║██║ ╚████║
╚═╝     ╚═╝╚═╝  ╚═╝╚═╝╚═╝  ╚═══╝

*/
void loop() {
  delay(10);
  byte rpmArrayIdx = 0;

  if ( fakeRPM == false ) {
    nowTime = micros();
    if ( nowTime - lastTime > 500000 ) {    // 500ms measurement window
      detachInterrupt(digitalPinToInterrupt(tachPin));
      elapsedTime = nowTime - lastTime;
      lastTime = nowTime;
      rpm = (unsigned long)revs * 60000000UL / elapsedTime / cylinderDivider;
      rpmArray[rpmIdx] = rpm;
      rpmIdx++;
      if ( rpmIdx >= RPM_ARRAY_SIZE ) {
        rpmIdx = 0;
      }
      revs = 0;
      attachInterrupt(digitalPinToInterrupt(tachPin), tachISR, RISING);
      rpmTotal = 0;
      for (rpmArrayIdx = 0; rpmArrayIdx < RPM_ARRAY_SIZE; rpmArrayIdx++) {
        rpmTotal += rpmArray[rpmArrayIdx];
      }
      rpm = rpmTotal / RPM_ARRAY_SIZE;
    }
  } else {
    fakeRpmCounter = constrain(fakeRpmCounter, 500, 8000);
    rpm = fakeRpmCounter;
  }

  if ( verbose ) {
    Serial.print(F("revs: ")); Serial.print(revs);
    Serial.print(F(" rpm: ")); Serial.print(rpm);
    Serial.print(F(" now: ")); Serial.print(nowTime);
    Serial.print(F(" last: ")); Serial.print(lastTime);
    Serial.print(F(" elapsed: ")); Serial.println(elapsedTime);
  }

  lightItUp();

  button.check();
  if ( !menuState ) {
    tachDisplay = true;
    if ( rpm != lastDisplayedRpm ) {
      lastDisplayedRpm = rpm;
      snprintf(displaybuffer, sizeof(displaybuffer), "%04d", rpm);
      alpha4print();
    }
  } else if ( menuState && !setItemState ) {
    lastDisplayedRpm = 0xFFFF; // force refresh on return to tach mode
    tachDisplay = false;
    if ( clickCounter > clickCounterLast ) {
      if ( menuPos < 11 ) {
        menuPos++;
      }
      clickCounterLast = clickCounter;
    } else if ( clickCounter < clickCounterLast ) {
      if ( menuPos > 0 ) {
        menuPos--;
      }
      clickCounterLast = clickCounter;
    }
    if ( verbose ) {
      Serial.print(F("encoder: ")); Serial.print(clickCounter);
      Serial.print(F(" last: ")); Serial.print(clickCounterLast);
      Serial.print(F(" menu: ")); Serial.print(menuPos);
      Serial.print(F(" menuState: ")); Serial.print(menuState);
      Serial.print(F(" setItem: ")); Serial.println(setItemState);
    }
  } else if ( menuState && setItemState ) {
    lastDisplayedRpm = 0xFFFF; // force refresh on return to tach mode
    switch (menuPos) {
      case 1: // brightness
        // usable range: 0x2F (dim) to 0x6B (bright)
        if ( clickCounter > clickCounterLast ) {
          if ( Settings.brightness < 240 ) {
            Settings.brightness += 16;
          }
        } else if ( clickCounter < clickCounterLast ) {
          if ( Settings.brightness > 16 ) {
            Settings.brightness -= 16;
          }
        }
        if ( Settings.brightness < 8 ) {
          Settings.brightness = 252;
        }
        if ( Settings.brightness > 252 ) {
          Settings.brightness = 8;
        }
        clickCounterLast = clickCounter;
        alpha4.setBrightness(Settings.brightness / 16);
        snprintf(displaybuffer, sizeof(displaybuffer), "%04d", Settings.brightness);
        alpha4print();
        colorFill(Wheel(Settings.color_primary), Settings.brightness);
        break;
      case 2: // enable rpm
        if ( clickCounter > clickCounterLast && Settings.enable_rpm <= Settings.shift_rpm - 200 ) {
          Settings.enable_rpm += 100;
        } else if ( clickCounter < clickCounterLast && Settings.enable_rpm > 1500 ) {
          Settings.enable_rpm -= 100;
        }
        clickCounterLast = clickCounter;
        snprintf(displaybuffer, sizeof(displaybuffer), "%04d", Settings.enable_rpm);
        alpha4print();
        break;
      case 3: // shift rpm
        if ( clickCounter > clickCounterLast && Settings.shift_rpm < 9000 ) {
          Settings.shift_rpm += 100;
        } else if ( clickCounter < clickCounterLast && Settings.shift_rpm > Settings.enable_rpm ) {
          Settings.shift_rpm -= 100;
        }
        clickCounterLast = clickCounter;
        snprintf(displaybuffer, sizeof(displaybuffer), "%04d", Settings.shift_rpm);
        alpha4print();
        break;
      case 4: // color 1
        if ( clickCounter > clickCounterLast ) {
          Settings.color_primary += 4;
        } else if ( clickCounter < clickCounterLast ) {
          Settings.color_primary -= 4;
        }
        clickCounterLast = clickCounter;
        snprintf(displaybuffer, sizeof(displaybuffer), "%04d", Settings.color_primary);
        alpha4print();
        colorFill(Wheel(Settings.color_primary), Settings.brightness);
        break;
      case 5: // color 2
        if ( clickCounter > clickCounterLast ) {
          Settings.color_secondary += 4;
        } else if ( clickCounter < clickCounterLast ) {
          Settings.color_secondary -= 4;
        }
        clickCounterLast = clickCounter;
        snprintf(displaybuffer, sizeof(displaybuffer), "%04d", Settings.color_secondary);
        alpha4print();
        colorFill(Wheel(Settings.color_secondary), Settings.brightness);
        break;
      case 6: // color 3
        if ( clickCounter > clickCounterLast ) {
          Settings.color_tertiary += 4;
        } else if ( clickCounter < clickCounterLast ) {
          Settings.color_tertiary -= 4;
        }
        clickCounterLast = clickCounter;
        snprintf(displaybuffer, sizeof(displaybuffer), "%04d", Settings.color_tertiary);
        alpha4print();
        colorFill(Wheel(Settings.color_tertiary), Settings.brightness);
        break;
      case 7: // shift color 1
        if ( clickCounter > clickCounterLast ) {
          Settings.color_shift_primary += 4;
        } else if ( clickCounter < clickCounterLast ) {
          Settings.color_shift_primary -= 4;
        }
        clickCounterLast = clickCounter;
        snprintf(displaybuffer, sizeof(displaybuffer), "%04d", Settings.color_shift_primary);
        alpha4print();
        colorFill(Wheel(Settings.color_shift_primary), Settings.brightness);
        break;
      case 8: // shift color 2
        if ( clickCounter > clickCounterLast ) {
          Settings.color_shift_secondary += 4;
        } else if ( clickCounter < clickCounterLast ) {
          Settings.color_shift_secondary -= 4;
        }
        clickCounterLast = clickCounter;
        snprintf(displaybuffer, sizeof(displaybuffer), "%04d", Settings.color_shift_secondary);
        alpha4print();
        colorFill(Wheel(Settings.color_shift_secondary), Settings.brightness);
        break;
      case 9: // fake rpm
        if ( clickCounter > clickCounterLast ) {
          fakeRPM = true;
          strcpy(displaybuffer, "YeP ");
          alpha4print();
          clickCounterLast = clickCounter;
        } else if ( clickCounter < clickCounterLast ) {
          fakeRPM = false;
          strcpy(displaybuffer, "NoPe");
          alpha4print();
          clickCounterLast = clickCounter;
        }
        break;
      case 10: // display style
        if ( (clickCounter < clickCounterLast) && (displayStyle == 1) ) {
          displayStyle = 0;
          strcpy(displaybuffer, "Dot ");
          alpha4print();
          clickCounterLast = clickCounter;
        }
        if ( ((clickCounter > clickCounterLast) && (displayStyle == 0)) || ((clickCounter < clickCounterLast) && (displayStyle == 2)) ) {
          displayStyle = 1;
          strcpy(displaybuffer, "Bar ");
          alpha4print();
          clickCounterLast = clickCounter;
        }
        if ( (clickCounter > clickCounterLast) && (displayStyle == 1) ) {
          displayStyle = 2;
          strcpy(displaybuffer, "CinO");
          alpha4print();
          clickCounterLast = clickCounter;
        }
        break;
      default:
        clickCounterLast = clickCounter;
        break;
    }

    if ( verbose ) {
      Serial.print(F("encoder: ")); Serial.print(clickCounter);
      Serial.print(F(" last: ")); Serial.print(clickCounterLast);
      Serial.print(F(" menu: ")); Serial.print(menuPos);
      Serial.print(F(" menuState: ")); Serial.print(menuState);
      Serial.print(F(" setItem: ")); Serial.println(setItemState);
      Serial.print(F(" brt: ")); Serial.print(Settings.brightness, HEX);
      Serial.print(F(" enab: ")); Serial.print(Settings.enable_rpm);
      Serial.print(F(" shft: ")); Serial.print(Settings.shift_rpm);
      Serial.print(F(" col1: ")); Serial.print(Settings.color_primary, HEX);
      Serial.print(F(" col2: ")); Serial.print(Settings.color_secondary, HEX);
      Serial.print(F(" col3: ")); Serial.print(Settings.color_tertiary, HEX);
      Serial.print(F(" sft1: ")); Serial.print(Settings.color_shift_primary, HEX);
      Serial.print(F(" sft2: ")); Serial.println(Settings.color_shift_secondary, HEX);
    }
  }

  // redraw menu label only when position changes or returning from setItemState
  if ( menuState && !setItemState ) {
    if ( prevSetItemState ) {
      lastMenuPos = -1; // force redraw when returning from a setting
    }
    if ( menuPos != lastMenuPos ) {
      lastMenuPos = menuPos;
      switch (menuPos) {
        case 0:
          strcpy(displaybuffer, "EXIT");
          alpha4print();
          blackout();
          break;
        case 1:
          strcpy(displaybuffer, "Brt ");
          alpha4print();
          break;
        case 2:
          strcpy(displaybuffer, "Enbl");
          alpha4print();
          break;
        case 3:
          strcpy(displaybuffer, "Shft");
          alpha4print();
          break;
        case 4:
          strcpy(displaybuffer, "Col1");
          alpha4print();
          break;
        case 5:
          strcpy(displaybuffer, "Col2");
          alpha4print();
          break;
        case 6:
          strcpy(displaybuffer, "Col3");
          alpha4print();
          break;
        case 7:
          strcpy(displaybuffer, "SC 1");
          alpha4print();
          break;
        case 8:
          strcpy(displaybuffer, "SC 2");
          alpha4print();
          break;
        case 9:
          strcpy(displaybuffer, "Fake");
          alpha4print();
          break;
        case 10:
          strcpy(displaybuffer, "StYl");
          alpha4print();
          break;
        case 11:
          strcpy(displaybuffer, "SAVE");
          alpha4print();
          blackout();
          break;
      }
    }
  }

  prevSetItemState = setItemState;
}

// flood all pixels with a color at the given brightness
void colorFill(uint32_t c, uint8_t brightness) {
  for (uint16_t i = 0; i < strip.numPixels(); i++) {
    strip.setPixelColor(i, c);
  }
  strip.setBrightness(Settings.brightness);
  strip.show();
}

void lightItUp(void) {
  int i = 0;
  if ( tachDisplay == false ) {
    return;
  }
  if ( rpm < Settings.enable_rpm ) {
    for (i = 0; i < numPixels; i++) {
      strip.setPixelColor(i, 0);
    }
    strip.show();
    return;
  }

  // precompute colors once — used repeatedly across all display styles
  uint32_t c1 = Wheel(Settings.color_primary);
  uint32_t c2 = Wheel(Settings.color_secondary);
  uint32_t c3 = Wheel(Settings.color_tertiary);

  // non-blocking alternating flash at shift rpm
  if ( rpm >= Settings.shift_rpm ) {
    uint32_t cs1 = Wheel(Settings.color_shift_primary);
    uint32_t cs2 = Wheel(Settings.color_shift_secondary);
    unsigned long now = millis();
    if ( now - shiftFlashTime >= 50 ) {
      shiftFlashTime = now;
      shiftFlashState = !shiftFlashState;
      uint32_t color = shiftFlashState ? cs1 : cs2;
      for (i = 0; i < numPixels; i++) {
        strip.setPixelColor(i, color);
      }
      strip.show();
    }
    return;
  }

  // rpm range / number of dots = rpm per dot
  int dotSize = (Settings.shift_rpm - Settings.enable_rpm) / numPixels;
  int dotCount = constrain((rpm - Settings.enable_rpm) / dotSize, 0, numPixels - 1);

  switch (displayStyle) {
    case 0: // dot
      for (i = 0; i < numPixels; i++) {
        strip.setPixelColor(i, 0);
      }
      if ( dotCount < 12 ) {
        strip.setPixelColor(dotCount, c1);
      } else if ( dotCount < 20 ) {
        strip.setPixelColor(dotCount, c2);
      } else if ( dotCount < 24 ) {
        strip.setPixelColor(dotCount, c3);
      }
      strip.show();
      break;
    case 1: // bar
      for (i = 0; i < numPixels; i++) {
        strip.setPixelColor(i, 0);
      }
      for (i = 0; i < dotCount; i++) {
        if ( i < 12 ) {
          strip.setPixelColor(i, c1);
        } else if ( i < 20 ) {
          strip.setPixelColor(i, c2);
        } else {
          strip.setPixelColor(i, c3);
        }
      }
      strip.show();
      break;
    case 2: // center-out
      dotSize = (Settings.shift_rpm - Settings.enable_rpm) / (numPixels / 2);
      dotCount = ((rpm - Settings.enable_rpm) / dotSize) + 12;
      for (i = 0; i < numPixels; i++) {
        strip.setPixelColor(i, 0);
      }
      for (i = 12; i < dotCount; i++) {
        uint32_t color;
        if ( i < 17 ) {
          color = c1;
        } else if ( i < 21 ) {
          color = c2;
        } else {
          color = c3;
        }
        strip.setPixelColor(i, color);
        strip.setPixelColor(23 - i, color);
      }
      strip.show();
      break;
  }
}

void blackout(void) {
  for (int i = 0; i < strip.numPixels(); i++) {
    strip.setPixelColor(i, 0);
  }
  strip.show();
}

void rainbow(uint8_t wait) {
  uint16_t i, j;
  for (j = 0; j < 256; j++) {
    for (i = 0; i < strip.numPixels(); i++) {
      strip.setPixelColor(i, Wheel((i + j) & 255));
    }
    strip.show();
    delay(wait);
  }
}

// Input a value 0 to 255 to get a color value.
// The colours are a transition r - g - b - back to r.
uint32_t Wheel(byte WheelPos) {
  WheelPos = 255 - WheelPos;
  if (WheelPos < 6) {
    return strip.Color(255, 255, 255);
  } else if (WheelPos > 250) {
    return strip.Color(0, 0, 0);
  } else if (WheelPos < 85) {
    return strip.Color(255 - WheelPos * 3, 0, WheelPos * 3);
  } else if (WheelPos < 170) {
    WheelPos -= 85;
    return strip.Color(0, WheelPos * 3, 255 - WheelPos * 3);
  } else {
    WheelPos -= 170;
    return strip.Color(WheelPos * 3, 255 - WheelPos * 3, 0);
  }
}

//
// Quad Alphanumeric Display functions
//

void alpha4print( void ) {
  for (int i = 0; i < 4; i++ ) {
    alpha4.writeDigitAscii(i, displaybuffer[i]);
  }
  alpha4.writeDisplay();
  delay(25);
}

void alpha4DashChase() {
  // 16-bit digits in each alpha
  // 0 DP N M L K J H G2 G1 F E D C B A
  //
  //     A              -
  // F H J K B      | \ | / |
  //   G1 G2          -   -
  // E L M N C      | / | \ |
  //     D     DP       -     *
  //
  int buffer[2] = { 0x40, 0x80 };

  alpha4.clear();
  alpha4.writeDisplay();
  displaybuffer[0] = 0xC0;
  displaybuffer[1] = 0xC0;
  displaybuffer[2] = 0xC0;
  displaybuffer[3] = 0xC0;
  alpha4print();
  delay(100);

  for (int i = 0; i < 4; i++) {
    alpha4.writeDigitRaw(i, 0x00);
  }
  alpha4.writeDisplay();

  alpha4.clear();
  alpha4.writeDisplay();
  for (int i = 0; i < 4; i++) {
    for (int j = 0; j < 2; j++) {
      alpha4.writeDigitRaw(i, buffer[j]);
      alpha4.writeDisplay();
      delay(50);
      alpha4.writeDigitRaw(i, 0x00);
    }
  }
  alpha4.clear();
  alpha4.writeDisplay();
  for (int i = 3; i >= 0; i--) {
    for (int j = 1; j >= 0; j--) {
      alpha4.clear();
      alpha4.writeDigitRaw(i, buffer[j]);
      alpha4.writeDisplay();
      delay(50);
    }
  }

  alpha4.clear();
  alpha4.writeDisplay();
}

//
// Button handler
//
// States:
//   0: tach mode (menuState=false, setItemState=false)
//   1: tach + button press => menuState=true
//   2: menu + button press => setItemState=true
//   3: setItem + button press => setItemState=false
//
// Special: position 0 exits menu, position 11 saves and exits
//
void handleEvent(AceButton* /* button */, uint8_t eventType, uint8_t /* buttonState */) {
  switch (eventType) {
    case AceButton::kEventReleased:
      if ( menuState && setItemState ) {
        setItemState = false;
        if ( menuPos == 0 ) {
          menuState = false;
        } else if ( menuPos == 11 ) {
          menuState = false;
          saveConfig();
          rainbow(5);
          delay(100);
          blackout();
          break;
        }
      } else if ( menuState && !setItemState ) {
        setItemState = true;
      } else {
        menuState = true;
        lastMenuPos = -1; // force label redraw on menu entry
      }
      break;
  }
}


// rotate is called anytime the rotary inputs change state.
void rotate() {
  unsigned char result = rotary.process();
  if (result == DIR_CW) {
    clickCounter--;
    fakeRpmCounter -= 100;
  } else if (result == DIR_CCW) {
    clickCounter++;
    fakeRpmCounter += 100;
  }
}

void tachISR() {
  revs++;
}

bool loadConfig() {
  if ( verbose ) {
    delay(1000);
    Serial.println(F("read settings from EEPROM"));
  }
  strcpy(displaybuffer, "----");
  alpha4print();
  EEPROM.readBlock(configAddress, Settings);
  if ( Settings.enable_rpm > 9000 ) // if unset this will be max unsigned int
    Settings.enable_rpm = 6500;
  if ( Settings.shift_rpm > 9000 )
    Settings.shift_rpm = 7500;
  if ( Settings.enable_rpm >= Settings.shift_rpm ) // prevent division by zero in lightItUp
    Settings.enable_rpm = Settings.shift_rpm - 500;
  if ( verbose ) {
    Serial.print(F(" brt: ")); Serial.print(Settings.brightness, HEX);
    Serial.print(F(" enab: ")); Serial.print(Settings.enable_rpm);
    Serial.print(F(" shft: ")); Serial.print(Settings.shift_rpm);
    Serial.print(F(" col1: ")); Serial.print(Settings.color_primary, HEX);
    Serial.print(F(" col2: ")); Serial.print(Settings.color_secondary, HEX);
    Serial.print(F(" col3: ")); Serial.print(Settings.color_tertiary, HEX);
    Serial.print(F(" sft1: ")); Serial.print(Settings.color_shift_primary, HEX);
    Serial.print(F(" sft2: ")); Serial.println(Settings.color_shift_secondary, HEX);
  }
  strcpy(displaybuffer, "____");
  alpha4print();
  strip.setBrightness(Settings.brightness);
  return strncmp(Settings.version, CONFIG_VERSION, sizeof(Settings.version)) == 0;
}

void saveConfig() {
  if ( verbose )
    Serial.println(F("write settings to EEPROM"));
  strcpy(displaybuffer, "____");
  alpha4print();
  EEPROM.writeBlock(configAddress, Settings);
  if ( verbose ) {
    delay(1000);
    loadConfig();
    delay(1000);
  }
  strcpy(displaybuffer, "----");
  alpha4print();
}
