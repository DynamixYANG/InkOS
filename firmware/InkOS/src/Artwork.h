#pragma once
#include "Domain.h"
namespace ink {
bool decodeArtwork(const uint8_t *jpeg, size_t length, uint8_t *monochrome);
}
