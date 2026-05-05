// magic vars
bool ok = true;
bool verbose = true;
bool smoothing = false;
bool tachDisplay = true;

// eeprom
#include <EEPROMex.h>
#define CONFIG_VERSION "ls1"
#define MEMORY_BASE 32
int configAddress = 0;

// Thank you AdaFruit!
// neopixel
#include <Adafruit_NeoPixel.h>
#include <avr/power.h>
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

//SettingsStruct Settings;
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
  //unsigned int mySettings[9];
} Settings = {
  CONFIG_VERSION,
  0x7F, 0xDAC, 0x1D4C, 0x50, 0x38, 0xC, 0xFC, 0xE0
  // 127,  3500,   7500, colors
};

#define NEO_PIN 5
int numPixels = 24;
// Parameter 1 = number of pixels in strip
// Parameter 2 = Arduino pin number (most are valid)
// Parameter 3 = pixel type flags, add together as needed:
//   NEO_KHZ800  800 KHz bitstream (most NeoPixel products w/WS2812 LEDs)
//   NEO_GRB     Pixels are wired for GRB bitstream (most NeoPixel products)
Adafruit_NeoPixel strip = Adafruit_NeoPixel(numPixels, NEO_PIN, NEO_GRB + NEO_KHZ800);

// IMPORTANT: To reduce NeoPixel burnout risk, add 1000 uF capacitor across
// pixel power leads, add 300 - 500 Ohm resistor on first pixel's data input
// and minimize distance between Arduino and first pixel.  Avoid connecting
// on a live circuit...if you must, connect GND first.

// alpha4 is on I2C
Adafruit_AlphaNum4 alpha4 = Adafruit_AlphaNum4();
//char displaybuffer[5] = {' ', ' ', ' ', ' ', ' '};
char displaybuffer[5] = "0000";

// rotary encoder
// outputs on pins 0 and 1
Rotary rotary = Rotary(0, 1);
volatile int clickCounter = 0;
int clickCounterLast = 0;
volatile unsigned int fakeRpmCounter = 0;
unsigned int fakeRpmCounterLast = 0;

// button on rotary encoder
#define buttonPin 8
AceButton button(buttonPin);
void handleEvent(AceButton*, uint8_t, uint8_t);

// stuff for rotary encoder on interrupts
#define encoder0PinA 0 // int 2 is pin 0 in micro
#define encoder0PinB 1 // int 3 is pin 1 in micro
/*
   volatile int encoder0Pos = 0;
   volatile int lastencoder0Pos = 0;
   #define readA bitRead(PIND,2)//faster than digitalRead()
   #define readB bitRead(PIND,3)//faster than digitalRead()
*/

// menu state machine
boolean menuState = false;
boolean lastMenuState = false;
volatile int menuPos = 0;
volatile int lastmenuPos;
boolean setItemState = false;
boolean lastSetItemState = false;
volatile int setItemPos = 0;

//configuration for the Tachometer variables
#define tachPin 7 // int 3 is pin 7 in micro

byte rpmArraySize = 3;  // 3 values
unsigned int rpmArray[4];
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


/*

███████╗███████╗████████╗██╗   ██╗██████╗ 
██╔════╝██╔════╝╚══██╔══╝██║   ██║██╔══██╗
███████╗█████╗     ██║   ██║   ██║██████╔╝
╚════██║██╔══╝     ██║   ██║   ██║██╔═══╝ 
███████║███████╗   ██║   ╚██████╔╝██║     
╚══════╝╚══════╝   ╚═╝    ╚═════╝ ╚═╝     
                                          
 */
