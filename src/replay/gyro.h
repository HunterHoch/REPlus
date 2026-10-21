#pragma once
namespace gyro {
  // 1 live, 2 off, 3 align heading, 4 record, 5 stop/save, 6 replay, 7 bake, 8 undo.
  void command(unsigned action);
  void apply(void* director);
  void streamTick();
  bool portraitForExport();
  bool recording();
  bool prepareExport(const char** reason);
}
