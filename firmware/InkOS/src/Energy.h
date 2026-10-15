#pragma once
#include "Domain.h"
namespace ink {
time_t parsePriceTimestamp(const char *);
bool parseEnergyPrices(const char *, Energy &, time_t now, int provider = 2);
const PriceHour *priceAt(const Energy &, time_t);
double totalPriceCt(const Energy &, time_t);
void loadEnergy();
void updateEnergy();
} // namespace ink