void setup() {
  Serial.begin(115200);

  if ( verbose ) {
    delay(1000);
    Serial.println("hello world");
    delay(1000);
  }

  EEPROM.setMemPool(MEMORY_BASE, EEPROMSizeMicro); // set memorypool base to 32, micro board
  configAddress = EEPROM.getAddress(sizeof(SettingsStruct)); // size of config object
  ok = loadConfig();

  // quadalpha setup

  alpha4.begin(0x70);  // pass in the address
  alpha4.setBrightness(Settings.brightness / 16);
  //pinMode(ALPHAPWM, OUTPUT);
  //analogWrite(ALPHAPWM, 255);
  //String("XXXX").toCharArray(displaybuffer,4);
  //alpha4print();
  //for (int i = 0; i < 255; i++ ) {
  //  Serial.println("fadein: " + String(i));
  //  analogWrite(ALPHAPWM, i);
  //  delay(10);
  //}
  alpha4DashChase();
  String("boot").toCharArray(displaybuffer, 5);
  alpha4print();
  delay(100);

  // neopixel setup
  strip.begin();
  strip.show(); // Initialize all pixels to 'off'
  rainbow(5);
  //rainbowCycle(5);
  delay(100);
  blackout();

  alpha4.clear();
  alpha4.writeDisplay();

  // display every character,
  //for (uint8_t i='!'; i<='z'; i++) {
  //  alpha4.writeDigitAscii(0, i);
  //  alpha4.writeDigitAscii(1, i+1);
  //  alpha4.writeDigitAscii(2, i+2);
  //  alpha4.writeDigitAscii(3, i+3);
  //  alpha4.writeDisplay();

  //  delay(300);
  //}

  //  String("Tach").toCharArray(displaybuffer,5);
  //  alpha4print();
  //  delay(10);

  // button setup
  //attachInterrupt(digitalPinToInterrupt(buttonPin), button, RISING);
  pinMode(buttonPin, INPUT_PULLUP);
  button.setEventHandler(handleEvent);

  // rotary encoder setup
  pinMode(encoder0PinA, INPUT_PULLUP);
  pinMode(encoder0PinB, INPUT_PULLUP);
  pinMode(tachPin, INPUT_PULLUP);
  attachInterrupt(digitalPinToInterrupt(encoder0PinA), rotate, CHANGE);
  attachInterrupt(digitalPinToInterrupt(encoder0PinB), rotate, CHANGE);
  attachInterrupt(digitalPinToInterrupt(tachPin), tachISR, RISING);

  // init rpm array to zeros
  for (rpmArrayIdx = 0; rpmArrayIdx < rpmArraySize; rpmArrayIdx++) {
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

  if ( fakeRPM == false ) {
    nowTime = micros();                     //  0.000001
    if ( nowTime - lastTime > 500000 ) {    // instead of using delay 500msec
      detachInterrupt(digitalPinToInterrupt(tachPin));
      elapsedTime = nowTime - lastTime; 
      lastTime = nowTime;
              //   secs/min microsec/sec
      rpm = (unsigned long)revs * 60000000UL / elapsedTime / cylinderDivider;
      rpmArray[rpmIdx] = rpm;
      rpmIdx++;
      if ( rpmIdx > 2 ) {
          rpmIdx = 0;
      }
      revs = 0;
      attachInterrupt(digitalPinToInterrupt(tachPin), tachISR, RISING);
      rpmTotal = 0;
      for (rpmArrayIdx = 0; rpmArrayIdx < rpmArraySize; rpmArrayIdx++) {
        rpmTotal += rpmArray[rpmArrayIdx];
      }
      rpm = rpmTotal / rpmArraySize;
    }
  } else {
      if ( fakeRpmCounter > 8000 ) {
          fakeRpmCounter = 8000; 
      }
      if ( fakeRpmCounter < 500 ) {
          fakeRpmCounter = 500;
      }
      rpm = fakeRpmCounter;
  }

  if ( verbose )
    Serial.println("revs: " + String(revs) + " rpm: " + String(rpm) + " now time: " + String(nowTime) + " last time: " + String(lastTime) + " elapsed time: " + String(elapsedTime));
  
  // THIS CALL PUSHES THE RPM VALUE TO THE DISPLAYS
  lightItUp(); // use the value of RPM to light up the neopixel bar

  // polling for a button press
  button.check();
  if ( !menuState ) {
    tachDisplay = true;

    Serial.println("revs: " + String(revs) + " rpm: " + String(rpm));
    snprintf(displaybuffer, sizeof(displaybuffer), "%04d", rpm);
    alpha4print();
    //delay(250);

  } else if ( menuState && !setItemState ) { // HIGH LOW
    // scroll through menu items
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
      Serial.print("encoder: " + String(clickCounter) + " last encoder: " + String(clickCounterLast) + " menu: " + String(menuPos));
      Serial.println(" menu: " + String(menuState) + " setItem: " + String(setItemState));
    }
  } else if ( menuState && setItemState ) { // HIGH HIGH
    // scroll through menu item settings
    // just realized they need to be tracked independently
    // RPM scale from 0 to 8000
    // ENABLE and SHIFT
    // Color can be from 0x00 00 00 to 0xFF FF FF
    // Wheel() can be 0x00 to 0xFF...
    switch (menuPos) {
      case 1: // brightness and colors
        // maintain absolute range
        // usable range seems to be more like 0x2F is super dim
        // and 0x6B is bright, but not painfully so
        // and of course anything above that is silly

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

        //analogWrite(ALPHAPWM, mySettings[SHIFTLIGHT_BRIGHTNESS]);
        alpha4.setBrightness(Settings.brightness / 16);
        //String("%04d",Settings.brightness).toCharArray(displaybuffer,5);
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
        //String(Settings.enable_rpm).toCharArray(displaybuffer,5);
        snprintf(displaybuffer, sizeof(displaybuffer), "%04d", Settings.enable_rpm);
        alpha4print();
        break;
      case 3: // shift rpm
        if ( clickCounter > clickCounterLast && Settings.shift_rpm <= 9000 ) {
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
        //if ( Settings.brightness < 8 ) { Settings.brightness = 252; }
        //if ( Settings.brightness > 252 ) { Settings.brightness = 8; }
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
        //if ( Settings.brightness < 8 ) { Settings.brightness = 252; }
        //if ( Settings.brightness > 252 ) { Settings.brightness = 8; }
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
        //if ( Settings.brightness < 8 ) { Settings.brightness = 252; }
        //if ( Settings.brightness > 252 ) { Settings.brightness = 8; }
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
        //if ( Settings.brightness < 8 ) { Settings.brightness = 252; }
        //if ( Settings.brightness > 252 ) { Settings.brightness = 8; }
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
          String("YeP ").toCharArray(displaybuffer, 5);
          alpha4print();
          clickCounterLast = clickCounter;
        } else if (clickCounter < clickCounterLast ) {
          fakeRPM = false;
          String("NoPe").toCharArray(displaybuffer, 5);
          alpha4print();
          clickCounterLast = clickCounter;
        }
        break;
      case 10: // display style
        if ( (clickCounter < clickCounterLast) && (displayStyle==1) ) {
          displayStyle = 0;
          String("Dot ").toCharArray(displaybuffer, 5);
          alpha4print();
          clickCounterLast = clickCounter;
        }
        if ( ((clickCounter > clickCounterLast) && (displayStyle==0)) || ((clickCounter < clickCounterLast) && (displayStyle==2)) ) {
          displayStyle = 1;
          String("Bar ").toCharArray(displaybuffer, 5);
          alpha4print();
          clickCounterLast = clickCounter;
        }
        if ( (clickCounter > clickCounterLast) && (displayStyle==1) ) {
          displayStyle = 2;
          String("CinO").toCharArray(displaybuffer, 5);
          alpha4print();
          clickCounterLast = clickCounter;
        }
        break;
      default: // brightness and colors
        clickCounterLast = clickCounter;
        break;
    }

    if ( verbose ) {
      Serial.print("encoder: " + String(clickCounter) + " last encoder: " + String(clickCounterLast) + " menu: " + String(menuPos));
      Serial.print(" menu: " + String(menuState) + " setItem: " + String(setItemState));
      Serial.print(" brt: " + String(Settings.brightness, HEX) +
                   " enab: " + String(Settings.enable_rpm, DEC) +
                   " shft: " + String(Settings.shift_rpm, DEC) +
                   " col1: " + String(Settings.color_primary, HEX));
      Serial.println( " col2: " + String(Settings.color_secondary, HEX) +
                      " col3: " + String(Settings.color_tertiary, HEX) +
                      " sft1: " + String(Settings.color_shift_primary, HEX) +
                      " sft2: " + String(Settings.color_shift_secondary, HEX));
    }
  } else {
    if ( verbose ) {
      Serial.print("encoder: " + String(clickCounter) + " last encoder: " + String(clickCounterLast) + " menu: " + String(menuPos));
      Serial.println(" menu: " + String(menuState) + " setItem: " + String(setItemState));
    }
  }

  if ( menuState && !setItemState ) {
    tachDisplay = false;
    switch (menuPos) {
      case 0:
        //displaybuffer[0] = 0x39;
        //displaybuffer[1] = 0x9;
        //displaybuffer[2] = 0x9;
        //displaybuffer[3] = 0xF;
        //alpha4printraw();
        //blackout();
        String("EXIT").toCharArray(displaybuffer, 5);
        alpha4print();
        blackout();
        break;
      case 1:
        String("Brt ").toCharArray(displaybuffer, 5);
        alpha4print();
        /*
          menustring = String("Brightness");
          menustring.toCharArray(longdisplaybuffer,menustring.length());
          alpha4printscroll();
        */
        break;
      case 2:
        String("Enbl").toCharArray(displaybuffer, 5);
        alpha4print();
        break;
      case 3:
        String("Shft").toCharArray(displaybuffer, 5);
        alpha4print();
        break;
      case 4:
        String("Col1").toCharArray(displaybuffer, 5);
        alpha4print();
        break;
      case 5:
        String("Col2").toCharArray(displaybuffer, 5);
        alpha4print();
        break;
      case 6:
        String("Col3").toCharArray(displaybuffer, 5);
        alpha4print();
        break;
      case 7:
        String("SC 1").toCharArray(displaybuffer, 5);
        alpha4print();
        break;
      case 8:
        String("SC 2").toCharArray(displaybuffer, 5);
        alpha4print();
        break;
      case 9:
        String("Fake").toCharArray(displaybuffer, 5);
        alpha4print();
        break;
      case 10:
        String("StYl").toCharArray(displaybuffer, 5);
        alpha4print();
        break;
      case 11:
        String("SAVE").toCharArray(displaybuffer, 5);
        alpha4print();
        blackout();
        break;
    }
  }
}

