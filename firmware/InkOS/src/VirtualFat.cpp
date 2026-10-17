#include "VirtualFat.h"
#include <stdio.h>
#include <string.h>
#include <time.h>
namespace ink {
namespace {
void put16(uint8_t *p, uint16_t v) { p[0] = uint8_t(v); p[1] = uint8_t(v >> 8); }
void put32(uint8_t *p, uint32_t v) { for (int i = 0; i < 4; i++) p[i] = uint8_t(v >> (8 * i)); }
void fatTime(uint32_t unix, uint16_t &date, uint16_t &time2) {
  if (unix < 315532800u + 86400u) unix = 1767225600u; // Unknown: 2026-01-01.
  time_t t = time_t(unix);
  struct tm tm;
  gmtime_r(&t, &tm);
  date = uint16_t(((tm.tm_year - 80) << 9) | ((tm.tm_mon + 1) << 5) | tm.tm_mday);
  time2 = uint16_t((tm.tm_hour << 11) | (tm.tm_min << 5) | (tm.tm_sec / 2));
}
bool isShort(const char *name, char out[11], uint8_t &caseFlags) {
  // Accept lower-case 8.3 names such as "current.log" (NT case flags keep them lower case).
  const char *dot = strchr(name, '.');
  size_t base = dot ? size_t(dot - name) : strlen(name), ext = dot ? strlen(dot + 1) : 0;
  if (!base || base > 8 || ext > 3 || (dot && strchr(dot + 1, '.')))
    return false;
  memset(out, ' ', 11);
  for (size_t i = 0; i < base; i++) {
    char c = name[i];
    if (!((c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '-' || c == '_')) return false;
    out[i] = (c >= 'a' && c <= 'z') ? char(c - 32) : c;
  }
  for (size_t i = 0; i < ext; i++) {
    char c = dot[1 + i];
    if (!((c >= 'a' && c <= 'z') || (c >= '0' && c <= '9'))) return false;
    out[8 + i] = (c >= 'a' && c <= 'z') ? char(c - 32) : c;
  }
  caseFlags = 0x08 | 0x10;
  return true;
}
uint8_t lfnChecksum(const char *sfn) {
  uint8_t sum = 0;
  for (int i = 0; i < 11; i++) sum = uint8_t(((sum & 1) << 7) + (sum >> 1) + uint8_t(sfn[i]));
  return sum;
}
} // namespace

int VirtualFat::load(const VirtualFatFile *files, int count, uint32_t volumeId) {
  volumeId_ = volumeId;
  files_ = 0;
  uint32_t next = 2, last = 2 + DataClusters; // Cluster numbers are [2, last).
  for (int i = 0; i < count && files_ < MaxFiles; i++) {
    VirtualFatFile f = files[i];
    uint32_t clusters = (f.size + ClusterBytes - 1) / ClusterBytes;
    if (next + clusters > last)
      continue;
    f.firstCluster = clusters ? uint16_t(next) : 0;
    f.clusters = uint16_t(clusters);
    next += clusters;
    file_[files_++] = f;
  }
  // Drop trailing files until the root directory fits (LFN entries cost extra slots).
  while (files_ && entries(dir_, RootEntries) < 0)
    files_--;
  if (!files_)
    entries(dir_, RootEntries);
  return files_;
}

void VirtualFat::bootSector(uint8_t *s) const {
  const uint8_t jump[3] = {0xEB, 0x3C, 0x90};
  memcpy(s, jump, 3);
  memcpy(s + 3, "INKOS1.0", 8);
  put16(s + 11, SectorBytes);
  s[13] = SectorsPerCluster;
  put16(s + 14, Reserved);
  s[16] = 2;
  put16(s + 17, RootEntries);
  put16(s + 19, uint16_t(TotalSectors));
  s[21] = 0xF8;
  put16(s + 22, FatSectors);
  put16(s + 24, 32);
  put16(s + 26, 2);
  s[36] = 0x80;
  s[38] = 0x29;
  put32(s + 39, volumeId_);
  memcpy(s + 43, "INKOS_LOGS ", 11);
  memcpy(s + 54, "FAT12   ", 8);
  s[510] = 0x55;
  s[511] = 0xAA;
}

uint16_t VirtualFat::fatEntry(uint32_t c) const {
  if (c == 0) return 0xFF8;
  if (c == 1) return 0xFFF;
  for (int i = 0; i < files_; i++) {
    const auto &f = file_[i];
    if (f.clusters && c >= f.firstCluster && c < uint32_t(f.firstCluster) + f.clusters)
      return c + 1 == uint32_t(f.firstCluster) + f.clusters ? 0xFFF : uint16_t(c + 1);
  }
  return 0;
}

void VirtualFat::fatSector(uint32_t index, uint8_t *s) const {
  uint32_t base = index * SectorBytes;
  for (uint32_t i = 0; i < SectorBytes; i++) {
    uint32_t b = base + i, pair = b / 3, at = b % 3;
    uint32_t e0 = fatEntry(pair * 2), e1 = fatEntry(pair * 2 + 1);
    if (pair * 2 >= uint32_t(DataClusters) + 2) { s[i] = 0; continue; }
    if (pair * 2 + 1 >= uint32_t(DataClusters) + 2) e1 = 0;
    s[i] = at == 0 ? uint8_t(e0) : at == 1 ? uint8_t((e0 >> 8) | ((e1 & 0xF) << 4)) : uint8_t(e1 >> 4);
  }
}

int VirtualFat::entries(uint8_t *dir, int max) const {
  memset(dir, 0, size_t(max) * 32);
  int n = 0;
  uint8_t *e = dir;
  memcpy(e, "INKOS_LOGS ", 11);
  e[11] = 0x08; // Volume label.
  n = 1;
  for (int i = 0; i < files_; i++) {
    const auto &f = file_[i];
    char sfn[11];
    uint8_t caseFlags = 0;
    bool plain = isShort(f.name, sfn, caseFlags);
    size_t len = strlen(f.name);
    int lfn = plain ? 0 : int((len + 12) / 13);
    if (!plain) {
      // Unique short alias: LOG~NNNN.TGZ style, index keeps it unique.
      char alias[12];
      snprintf(alias, sizeof alias, "LOG%05d", i + 1);
      memcpy(sfn, alias, 8);
      memcpy(sfn + 8, "TGZ", 3);
    }
    if (n + lfn + 1 > max)
      return -1;
    uint8_t sum = lfnChecksum(sfn);
    for (int k = lfn; k >= 1; k--) {
      uint8_t *l = dir + 32 * n++;
      l[0] = uint8_t(k | (k == lfn ? 0x40 : 0));
      l[11] = 0x0F;
      l[13] = sum;
      static const uint8_t pos[13] = {1, 3, 5, 7, 9, 14, 16, 18, 20, 22, 24, 28, 30};
      for (int c = 0; c < 13; c++) {
        size_t at = size_t(k - 1) * 13 + c;
        uint16_t ch = at < len ? uint8_t(f.name[at]) : at == len ? 0 : 0xFFFF;
        put16(l + pos[c], ch);
      }
    }
    uint8_t *d = dir + 32 * n++;
    memcpy(d, sfn, 11);
    d[11] = 0x01; // Read-only.
    d[12] = caseFlags;
    uint16_t date, tm;
    fatTime(f.mtime, date, tm);
    put16(d + 14, tm);
    put16(d + 16, date);
    put16(d + 18, date);
    put16(d + 22, tm);
    put16(d + 24, date);
    put16(d + 26, f.firstCluster);
    put32(d + 28, f.size);
  }
  return n;
}

void VirtualFat::rootSector(uint32_t index, uint8_t *s) const {
  memcpy(s, dir_ + index * SectorBytes, SectorBytes);
}

void VirtualFat::sector(uint32_t lba, uint8_t *out, ReadFn read) const {
  memset(out, 0, SectorBytes);
  if (lba == 0) return bootSector(out);
  if (lba < Reserved + 2u * FatSectors) return fatSector((lba - Reserved) % FatSectors, out);
  if (lba < FirstData) return rootSector(lba - Reserved - 2u * FatSectors, out);
  if (lba >= TotalSectors) return;
  uint32_t cluster = 2 + (lba - FirstData) / SectorsPerCluster;
  uint32_t within = ((lba - FirstData) % SectorsPerCluster) * SectorBytes;
  for (int i = 0; i < files_; i++) {
    const auto &f = file_[i];
    if (f.clusters && cluster >= f.firstCluster && cluster < uint32_t(f.firstCluster) + f.clusters) {
      uint32_t offset = (cluster - f.firstCluster) * ClusterBytes + within;
      if (offset >= f.size) return;
      size_t n = f.size - offset < SectorBytes ? f.size - offset : SectorBytes;
      if (read && !read(f.name, offset, out, n)) memset(out, 0, SectorBytes);
      return;
    }
  }
}
} // namespace ink
