// Tipi della modalita' autonoma (in un .h perche' Arduino genera i prototipi
// delle funzioni prima del codice degli .ino)
#pragma once
#include <Arduino.h>

struct ModelCount {
  uint32_t id;
  int32_t downloads;
  int32_t prints;
  int32_t boosts;
  int32_t comments;   // commenti + risposte (MakerWorld conta anche le tue)
};

struct PendingEvent {
  String type;
  String title;
  String coverUrl;
  int32_t value;
  int32_t total;
  uint32_t designId = 0;   // per leggere il commento nuovo
  String author;           // commento: chi l'ha scritto
  String text;             // commento: il testo
};
