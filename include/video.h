// MJPEG video playback for the ESP32-S3 + ST7789 build.
// Included from src/main.cpp. Requires: gfx, server, TJpgDec, imgOffX/imgOffY.

#pragma once

// .mjpeg layout:
//   b"MJPG" + u16 frameCount + u16 reserved
//   then per frame: u32 length + JPEG bytes

static uint8_t *vidBuf      = nullptr;
static size_t   vidCap      = 0;
static size_t   vidLen      = 0;
static uint16_t vidFrames   = 0;
static bool     vidReady    = false;
static bool     vidPlaying  = false;
static uint16_t vidFrameIdx = 0;
static uint32_t vidNextMs   = 0;
static uint32_t vidFps      = 10;
static size_t   vidAcc      = 0;
static bool     vidOver     = false;
static bool     vidStart    = false;

#define VID_MAX_BYTES (4 * 1024 * 1024)

static void vidParse() {
  vidFrames = 0;
  vidReady  = false;

  if (!vidBuf || vidLen < 8) return;
  if (memcmp(vidBuf, "MJPG", 4) != 0) {
    Serial.println("video: bad magic");
    return;
  }

  uint16_t declared = (uint16_t)vidBuf[4] | ((uint16_t)vidBuf[5] << 8);
  size_t pos = 8;
  uint16_t n = 0;

  while (pos + 4 <= vidLen) {
    uint32_t flen = (uint32_t)vidBuf[pos]
                  | ((uint32_t)vidBuf[pos + 1] << 8)
                  | ((uint32_t)vidBuf[pos + 2] << 16)
                  | ((uint32_t)vidBuf[pos + 3] << 24);
    pos += 4;
    if (flen == 0 || pos + flen > vidLen) break;
    pos += flen;
    n++;
  }

  vidFrames = n;
  vidReady  = (n > 0);
  Serial.printf("video: %u declared, %u parsed, %u bytes\n",
                declared, n, (unsigned)vidLen);
}

static void vidDrawFrame(uint16_t idx) {
  if (!vidReady || idx >= vidFrames) return;

  size_t pos = 8;
  for (uint16_t k = 0; k <= idx; k++) {
    if (pos + 4 > vidLen) return;
    uint32_t flen = (uint32_t)vidBuf[pos]
                  | ((uint32_t)vidBuf[pos + 1] << 8)
                  | ((uint32_t)vidBuf[pos + 2] << 16)
                  | ((uint32_t)vidBuf[pos + 3] << 24);
    pos += 4;
    if (k == idx) {
      TJpgDec.drawJpg(0, 0, vidBuf + pos, flen);
      return;
    }
    pos += flen;
  }
}

static void handleVideoUpload() {
  HTTPUpload &up = server.upload();

  if (up.status == UPLOAD_FILE_START) {
    vidAcc = 0;
    vidOver = false;
    vidStart = true;
    vidReady = false;
    vidPlaying = false;

    if (!vidBuf) {
      vidBuf = (uint8_t *)ps_malloc(VID_MAX_BYTES);
      vidCap = vidBuf ? VID_MAX_BYTES : 0;
      Serial.printf("video buf: %s (%u bytes)\n",
                    vidBuf ? "PSRAM" : "FAIL", (unsigned)vidCap);
    }
    return;
  }

  if (up.status == UPLOAD_FILE_WRITE) {
    if (!vidBuf || vidOver) return;
    if (vidAcc + up.currentSize > vidCap) { vidOver = true; return; }
    memcpy(vidBuf + vidAcc, up.buf, up.currentSize);
    vidAcc += up.currentSize;
  }
}

static void handleVideoDone() {
  if (!vidStart || !vidBuf) { server.send(500, "text/plain", "no upload"); return; }
  if (vidOver)              { server.send(413, "text/plain", "too large"); return; }
  if (vidAcc < 16)          { server.send(400, "text/plain", "too small"); return; }

  vidLen = vidAcc;
  vidParse();

  if (!vidReady) { server.send(400, "text/plain", "bad mjpeg"); return; }

  imgOffX = 0;
  imgOffY = 0;
  vidFrameIdx = 0;
  vidPlaying = true;
  vidNextMs = millis();

  Serial.printf("/video %u bytes, %u frames, playing\n",
                (unsigned)vidLen, vidFrames);
  server.send(200, "text/plain", "ok");
}

static void handleVidPlay() {
  if (vidReady) { vidPlaying = true; vidNextMs = millis(); }
  server.send(200, "text/plain", vidReady ? "ok" : "no video");
}

static void handleVidPause() {
  vidPlaying = false;
  server.send(200, "text/plain", "ok");
}

static void handleVidStop() {
  vidPlaying = false;
  vidFrameIdx = 0;
  gfx->fillScreen(BLACK);
  server.send(200, "text/plain", "ok");
}

static void vidTick() {
  if (!vidPlaying || !vidReady) return;
  uint32_t now = millis();
  if ((int32_t)(now - vidNextMs) >= 0) {
    vidDrawFrame(vidFrameIdx);
    vidFrameIdx++;
    if (vidFrameIdx >= vidFrames) vidFrameIdx = 0;
    vidNextMs = now + (1000 / vidFps);
  }
}
