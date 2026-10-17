#pragma once
#include <stddef.h>
#include <stdint.h>
namespace ink {
uint32_t decodeUtf8(const char *&text);
void copyUtf8(char *out, const char *text, size_t capacity);
}
