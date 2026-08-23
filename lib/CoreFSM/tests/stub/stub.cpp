#include "Arduino.h"
HardwareSerial Serial;
unsigned long g_ms = 0;
unsigned long millis(){ return g_ms; }
unsigned long g_us = 0;
unsigned long micros(){ return g_us; }
void delay(unsigned long){} void delayMicroseconds(unsigned int){}
void pinMode(uint8_t,uint8_t){} void digitalWrite(uint8_t,uint8_t){}
int digitalRead(uint8_t){return 1;} int analogRead(uint8_t){return 0;} void analogWrite(uint8_t,int){}

unsigned long pulseIn(uint8_t,uint8_t,unsigned long){return 0;}
