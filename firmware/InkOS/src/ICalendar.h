#pragma once
#include "Domain.h"
namespace ink {
bool parseCalendar(const char *ics, const char *floatingZone, time_t begin, time_t end,
                   Agenda &out);
}
