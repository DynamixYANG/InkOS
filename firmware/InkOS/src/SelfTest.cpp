#include "Artwork.h"
#include "ArtworkFixture.h"
#include "Astronomy.h"
#include "Canvas.h"
#include "Domain.h"
#include "Energy.h"
#include "ICalendar.h"
#include "Lyrics.h"
#include "Zones.h"
#include <esp_heap_caps.h>
#include <new>
namespace ink {
bool selfTest() {
  int passed = 0, total = 0;
  auto check = [&](bool condition) {
    total++;
    if (condition)
      passed++;
    else
      Serial.printf("INKOS selftest failed_case=%d\n", total);
  };
  Holiday list[48];
  int n = holidays(2026, list, 48);
  auto has = [&](int m, int d, const char *name) {
    for (int i = 0; i < n; i++)
      if (list[i].month == m && list[i].day == d && !strcmp(list[i].name, name))
        return true;
    return false;
  };
  check(has(4, 5, "Easter Sunday"));
  check(has(6, 20, "Midsummer Day"));
  check(has(10, 31, "All Saints' Day"));
  check(has(5, 10, "Mother's Day"));
  check(Clock::monthDays(2028, 2) == 29);
  check(Clock::monthDays(2100, 2) == 28);
  check(Clock::finland(Clock::epoch(2026, 3, 29, 0, 59)).tm_hour == 2);
  check(Clock::finland(Clock::epoch(2026, 3, 29, 1)).tm_hour == 4);
  check(Clock::finland(Clock::epoch(2026, 10, 25, 0, 59)).tm_hour == 3);
  check(Clock::finland(Clock::epoch(2026, 10, 25, 1)).tm_hour == 3);
  auto cn = Clock::beijing(Clock::epoch(2026, 12, 31, 18));
  check(cn.tm_year == 127 && cn.tm_mon == 0 && cn.tm_mday == 1 && cn.tm_hour == 2);
  // Allocate the large music model in PSRAM, never on a task's small stack.
  Music *m = (Music *)heap_caps_malloc(sizeof(Music), MALLOC_CAP_SPIRAM);
  if (!m)
    return false;
  new (m) Music();
  m->duration = 10000;
  m->position = 1000;
  m->sampleUs = 1000000;
  m->valid = true;
  m->playing = true;
  check(playbackPosition(*m, 3000000) == 3000);
  m->playing = false;
  check(playbackPosition(*m, 9000000) == 1000);
  m->playing = true;
  m->lines[0].atMs = 0;
  m->lines[1].atMs = 2500;
  m->lineCount = 2;
  check(currentLyric(*m, 3000000) == 1);
  parseTimedLyrics("[ar:Original fixture]\n[00:01.20][00:04.50]First test line\n[00:03.001]Second "
                   "test line\n[00:05.00]",
                   *m);
  check(m->lineCount == 4 && m->lines[0].atMs == 1200 && m->lines[1].atMs == 3001);
  check(m->lines[3].text[0] == 0);
  check(decodeArtwork(artworkFixture, sizeof artworkFixture, m->art));
  int black = 0;
  for (uint8_t byte : m->art)
    black += 8 - __builtin_popcount(byte);
  check(black > 500 && black < ART * ART - 500);
  free(m);
  check(pageAfter(Page::Home, -1) == Page::Energy);
  check(pageAfter(Page::Energy, 1) == Page::Home);
  check(inZone(Clock::epoch(2026, 3, 29, 0, 59), "Europe/London").tm_hour == 0);
  check(inZone(Clock::epoch(2026, 3, 29, 1), "Europe/London").tm_hour == 2);
  check(inZone(Clock::epoch(2026, 10, 25, 1), "Europe/London").tm_hour == 1);
  check(inZone(Clock::epoch(2026, 3, 8, 6, 59), "America/New_York").tm_hour == 1);
  check(inZone(Clock::epoch(2026, 3, 8, 7), "America/New_York").tm_hour == 3);
  check(zoneOffset(Clock::epoch(2026, 1, 1), "Australia/Sydney") == 39600);
  check(zoneOffset(Clock::epoch(2026, 6, 1), "Australia/Sydney") == 36000);
  check(fromZone(Clock::epoch(2026, 10, 26), "Europe/Helsinki") -
            fromZone(Clock::epoch(2026, 10, 25), "Europe/Helsinki") ==
        90000);
  auto agenda = (Agenda *)heap_caps_calloc(1, sizeof(Agenda), MALLOC_CAP_SPIRAM);
  if (!agenda)
    return false;
  new (agenda) Agenda();
  const char *fixture =
      "BEGIN:VCALENDAR\r\nVERSION:2.0\r\nBEGIN:VEVENT\r\nUID:fixture-series\r\nDTSTART;TZID=Europe/"
      "Helsinki:20261023T090000\r\nDTEND;TZID=Europe/"
      "Helsinki:20261023T100000\r\nRRULE:FREQ=DAILY;COUNT=4\r\nEXDATE;TZID=Europe/"
      "Helsinki:20261024T090000\r\nSUMMARY:Original \\ntext\r\nLOCATION:Room "
      "A\r\nBEGIN:VALARM\r\nSUMMARY:Do not use alarm "
      "title\r\nEND:VALARM\r\nEND:VEVENT\r\nBEGIN:VEVENT\r\nUID:fixture-series\r\nRECURRENCE-ID;"
      "TZID=Europe/Helsinki:20261025T090000\r\nDTSTART;TZID=Europe/"
      "Helsinki:20261025T100000\r\nDTEND;TZID=Europe/Helsinki:20261025T110000\r\nSUMMARY:Moved "
      "meeting\r\nEND:VEVENT\r\nBEGIN:VEVENT\r\nUID:fixture-allday\r\nDTSTART;VALUE=DATE:"
      "20261025\r\nDTEND;VALUE=DATE:20261026\r\nSUMMARY:All-day "
      "\\nfixture\r\nEND:VEVENT\r\nEND:VCALENDAR\r\n";
  bool parsed = parseCalendar(fixture, "Europe/Helsinki", Clock::epoch(2026, 10, 20),
                              Clock::epoch(2026, 10, 30), *agenda);
  check(parsed && agenda->count == 4);
  check(parsed && agenda->events[0].start == Clock::epoch(2026, 10, 23, 6));
  check(parsed && !strcmp(agenda->events[0].title, "Original  text"));
  check(parsed && agenda->events[1].allDay &&
        agenda->events[1].end - agenda->events[1].start == 90000);
  check(parsed && agenda->events[2].start == Clock::epoch(2026, 10, 25, 8));
  check(parsed && agenda->events[3].start == Clock::epoch(2026, 10, 26, 7));
  check(nextEvent(*agenda, Clock::epoch(2026, 10, 25, 8, 30)) == &agenda->events[2]);
  check(nextEvent(*agenda, Clock::epoch(2026, 10, 30)) == nullptr);
  check(!parseCalendar("<html>login</html>", "Europe/Helsinki", 0, 0, *agenda));
  free(agenda);
  auto a = (uint8_t *)heap_caps_malloc(FRAME_BYTES, MALLOC_CAP_SPIRAM);
  auto b = (uint8_t *)heap_caps_malloc(FRAME_BYTES, MALLOC_CAP_SPIRAM);
  if (!a || !b) {
    free(a);
    free(b);
    return false;
  }
  Canvas first(a), second(b);
  for (int code : {0, 3, 61}) {
    first.clear();
    second.clear();
    first.animatedIcon(21, 332, 72, code, true, 0);
    second.animatedIcon(21, 332, 72, code, true, 1);
    int changed = 0;
    bool contained = true;
    for (int y = 0; y < HEIGHT; y++)
      for (int x = 0; x < WIDTH; x++) {
        int offset = y * 100 + x / 8;
        if (((a[offset] ^ b[offset]) & (128 >> (x & 7)))) {
          changed++;
          contained &= x >= 21 && x < 93 && y >= 332 && y < 404;
        }
      }
    check(changed > 4 && contained);
  }
  free(a);
  free(b);
  auto solar = solarDay(Clock::epoch(2026, 10, 15, 12), 65.0121, 25.4651);
  // Open-Meteo reference fetched 2026-10-15; independent solar calculation.
  check(llabs(solar.rise - 1792041034) < 300 && llabs(solar.set - 1792076161) < 300);
  auto night = adjacentSolarEvents(Clock::epoch(2026, 10, 15, 23), 65.0121, 25.4651);
  check(night.previous.at < Clock::epoch(2026, 10, 15, 23) &&
        night.next.at > Clock::epoch(2026, 10, 15, 23) && !night.previous.rising &&
        night.next.rising);
  auto polar = solarDay(Clock::epoch(2026, 6, 21, 12), 80, 20);
  check(polar.polarDay && !polar.rise && !polar.set);
  auto polarPair = adjacentSolarEvents(Clock::epoch(2026, 6, 21, 12), 80, 20);
  check(polarPair.previous.at > 0 && polarPair.next.at > Clock::epoch(2026, 6, 21, 12));
  auto newMoon = moonState(Clock::epoch(2026, 10, 10, 16)),
       fullMoon = moonState(Clock::epoch(2026, 10, 26, 4));
  check(newMoon.illuminated < .02 && fullMoon.illuminated > .98);
  // USNO phase dates: SunCalc is approximate, not an ephemeris service.
  check(llabs(moonPhaseEvent(Clock::epoch(2026, 10, 15), 1) - Clock::epoch(2026, 10, 18, 16, 12)) <
        21600);
  check(llabs(moonPhaseEvent(Clock::epoch(2026, 10, 15), 2) - Clock::epoch(2026, 10, 26, 4, 12)) <
        21600);
  check(llabs(moonPhaseEvent(Clock::epoch(2026, 10, 15), 0, false) -
              Clock::epoch(2026, 10, 10, 15, 50)) < 21600);
  check(parsePriceTimestamp("2026-10-25T03:00:00+03:00") == Clock::epoch(2026, 10, 25));
  check(parsePriceTimestamp("2026-02-30T00:00:00Z") == 0);
  check(parsePriceTimestamp("2026-10-15T00:00:00") == 0);
  check(Clock::fromFinland(Clock::epoch(2026, 3, 30)) -
            Clock::fromFinland(Clock::epoch(2026, 3, 29)) ==
        23 * 3600);
  auto energy = (Energy *)heap_caps_malloc(sizeof(Energy), MALLOC_CAP_SPIRAM);
  if (!energy)
    return false;
  new (energy) Energy();
  const char *prices =
      "{\"prices\":[{\"price\":1,\"startDate\":\"2026-10-25T00:00:00Z\",\"endDate\":\"2026-10-"
      "25T00:14:59.999Z\"},{\"price\":2,\"startDate\":\"2026-10-25T00:15:00Z\",\"endDate\":\"2026-"
      "10-25T00:29:59.999Z\"},{\"price\":-1,\"startDate\":\"2026-10-25T00:30:00Z\",\"endDate\":"
      "\"2026-10-25T00:44:59.999Z\"},{\"price\":2,\"startDate\":\"2026-10-25T00:45:00Z\","
      "\"endDate\":\"2026-10-25T00:59:59.999Z\"}]}";
  auto stamp = Clock::epoch(2026, 10, 25);
  check(parseEnergyPrices(prices, *energy, stamp) && energy->count == 1 &&
        fabs(energy->hours[0].spotNetCt - 1) < .0001);
  check(fabs(totalPriceCt(*energy, stamp) - 9.34975) < .0001);
  energy->feesIncludeVat = true;
  check(fabs(totalPriceCt(*energy, stamp) - 7.705) < .0001);
  energy->feesIncludeVat = false;
  check(!priceAt(*energy, stamp + 3600));
  check(!parseEnergyPrices("{\"prices\":[{\"price\":4,\"startDate\":\"2026-10-25T01:00:00Z\","
                           "\"endDate\":\"2026-10-25T01:14:59.999Z\"}]}",
                           *energy, stamp));
  energy->hours[0].spotNetCt = -10;
  check(totalPriceCt(*energy, stamp) < 0);
  check(parseEnergyPrices("{\"prices\":[{\"price\":1.255,\"startDate\":\"2026-10-25T01:00:00Z\","
                          "\"endDate\":\"2026-10-25T01:59:59.999Z\"}]}",
                          *energy, stamp, 1) &&
        fabs(priceAt(*energy, stamp + 3600)->spotNetCt - 1) < .0001);
  check(!energy->feesIncludeVat && energy->vatPercent == 25.5 && energy->marginCt == .45 &&
        energy->transmissionCt == 6);
  check(
      parseEnergyPrices("{\"prices\":[{\"value\":100,\"date\":\"2026-10-25T02:00:00Z\"},{\"value\":"
                        "200,\"date\":\"2026-10-25T02:15:00Z\"},{\"value\":-100,\"date\":\"2026-10-"
                        "25T02:30:00Z\"},{\"value\":200,\"date\":\"2026-10-25T02:45:00Z\"}]}",
                        *energy, stamp, 0) &&
      fabs(priceAt(*energy, stamp + 7200)->spotNetCt - 10) < .0001);
  free(energy);
  Serial.printf("INKOS selftest passed=%d total=%d\n", passed, total);
  return passed == total;
}
} // namespace ink
