// Tipi della modalita' autonoma (in un .h perche' Arduino genera i prototipi
// delle funzioni prima del codice degli .ino)
#pragma once
#include <Arduino.h>

struct ModelCount {
  uint32_t id;
  int32_t downloads;
  int32_t prints;
  int32_t boosts;
};

struct PendingEvent {
  String type;
  String title;
  String coverUrl;
  int32_t value;
  int32_t total;
};
