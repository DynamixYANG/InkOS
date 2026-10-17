#pragma once
#include <string>
#include <cstring>
#include <cstdint>
#include <cstdlib>
struct FakeSerial { std::string out; int room=4096; int availableForWrite(){return room;} size_t write(const uint8_t*p,size_t n){out.append((const char*)p,n);return n;} };
extern FakeSerial Serial;
typedef int portMUX_TYPE;
#define portMUX_INITIALIZER_UNLOCKED 0
#define portENTER_CRITICAL(x) (void)0
#define portEXIT_CRITICAL(x) (void)0
