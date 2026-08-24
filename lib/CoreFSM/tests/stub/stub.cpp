#include "Arduino.h"
HardwareSerial Serial;
unsigned long g_ms = 0;
unsigned long millis(){ return g_ms; }
unsigned long g_us = 0;
unsigned long micros(){ return g_us; }
uint8_t g_pinModes[256] = {0};
uint8_t g_digitalLevels[256] = {0};
int g_analogLevels[256] = {0};
int g_pwmLevels[256] = {0};
void resetArduinoStub(){
  g_ms = g_us = 0;
  memset(g_pinModes, 0, sizeof(g_pinModes));
  memset(g_digitalLevels, 0, sizeof(g_digitalLevels));
  memset(g_analogLevels, 0, sizeof(g_analogLevels));
  memset(g_pwmLevels, 0, sizeof(g_pwmLevels));
}
void delay(unsigned long){} void delayMicroseconds(unsigned int){}
void pinMode(uint8_t p,uint8_t m){g_pinModes[p]=m;}
void digitalWrite(uint8_t p,uint8_t v){g_digitalLevels[p]=v;}
int digitalRead(uint8_t p){return g_digitalLevels[p];}
int analogRead(uint8_t p){return g_analogLevels[p];}
void analogWrite(uint8_t p,int v){g_pwmLevels[p]=v;}

unsigned long pulseIn(uint8_t,uint8_t,unsigned long){return 0;}