// Fill the dots one after the other with a color
void colorWipe(uint32_t c, uint8_t wait) {
  for (uint16_t i = 0; i < strip.numPixels(); i++) {
    strip.setPixelColor(i, c);
    strip.show();
    delay(wait);
  }
}

// flood all pixels in range with brightness
void colorFill(uint32_t c, uint8_t brightness) {
  for (uint16_t i = 0; i < strip.numPixels(); i++) {
    strip.setPixelColor(i, c);
  }
  strip.setBrightness(brightness);
  strip.show();
}

void lightItUp(void) {
  int pixelCount = 0;
  int i = 0;
  // don't do anything
  if ( tachDisplay == false ) {
    return;
  }
  if ( rpm < Settings.enable_rpm ) {
    for (i = 0; i < strip.numPixels(); i++) {
      strip.setPixelColor(i, 0);
    }
    strip.show();
    return;
  }
  // flash if we are past the configured shift rpm
  if ( rpm >= Settings.shift_rpm ) {
    for (i = 0; i < numPixels; i++) {
      strip.setPixelColor(i, 0);
    }
    strip.show();
    for (i = 0; i < numPixels; i++) {
      strip.setPixelColor(i, Wheel(Settings.color_shift_primary));
    }
    strip.show();
    delay(50);
    for (i = 0; i < numPixels; i++) {
      strip.setPixelColor(i, Wheel(Settings.color_shift_secondary));
    }
    strip.show();
    return;
  }


  // one routine to wipe left to right
  // max rpm - low rpm = rpm range
  // rpm range / number of dots to get #rpm per dot
  
  int dotSize = (Settings.shift_rpm - Settings.enable_rpm) / numPixels;
  int dotCount = (rpm - Settings.enable_rpm) / dotSize; // 1 to 32

  switch (displayStyle) {
    case 0:
      for (i = 0; i < numPixels; i++) {
        strip.setPixelColor(i, 0);
      }
      /*  this is for 32 pixels
      if ( dotCount < 16 ) {
        strip.setPixelColor(dotCount, Wheel(Settings.color_primary));
      }
      if ( (dotCount < 24) && (dotCount >= 16) ) {
        strip.setPixelColor(dotCount, Wheel(Settings.color_secondary));
      }
      if ( (dotCount < 32) && (dotCount >= 24) ) {
        strip.setPixelColor(dotCount, Wheel(Settings.color_tertiary));
      }
      */
      if ( dotCount < 12 ) {
        strip.setPixelColor(dotCount, Wheel(Settings.color_primary));
      }
      if ( (dotCount < 20) && (dotCount >= 12) ) {
        strip.setPixelColor(dotCount, Wheel(Settings.color_secondary));
      }
      if ( (dotCount < 24) && (dotCount >= 20) ) {
        strip.setPixelColor(dotCount, Wheel(Settings.color_tertiary));
      }
      strip.show();
      break;
    case 1:
      for (i = 0; i < numPixels; i++) {
        strip.setPixelColor(i, 0);
      }
      /* for 32 pixels
      for (i = 0; i < dotCount; i++) {
        if ( i < 16 ) {
          strip.setPixelColor(i, Wheel(Settings.color_primary));
        }
        if ( (i < 24) && (i >= 16) ) {
          strip.setPixelColor(i, Wheel(Settings.color_secondary));
        }
        if ( (i <= 32) && (i >= 24) ) {
          strip.setPixelColor(i, Wheel(Settings.color_tertiary));
        }
      }
      */
      for (i = 0; i < dotCount; i++) {
        if ( i < 12 ) {
          strip.setPixelColor(i, Wheel(Settings.color_primary));
        }
        if ( (i < 20) && (i >= 12) ) {
          strip.setPixelColor(i, Wheel(Settings.color_secondary));
        }
        if ( (i <= 24) && (i >= 20) ) {
          strip.setPixelColor(i, Wheel(Settings.color_tertiary));
        }
      }
      strip.show();
      break;
    case 2:
      dotSize = (Settings.shift_rpm - Settings.enable_rpm) / (numPixels / 2);
      //dotCount = ((rpm - Settings.enable_rpm) / dotSize) + 16; // 16 to 32
      dotCount = ((rpm - Settings.enable_rpm) / dotSize) + 12; // 12 to 24
      for (i = 0; i < numPixels; i++) {
        strip.setPixelColor(i, 0);
      }
      /*
      for (i = 16; i < dotCount; i++) {
        if ( i < 24 ) {
          strip.setPixelColor(i, Wheel(Settings.color_primary));
          strip.setPixelColor(31-i, Wheel(Settings.color_primary));
        }
        if ( (i < 28) && (i >= 24) ) {
          strip.setPixelColor(i, Wheel(Settings.color_secondary));
          strip.setPixelColor(31-i, Wheel(Settings.color_secondary));
        }
        if ( (i <= 32) && (i >= 28) ) {
          strip.setPixelColor(i, Wheel(Settings.color_tertiary));
          strip.setPixelColor(31-i, Wheel(Settings.color_tertiary));
        }
      }
      */
      for (i = 12; i < dotCount; i++) {
        if ( i < 17 ) {
          strip.setPixelColor(i, Wheel(Settings.color_primary));
          strip.setPixelColor(23-i, Wheel(Settings.color_primary));
        }
        if ( (i < 21) && (i >= 17) ) {
          strip.setPixelColor(i, Wheel(Settings.color_secondary));
          strip.setPixelColor(23-i, Wheel(Settings.color_secondary));
        }
        if ( (i <= 24) && (i >= 21) ) {
          strip.setPixelColor(i, Wheel(Settings.color_tertiary));
          strip.setPixelColor(23-i, Wheel(Settings.color_tertiary));
        }
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
  strip.setBrightness(32);
  for (j = 0; j < 256; j++) {
    for (i = 0; i < strip.numPixels(); i++) {
      strip.setPixelColor(i, Wheel((i + j) & 255));
    }
    strip.show();
    delay(wait);
  }
}

// Slightly different, this makes the rainbow equally distributed throughout
void rainbowCycle(uint8_t wait) {
  uint16_t i, j;

  //for(j=0; j<256*2; j++) { // 2 cycles of all colors on wheel, was 5
  for (j = 0; j < 250 * 2; j++) { // 2 cycles of all colors on wheel, was 5
    for (i = 0; i < strip.numPixels(); i++) {
      strip.setPixelColor(i, Wheel(((i * 256 / strip.numPixels()) + j) & 255));
      strip.setBrightness( (j / 5 / 16) + 64);
    }
    strip.show();
    delay(wait);
  }
}

//Theatre-style crawling lights.
void theaterChase(uint32_t c, uint8_t wait) {
  for (int j = 0; j < 10; j++) { //do 10 cycles of chasing
    for (int q = 0; q < 3; q++) {
      for (int i = 0; i < strip.numPixels(); i = i + 3) {
        strip.setPixelColor(i + q, c);  //turn every third pixel on
      }
      strip.show();

      delay(wait);

      for (int i = 0; i < strip.numPixels(); i = i + 3) {
        strip.setPixelColor(i + q, 0);      //turn every third pixel off
      }
    }
  }
}

//Theatre-style crawling lights with rainbow effect
void theaterChaseRainbow(uint8_t wait) {
  for (int j = 0; j < 256; j++) {   // cycle all 256 colors in the wheel
    for (int q = 0; q < 3; q++) {
      for (int i = 0; i < strip.numPixels(); i = i + 3) {
        strip.setPixelColor(i + q, Wheel( (i + j) % 255)); //turn every third pixel on
      }
      strip.show();

      delay(wait);

      for (int i = 0; i < strip.numPixels(); i = i + 3) {
        strip.setPixelColor(i + q, 0);      //turn every third pixel off
      }
    }
  }
}

// Input a value 0 to 255 to get a color value.
// The colours are a transition r - g - b - back to r.
uint32_t Wheel(byte WheelPos) {

  WheelPos = 255 - WheelPos; // why do we invert WheelPos?
  if (WheelPos < 6 ) {
    return strip.Color(255, 255, 255);
  } else if (WheelPos > 250) {
    return strip.Color(0, 0, 0);
  } else if (WheelPos < 85) {
    // full red,    no green,     blue
    return strip.Color(255 - WheelPos * 3, 0, WheelPos * 3);
  } else if (WheelPos < 170) {
    WheelPos -= 85;
    // no red, full green,  blue
    return strip.Color(0, WheelPos * 3, 255 - WheelPos * 3);
  } else { // blue
    WheelPos -= 170;
    // full red, green, no blue
    return strip.Color(WheelPos * 3, 255 - WheelPos * 3, 0);
  }


  // 4 blocks, 64
  //WheelPos = 255 - WheelPos;
  //if(WheelPos < 64) {
  //  return strip.Color(255 - WheelPos * 4, 0, WheelPos * 4);
  //} else if(WheelPos < 128) {
  //  WheelPos -= 64;
  //  return strip.Color(0, WheelPos * 4, 255 - WheelPos * 4);
  //} else if(WheelPos < 192) {
  //  WheelPos -= 128;
  //  return strip.Color(WheelPos * 4, 255 - WheelPos * 4, 0);
  //} else {
  //  WheelPos -= 192;
  //  return strip.Color(WheelPos * 4, WheelPos * 4, WheelPos * 4);
  //}
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

void alpha4printraw( void ) {
  for (int i = 0; i < 4; i++ ) {
    alpha4.writeDigitRaw(i, displaybuffer[i]);
  }
  alpha4.writeDisplay();
  delay(25);
}

/*
  void alpha4printscroll( void ) {
  // scroll print the long string
  // finish by leaving the static short string
  // we need to know if we printed the long string or not though
  // and only print the scrolly bit when we need to
  String("     ").toCharArray(displaybuffer,4);
  alpha4print();
  for (int i = 0; i < menustring.length(); i++) {
    displaybuffer[0] = displaybuffer[1];
    displaybuffer[1] = displaybuffer[2];
    displaybuffer[2] = displaybuffer[3];
    displaybuffer[3] = menustring[i];
    alpha4print();
    delay(250);
  }
  delay(500);
  }
*/

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

  //Serial.println("clear, then write all dashes");

  alpha4.clear();  // clear display first
  alpha4.writeDisplay();
  displaybuffer[0] = 0xC0;
  displaybuffer[1] = 0xC0;
  displaybuffer[2] = 0xC0;
  displaybuffer[3] = 0xC0;
  alpha4print();
  delay(100);

  //Serial.println("clear to prep for animated dash chase");
  for (int i = 0; i < 4; i++) {
    alpha4.writeDigitRaw(i, 0x00);
  }
  alpha4.writeDisplay();

  for (int k = 0; k < 1; k++) {
    alpha4.clear();  // clear display first
    alpha4.writeDisplay();

    for (int i = 0; i < 4; i++) {
      for (int j = 0; j < 2; j++) {
        alpha4.writeDigitRaw(i, buffer[j]);
        alpha4.writeDisplay();
        delay(50);
        //Serial.println("dash chase i: " + String(i) + " j: " + String(j) + " digit: " + String(buffer[j],HEX));
        alpha4.writeDigitRaw(i, 0x00);
      }
    }
    alpha4.clear();  // clear display
    alpha4.writeDisplay();
    for (int i = 3; i >= 0; i--) {
      for (int j = 1; j >= 0; j--) {
        alpha4.clear();
        alpha4.writeDigitRaw(i, buffer[j]);
        alpha4.writeDisplay();
        delay(50);
        //Serial.println("dash chase i: " + String(i) + " j: " + String(j) + " digit: " + String(buffer[j],HEX));
      }
    }
  }

  alpha4.clear();
  alpha4.writeDisplay();
  return;
}

//
// Button function
//
void handleEvent(AceButton* /* button */, uint8_t eventType, uint8_t /* buttonState */) {
  switch (eventType) {
    case AceButton::kEventReleased:
      // basically you can either be in the top level menu that lists your parameters
      // or you can be in the menu for one of the parameters
      // state 0: tach mode menuState false, setItemState should also be false
      // state 1: tach mode + button press => menuState true (in the menu system)
      // state 2: in menu + button press => setItemState true (configuring an item)
      // state 3: in setItemState + button press => setItemState false

      // special condition for state 2, postion 0 just reset states to false (exit)
      // special condition for state 2, position 10 reset states to false, save config, exit

      if ( menuState && setItemState ) { // in menu and setting
        // button press here should write to EEPROM
        // then move back to menu item selection
        lastSetItemState = setItemState;
        setItemState = false;
        // special if we are at the beginning or end
        if ( menuPos == 0 ) { // exit
          menuState = false;
        } else if ( menuPos == 11 ) { // save and exit
          menuState = false;
          saveConfig();
          rainbow(5);
          delay(100);
          blackout();
          break;
        }
      } else if ( menuState && !setItemState ) { // in menu, not setting a param yet
        // we are at a menu item that we want to set
        lastSetItemState = setItemState;
        setItemState = true;
      } else {
        lastMenuState = menuState;
        menuState = true;
      }
      break;
  }
}


// rotate is called anytime the rotary inputs change state.
void rotate() {
  unsigned char result = rotary.process();
  if (result == DIR_CW) {
    clickCounter--;
    fakeRpmCounter-=100;
  } else if (result == DIR_CCW) {
    clickCounter++;
    fakeRpmCounter+=100;
  }
}

void tachISR() {
  revs++;
}

bool loadConfig() {
  if ( verbose ) {
    delay(1000);
    Serial.println("      read settings from EEPROM    ");
  }
  String("----").toCharArray(displaybuffer, 5);
  alpha4print();
  EEPROM.readBlock(configAddress, Settings);
  if ( Settings.enable_rpm > 9000 ) // insurance. if unset this will be max unsigned int
    Settings.enable_rpm = 6500;
  if ( Settings.shift_rpm > 9000 )
    Settings.shift_rpm = 7500;
  if ( verbose ) {
    //    Serial.print("encoder: " + String(encoder0Pos) + " last encoder: " + String(lastencoder0Pos) + " menu: " + String(menuPos));
    Serial.print(" menu: " + String(menuPos));
    Serial.print(" menu: " + String(menuState) + " setItem: " + String(setItemState));
    Serial.print(" brt: " + String(Settings.brightness, HEX) +
                 " enab: " + String(Settings.enable_rpm, DEC) +
                 " shft: " + String(Settings.shift_rpm, DEC) +
                 " col1: " + String(Settings.color_primary, HEX));
    Serial.println( " col2: " + String(Settings.color_secondary, HEX) +
                    " col3: " + String(Settings.color_tertiary, HEX) +
                    " sft1: " + String(Settings.color_shift_primary, HEX) +
                    " sft2: " + String(Settings.color_shift_secondary, HEX));
  }
  String("____").toCharArray(displaybuffer, 5);
  alpha4print();
  return strncmp(Settings.version, CONFIG_VERSION, sizeof(Settings.version)) == 0;
}

void saveConfig() {
  if ( verbose )
    Serial.println("      write settings to EEPROM     ");
  String("____").toCharArray(displaybuffer, 5);
  alpha4print();
  EEPROM.writeBlock(configAddress, Settings);
  if ( verbose ) {
    delay(1000);
    ok = loadConfig();
    delay(1000);
  }
  String("----").toCharArray(displaybuffer, 5);
  alpha4print();
}
