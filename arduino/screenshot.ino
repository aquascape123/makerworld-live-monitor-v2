// ============================================
// SCREENSHOT DEL DISPLAY: http://<IP>/screenshot
// ============================================
// Legge i pixel dal display e li manda come immagine BMP (24 bit).
// Utile per vedere la grafica senza avere il CYD davanti.

void handleScreenshot() {
  const int w = tft.width();
  const int h = tft.height();
  const uint32_t rowBytes = w * 3;                 // 320*3 = 960, gia' multiplo di 4
  const uint32_t dataSize = rowBytes * h;
  const uint32_t fileSize = 54 + dataSize;

  uint8_t header[54] = {0};
  header[0] = 'B'; header[1] = 'M';
  memcpy(&header[2], &fileSize, 4);
  header[10] = 54;                                  // offset dati
  header[14] = 40;                                  // dimensione header DIB
  int32_t bw = w, bh = h;                           // altezza positiva: righe dal basso
  memcpy(&header[18], &bw, 4);
  memcpy(&header[22], &bh, 4);
  header[26] = 1;                                   // piani
  header[28] = 24;                                  // bit per pixel
  memcpy(&header[34], &dataSize, 4);

  server.setContentLength(fileSize);
  server.send(200, "image/bmp", "");
  server.sendContent((const char *)header, 54);

  uint16_t *line = (uint16_t *)malloc(w * sizeof(uint16_t));
  uint8_t *out = (uint8_t *)malloc(rowBytes);
  if (!line || !out) {
    free(line);
    free(out);
    return;
  }
  for (int y = h - 1; y >= 0; y--) {
    tft.readRect(0, y, w, 1, line);
    for (int x = 0; x < w; x++) {
      uint16_t c = (line[x] >> 8) | (line[x] << 8);  // readRect restituisce i byte scambiati
      out[x * 3 + 0] = (c & 0x1F) << 3;             // B
      out[x * 3 + 1] = ((c >> 5) & 0x3F) << 2;      // G
      out[x * 3 + 2] = ((c >> 11) & 0x1F) << 3;     // R
    }
    server.sendContent((const char *)out, rowBytes);
  }
  free(line);
  free(out);
}

