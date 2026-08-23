#ifndef ARDUINO_H_STUB
#define ARDUINO_H_STUB
#include <stdint.h>
#include <stddef.h>
#include <string.h>
#include <stdio.h>
#include <string>

#define HIGH 1
#define LOW 0
#define INPUT 0
#define OUTPUT 1
#define INPUT_PULLUP 2
#define PROGMEM
#define DEC 10
#define HEX 16
typedef bool boolean;
typedef uint8_t byte;

class __FlashStringHelper;
#define F(x) (reinterpret_cast<const __FlashStringHelper*>(x))
#define PSTR(x) x
inline uint16_t pgm_read_word(const void* p){ uint16_t v; memcpy(&v,p,2); return v; }
inline uint8_t  pgm_read_byte(const void* p){ return *(const uint8_t*)p; }
inline size_t strlen_P(const char* s){ return strlen(s); }

extern unsigned long g_ms;
extern unsigned long g_us;
unsigned long millis();
unsigned long micros();
void delay(unsigned long);
void delayMicroseconds(unsigned int);
void pinMode(uint8_t, uint8_t);
void digitalWrite(uint8_t, uint8_t);
int  digitalRead(uint8_t);
int  analogRead(uint8_t);
void analogWrite(uint8_t, int);
unsigned long pulseIn(uint8_t, uint8_t, unsigned long t=1000000UL);
#define max(a,b) ((a)>(b)?(a):(b))
#define min(a,b) ((a)<(b)?(a):(b))
#define abs(x) ((x)>0?(x):-(x))
#define constrain(x,l,h) ((x)<(l)?(l):((x)>(h)?(h):(x)))

typedef std::string String;

class Print {
 public:
  virtual size_t write(uint8_t) { return 1; }
  size_t write(const uint8_t* b, size_t n){ (void)b; return n; }
  size_t print(const char* s){ fputs(s, stdout); return strlen(s);}
  size_t print(const __FlashStringHelper* s){ return print((const char*)s); }
  size_t print(char c){ fputc(c, stdout); return 1; }
  size_t print(int v, int b=DEC){ (void)b; printf("%d",v); return 1; }
  size_t print(unsigned int v, int b=DEC){ (void)b; printf("%u",v); return 1; }
  size_t print(long v, int b=DEC){ (void)b; printf("%ld",v); return 1; }
  size_t print(unsigned long v, int b=DEC){ (void)b; printf("%lu",v); return 1; }
  size_t print(double v, int d=2){ (void)d; printf("%f",v); return 1; }
  size_t print(const String& s){ return print(s.c_str()); }
  template<class T> size_t println(T v){ size_t n=print(v); fputc('\n',stdout); return n+1; }
  template<class T> size_t println(T v,int b){ size_t n=print(v,b); fputc('\n',stdout); return n+1; }
  size_t println(){ fputc('\n',stdout); return 1; }
};

class Stream : public Print {
 public:
  virtual int available(){ return 0; }
  virtual int read(){ return -1; }
  virtual int peek(){ return -1; }
  void flush(){}
};

class HardwareSerial : public Stream {
 public:
  void begin(unsigned long){}
  operator bool() const { return true; }
};
extern HardwareSerial Serial;
#endif
