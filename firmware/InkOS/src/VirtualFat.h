#pragma once
#include <stddef.h>
#include <stdint.h>
// Read-only FAT12 image synthesised on the fly from a small file snapshot.
// Pure C++ (no Arduino) so the layout can be validated on a host.
namespace ink {
struct VirtualFatFile {
  char name[32];      // Long name, e.g. "inkos-000001.tar.gz".
  uint32_t size;      // Bytes frozen at snapshot time.
  uint32_t mtime;     // Unix seconds (0 = unknown).
  uint16_t firstCluster, clusters;
};
class VirtualFat {
public:
  static constexpr uint16_t SectorBytes = 512, SectorsPerCluster = 4, ClusterBytes = 2048;
  static constexpr uint32_t TotalSectors = 8192; // 4 MiB volume.
  static constexpr uint16_t Reserved = 1, FatSectors = 8, RootEntries = 64,
                            RootSectors = RootEntries * 32 / SectorBytes;
  static constexpr uint32_t FirstData = Reserved + 2 * FatSectors + RootSectors;
  static constexpr uint16_t DataClusters = (TotalSectors - FirstData) / SectorsPerCluster;
  static constexpr int MaxFiles = 20;
  using ReadFn = bool (*)(const char *name, uint32_t offset, uint8_t *out, size_t n);
  // Lays out files contiguously; files that do not fit are left out. Returns count kept.
  int load(const VirtualFatFile *files, int count, uint32_t volumeId);
  // Fills one 512-byte sector.
  void sector(uint32_t lba, uint8_t *out, ReadFn read) const;
  int count() const { return files_; }
  const VirtualFatFile &file(int i) const { return file_[i]; }

private:
  void bootSector(uint8_t *out) const;
  void fatSector(uint32_t index, uint8_t *out) const;
  void rootSector(uint32_t index, uint8_t *out) const;
  uint16_t fatEntry(uint32_t cluster) const;
  int entries(uint8_t *dir, int max) const; // Builds the whole root directory.
  VirtualFatFile file_[MaxFiles];
  uint8_t dir_[RootEntries * 32] = {}; // Root directory, built by load().
  int files_ = 0;
  uint32_t volumeId_ = 0;
};
} // namespace ink
