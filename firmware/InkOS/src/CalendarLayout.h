#pragma once
#include "Domain.h"
namespace ink {
struct IsoWeek { int year, number; };
IsoWeek isoWeek(const tm &localDate);
time_t agendaFirstDay(time_t now, const char *zone);
namespace weekgrid {
constexpr int Left = 56, Right = 787, Top = 180, Bottom = 452;
constexpr int AllDayTop = 132, AllDayRows = 2, AllDayRowHeight = 18;
constexpr int MaxLanes = 2, MinimumBlock = 20;
inline int columnX(int day) { return Left + (Right - Left) * day / 7; }
struct Range { int first = 480, last = 1200; }; // Expand to include events and the current time.
struct TimedBlock { int event, start, end, top, bottom, lane = 0, lanes = 1; };
struct DayPlan { TimedBlock blocks[MAX_EVENTS]; int count = 0, hidden = 0; };
struct AllDayBlock { int event, first, last, lane; };
struct AllDayPlan { AllDayBlock blocks[MAX_EVENTS]; int count = 0; int hidden[7] = {}; };
int minuteY(int minute, const Range &);
Range visibleRange(const Agenda &, time_t firstDayWall, const char *zone, time_t now);
void planDay(const Agenda &, time_t dayWall, const char *zone, const Range &, DayPlan &);
void planAllDay(const Agenda &, time_t firstDayWall, const char *zone, AllDayPlan &);
}
// Synthetic developer previews render to a separate buffer and never change subscriptions.
String calendarPreview(int fixture, int offset);
}
