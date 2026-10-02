// ============================================================
//  1.69 inch ST7789 240x280 + ESP32-S3
//  WiFi web control: text (CN + EN) and JPEG images
//
//  TFT wiring:
//    SCK=12  MOSI=11  CS=10  DC=9  BLK=4
//    RST: NOT CONNECTED (vendor demo passes -1)
//
//  Chinese: 16x16 GB2312 level-1 bitmap font in include/cn_font.h
//  Images : JPEG POSTed by the browser, decoded by TJpg_Decoder
// ============================================================

#include <Arduino.h>
#include <WiFi.h>
#include <WebServer.h>
#include <Arduino_GFX_Library.h>
#include <TJpg_Decoder.h>

#include "cn_font.h"

// ---------------- WiFi ----------------
static const char *WIFI_SSID = "4-2-3";
static const char *WIFI_PASS = "liu818226";

// ---------------- TFT ----------------
#define GFX_BL 4

Arduino_DataBus *bus = new Arduino_ESP32SPI(
    9  /* DC */,
    10 /* CS */,
    12 /* SCK */,
    11 /* MOSI */);

Arduino_GFX *gfx = new Arduino_ST7789(
    bus, -1 /* RST */, 0 /* rotation */, true /* IPS */,
    240, 280, 0 /* col off */, 20 /* row off */, 0, 0);

WebServer server(80);

// ---------------- display state ----------------
static int      curSize  = 2;
static uint16_t curColor = WHITE;
static int      cursorX  = 8;
static int      cursorY  = 8;

#define LINE_GAP 2
#define MARGIN_X 8

static uint16_t colorFromName(const String &name) {
  if (name == "white")   return WHITE;
  if (name == "red")     return RED;
  if (name == "green")   return GREEN;
  if (name == "blue")    return BLUE;
  if (name == "yellow")  return YELLOW;
  if (name == "cyan")    return CYAN;
  if (name == "magenta") return MAGENTA;
  if (name == "orange")  return ORANGE;
  return WHITE;
}

// ---------------- CJK ----------------
// cn_font_index is sorted ascending, so binary search works.
static const uint8_t *findGlyph(uint32_t cp) {
  int lo = 0, hi = CN_FONT_COUNT - 1;
  while (lo <= hi) {
    int mid = (lo + hi) >> 1;
    uint32_t v = cn_font_index[mid];
    if (v == cp) return cn_font16[mid];
    if (v < cp)  lo = mid + 1;
    else         hi = mid - 1;
  }
  return nullptr;
}

static void drawCJK(uint32_t cp, int x, int y, int scale, uint16_t color) {
  const uint8_t *g = findGlyph(cp);
  if (!g) return;

  for (int row = 0; row < 16; row++) {
    uint8_t hiB = g[row * 2];
    uint8_t loB = g[row * 2 + 1];
    for (int col = 0; col < 16; col++) {
      bool on = (col < 8) ? ((hiB >> (7 - col)) & 1)
                          : ((loB >> (7 - (col - 8))) & 1);
      if (!on) continue;
      if (scale == 1) gfx->drawPixel(x + col, y + row, color);
      else            gfx->fillRect(x + col * scale, y + row * scale, scale, scale, color);
    }
  }
}

// ---------------- layout ----------------
static int asciiW(int s) { return 6 * s; }
static int cjkW(int s)   { return 16 * s; }
static int lineH(int s)  { return (16 * s) + LINE_GAP; }

static void nextLine(int s) {
  cursorX = MARGIN_X;
  cursorY += lineH(s);
  if (cursorY + lineH(s) > (int)gfx->height()) {
    gfx->fillScreen(BLACK);
    cursorY = MARGIN_X;
  }
}

// Decode one UTF-8 codepoint; advances i past it.
static uint32_t utf8Next(const String &s, unsigned int &i) {
  uint8_t c = (uint8_t)s[i];
  if (c < 0x80) { i += 1; return c; }

  if ((c & 0xE0) == 0xC0 && i + 1 < s.length()) {
    uint32_t cp = ((c & 0x1F) << 6) | ((uint8_t)s[i + 1] & 0x3F);
    i += 2; return cp;
  }
  if ((c & 0xF0) == 0xE0 && i + 2 < s.length()) {
    uint32_t cp = ((c & 0x0F) << 12)
                | (((uint8_t)s[i + 1] & 0x3F) << 6)
                |  ((uint8_t)s[i + 2] & 0x3F);
    i += 3; return cp;
  }
  if ((c & 0xF8) == 0xF0 && i + 3 < s.length()) {
    uint32_t cp = ((c & 0x07) << 18)
                | (((uint8_t)s[i + 1] & 0x3F) << 12)
                | (((uint8_t)s[i + 2] & 0x3F) << 6)
                |  ((uint8_t)s[i + 3] & 0x3F);
    i += 4; return cp;
  }
  i += 1; return '?';
}

static void drawText(const String &raw) {
  const int s = curSize;

  if (raw.length() == 0) { nextLine(s); return; }

  unsigned int i = 0;
  while (i < raw.length()) {
    uint32_t cp = utf8Next(raw, i);

    if (cp == '\n') { nextLine(s); continue; }

    if (cp >= 0x20 && cp < 0x7F) {
      int w = asciiW(s);
      if (cursorX + w > (int)gfx->width()) nextLine(s);
      gfx->setTextSize(s);
      gfx->setTextColor(curColor, BLACK);
      gfx->setCursor(cursorX, cursorY);
      gfx->write((char)cp);
      cursorX += w;
    } else if (cp >= 0x2E80) {
      int w = cjkW(s);
      if (cursorX + w > (int)gfx->width()) nextLine(s);
      drawCJK(cp, cursorX, cursorY, s, curColor);
      cursorX += w;
    }
  }
}

// ---------------- JPEG decode ----------------
// Destination offset so a smaller image is centered on the panel.
static int imgOffX = 0;
static int imgOffY = 0;

static bool jpgOutput(int16_t x, int16_t y, uint16_t w, uint16_t h, uint16_t *bmp) {
  int dx = x + imgOffX;
  int dy = y + imgOffY;

  // clip to panel
  if (dx >= (int)gfx->width() || dy >= (int)gfx->height()) return true;

  gfx->draw16bitRGBBitmap(dx, dy, bmp, w, h);
  return true;
}

// ---------------- video engine ----------------
// Included here so gfx / server / TJpgDec / imgOffX are already declared.
#include "video.h"

// ---------------- HTML ----------------
static const char INDEX_HTML[] PROGMEM = R"HTMLPAGE(
<!DOCTYPE html>
<html>
<head>
<meta charset="utf-8">
<meta name="viewport" content="width=device-width,initial-scale=1">
<title>ESP32 TFT</title>
<style>
  body{font-family:system-ui,-apple-system,"Segoe UI",sans-serif;
       margin:0;padding:16px;background:#111;color:#eee}
  h1{font-size:20px;margin:0 0 12px}
  .card{background:#1c1c1e;border-radius:12px;padding:14px;margin-bottom:12px}
  label{display:block;font-size:13px;color:#9a9a9e;margin-bottom:6px}
  input[type=text]{width:100%;box-sizing:border-box;font-size:17px;padding:11px;
       border-radius:9px;border:1px solid #333;background:#2c2c2e;color:#fff}
  input[type=file]{width:100%;font-size:14px;color:#ccc}
  button{font-size:16px;padding:11px 16px;border-radius:9px;border:0;
       background:#0a84ff;color:#fff;font-weight:600}
  button.sec{background:#3a3a3c}
  .row{display:flex;gap:8px;flex-wrap:wrap;margin-top:10px}
  .sw{width:40px;height:40px;border-radius:9px;border:2px solid #444;cursor:pointer}
  .sw.on{border-color:#fff}
  .sz{min-width:44px}
  #status{font-size:13px;color:#9a9a9e;margin-top:8px}
  #preview{max-width:100%;margin-top:10px;border-radius:9px;display:none}
</style>
</head>
<body>
<h1>ESP32 TFT Control</h1>

<div class="card">
  <label>Video (.mjpeg built by tools/pack_mjpeg.py)</label>
  <input type="file" id="vid" accept=".mjpeg,.mjpg,.bin">
  <div class="row">
    <button onclick="uploadVideo()">Upload &amp; Play</button>
    <button class="sec" onclick="hit('/vid/pause')">Pause</button>
    <button class="sec" onclick="hit('/vid/play')">Resume</button>
    <button class="sec" onclick="hit('/vid/stop')">Stop</button>
  </div>
  <div id="vstatus" style="font-size:13px;color:#9a9a9e;margin-top:8px"></div>
</div>

<div class="card">
  <label>Text (Chinese and English)</label>
  <input type="text" id="msg" placeholder="type then press Send" autocomplete="off">
  <div class="row">
    <button onclick="send()">Send</button>
    <button class="sec" onclick="hit('/clear')">Clear</button>
    <button class="sec" onclick="hit('/newline')">Newline</button>
  </div>
</div>

<div class="card">
  <label>Text size</label>
  <div class="row" id="sizes"></div>
</div>

<div class="card">
  <label>Color</label>
  <div class="row" id="colors"></div>
</div>

<div class="card">
  <label>Image: resized to fit 240x280, sent as JPEG</label>
  <input type="file" id="file" accept="image/*">
  <div class="row">
    <button onclick="uploadImage()">Upload image</button>
  </div>
  <canvas id="preview"></canvas>
  <div id="status"></div>
</div>

<script>
const COLORS = ["white","red","green","blue","yellow","cyan","magenta","orange"];
const HEX = {white:"#ffffff",red:"#ff3b30",green:"#34c759",blue:"#0a84ff",
             yellow:"#ffd60a",cyan:"#64d2ff",magenta:"#ff2d55",orange:"#ff9f0a"};
let curColor = "white", curSize = 2;

const sizesEl = document.getElementById("sizes");
for (let s = 1; s <= 3; s++) {
  const b = document.createElement("button");
  b.className = "sec sz";
  b.textContent = s;
  b.onclick = () => { curSize = s; fetch("/size?v=" + s); refresh(); };
  sizesEl.appendChild(b);
}

const colorsEl = document.getElementById("colors");
COLORS.forEach(c => {
  const d = document.createElement("div");
  d.className = "sw";
  d.style.background = HEX[c];
  d.onclick = () => { curColor = c; fetch("/color?v=" + c); refresh(); };
  colorsEl.appendChild(d);
});

function refresh() {
  [...sizesEl.children].forEach(b => {
    b.style.background = (parseInt(b.textContent) === curSize) ? "#0a84ff" : "#3a3a3c";
  });
  [...colorsEl.children].forEach((d, i) => {
    d.className = "sw" + (COLORS[i] === curColor ? " on" : "");
  });
}
refresh();

async function send() {
  const el = document.getElementById("msg");
  const t = el.value;
  if (!t) return;
  await fetch("/text?msg=" + encodeURIComponent(t));
  el.value = "";
  el.focus();
}
async function hit(p) { await fetch(p); }

document.getElementById("msg").addEventListener("keydown", e => {
  if (e.key === "Enter") send();
});

const fileEl = document.getElementById("file");
const cv = document.getElementById("preview");
const st = document.getElementById("status");

fileEl.addEventListener("change", () => {
  const f = fileEl.files[0];
  if (!f) return;
  const img = new Image();
  img.onload = () => {
    const MAXW = 240, MAXH = 280;
    let w = img.width, h = img.height;
    const r = Math.min(MAXW / w, MAXH / h, 1);
    w = Math.round(w * r);
    h = Math.round(h * r);
    // JPEG MCU is 8 or 16 px; round down to a multiple of 8 to avoid edge artifacts
    w = Math.max(8, w - (w % 8));
    h = Math.max(8, h - (h % 8));

    cv.width = w; cv.height = h;
    cv.style.display = "block";
    const ctx = cv.getContext("2d");
    ctx.fillStyle = "#000";
    ctx.fillRect(0, 0, w, h);
    ctx.drawImage(img, 0, 0, w, h);
    st.textContent = "ready: " + w + "x" + h;
  };
  img.src = URL.createObjectURL(f);
});

async function uploadImage() {
  if (!cv.width) { st.textContent = "pick a file first"; return; }
  st.textContent = "encoding...";
  const blob = await new Promise(res => cv.toBlob(res, "image/jpeg", 0.75));

  // WebServer expects multipart/form-data, so wrap the JPEG in a FormData.
  const fd = new FormData();
  fd.append("img", blob, "photo.jpg");

  st.textContent = "uploading " + Math.round(blob.size / 1024) + " KB...";
  const t0 = Date.now();
  try {
    const r = await fetch("/image", { method: "POST", body: fd });
    const txt = await r.text();
    st.textContent = (r.ok ? "done in " : "FAILED: " + txt + " ") + (Date.now() - t0) + " ms";
  } catch (e) {
    st.textContent = "error: " + e;
  }
}

async function uploadVideo() {
  const el = document.getElementById("vid");
  const f = el.files[0];
  const vs = document.getElementById("vstatus");
  if (!f) { vs.textContent = "pick a .mjpeg file first"; return; }
  const fd = new FormData();
  fd.append("vid", f, "video.mjpeg");
  vs.textContent = "uploading " + Math.round(f.size / 1024) + " KB...";
  const t0 = Date.now();
  try {
    const r = await fetch("/video", { method: "POST", body: fd });
    const txt = await r.text();
    vs.textContent = (r.ok ? "playing" : "FAILED: " + txt) + " (" + (Date.now() - t0) + " ms)";
  } catch (e) {
    vs.textContent = "error: " + e;
  }
}
</script>
</body>
</html>
)HTMLPAGE";

// ---------------- JPEG upload ----------------
// WebServer parses multipart/form-data and hands the file to us in chunks
// via HTTPUpload (buf holds HTTP_UPLOAD_BUFLEN = 1436 bytes per call).
// We accumulate into a PSRAM buffer and decode once the body is complete.

static uint8_t *jpgBuf      = nullptr;
static size_t   jpgLen      = 0;
static size_t   jpgCap      = 0;
static bool     jpgOversize = false;
static bool     jpgStarted  = false;

#define JPG_MAX_BYTES (400 * 1024)

static void handleImageUpload() {
  HTTPUpload &up = server.upload();

  if (up.status == UPLOAD_FILE_START) {
    jpgLen = 0;
    jpgOversize = false;
    jpgStarted = true;

    if (!jpgBuf) {
      jpgBuf = (uint8_t *)ps_malloc(JPG_MAX_BYTES);
      jpgCap = jpgBuf ? JPG_MAX_BYTES : 0;
      Serial.printf("JPEG buffer: %s (%u bytes)\n",
                    jpgBuf ? "PSRAM" : "ALLOC FAILED", (unsigned)jpgCap);
    }
    Serial.printf("upload start: %s\n", up.filename.c_str());
    return;
  }

  if (up.status == UPLOAD_FILE_WRITE) {
    if (!jpgBuf || jpgOversize) return;
    if (jpgLen + up.currentSize > jpgCap) {
      jpgOversize = true;
      Serial.println("upload too large, discarding");
      return;
    }
    memcpy(jpgBuf + jpgLen, up.buf, up.currentSize);
    jpgLen += up.currentSize;
    return;
  }
}

static void handleImageDone() {
  if (!jpgStarted)  { server.send(400, "text/plain", "no upload"); return; }
  if (!jpgBuf)      { server.send(500, "text/plain", "no buffer"); return; }
  if (jpgOversize)  { server.send(413, "text/plain", "too large"); return; }
  if (jpgLen < 128) { server.send(400, "text/plain", "too small"); return; }

  Serial.printf("/image  %u bytes\n", (unsigned)jpgLen);

  uint16_t iw = 0, ih = 0;
  TJpgDec.getJpgSize(&iw, &ih, jpgBuf, jpgLen);

  imgOffX = ((int)gfx->width()  - (int)iw) / 2;
  imgOffY = ((int)gfx->height() - (int)ih) / 2;
  if (imgOffX < 0) imgOffX = 0;
  if (imgOffY < 0) imgOffY = 0;

  gfx->fillScreen(BLACK);

  JRESULT r = TJpgDec.drawJpg(0, 0, jpgBuf, jpgLen);
  Serial.printf("decode %s  (%ux%u)  offset %d,%d\n",
                r == JDR_OK ? "OK" : "FAIL", iw, ih, imgOffX, imgOffY);

  server.send(200, "text/plain", r == JDR_OK ? "ok" : "decode failed");
}

// ---------------- other handlers ----------------
static void handleRoot()  { server.send_P(200, "text/html", INDEX_HTML); }

static void handleClear() {
  gfx->fillScreen(BLACK);
  cursorX = MARGIN_X;
  cursorY = MARGIN_X;
  Serial.println("/clear");
  server.send(200, "text/plain", "ok");
}

static void handleNewline() {
  nextLine(curSize);
  Serial.println("/newline");
  server.send(200, "text/plain", "ok");
}

static void handleSize() {
  int v = server.arg("v").toInt();
  if (v >= 1 && v <= 3) curSize = v;
  Serial.printf("/size = %d\n", curSize);
  server.send(200, "text/plain", "ok");
}

static void handleColor() {
  curColor = colorFromName(server.arg("v"));
  Serial.printf("/color = %s\n", server.arg("v").c_str());
  server.send(200, "text/plain", "ok");
}

static void handleText() {
  String msg = server.arg("msg");
  Serial.printf("/text msg=\"%s\"\n", msg.c_str());
  drawText(msg);
  server.send(200, "text/plain", "ok");
}

static void handleNotFound() { server.send(404, "text/plain", "not found"); }

// ---------------- setup ----------------
void setup() {
  Serial.begin(115200);
  delay(1000);
  Serial.println();
  Serial.println("=== ESP32 TFT web control ===");

  pinMode(GFX_BL, OUTPUT);
  digitalWrite(GFX_BL, HIGH);

  gfx->begin();
  gfx->fillScreen(BLACK);
  gfx->setTextSize(2);
  gfx->setTextColor(WHITE, BLACK);
  gfx->setCursor(MARGIN_X, MARGIN_X);
  gfx->println("WiFi connecting...");

  // TJpg_Decoder writes pixels straight to the panel
  TJpgDec.setCallback(jpgOutput);

  WiFi.mode(WIFI_STA);
  WiFi.begin(WIFI_SSID, WIFI_PASS);
  Serial.printf("connecting to \"%s\"", WIFI_SSID);

  int tries = 0;
  while (WiFi.status() != WL_CONNECTED && tries < 60) {
    delay(500);
    Serial.print(".");
    tries++;
  }
  Serial.println();

  if (WiFi.status() == WL_CONNECTED) {
    String ip = WiFi.localIP().toString();
    Serial.printf("connected! IP = %s\n", ip.c_str());

    gfx->fillScreen(BLACK);
    gfx->setTextSize(2);
    gfx->setTextColor(GREEN, BLACK);
    gfx->setCursor(MARGIN_X, 8);
    gfx->println("WiFi OK");
    gfx->setTextColor(WHITE, BLACK);
    gfx->setCursor(MARGIN_X, 40);
    gfx->println("Open in browser:");
    gfx->setTextColor(YELLOW, BLACK);
    gfx->setCursor(MARGIN_X, 72);
    gfx->println(ip);

    cursorX = MARGIN_X;
    cursorY = 120;

    curColor = GREEN;
    drawText("\xE4\xB8\xAD\xE6\x96\x87\xE6\xB5\x8B\xE8\xAF\x95");  // UTF-8 for 中文测试
    nextLine(curSize);
    curColor = WHITE;
  } else {
    Serial.println("WiFi FAILED");
    gfx->fillScreen(BLACK);
    gfx->setTextSize(2);
    gfx->setTextColor(RED, BLACK);
    gfx->setCursor(MARGIN_X, 8);
    gfx->println("WiFi FAILED");
    cursorX = MARGIN_X;
    cursorY = 60;
  }

  server.on("/",        HTTP_GET,  handleRoot);
  server.on("/text",    HTTP_GET,  handleText);
  server.on("/clear",   HTTP_GET,  handleClear);
  server.on("/newline", HTTP_GET,  handleNewline);
  server.on("/size",    HTTP_GET,  handleSize);
  server.on("/color",   HTTP_GET,  handleColor);
  server.on("/image",   HTTP_POST, handleImageDone, handleImageUpload);
  server.on("/video",     HTTP_POST, handleVideoDone,    handleVideoUpload);
  server.on("/vid/play",  HTTP_GET,  handleVidPlay);
  server.on("/vid/pause", HTTP_GET,  handleVidPause);
  server.on("/vid/stop",  HTTP_GET,  handleVidStop);
  server.onNotFound(handleNotFound);

  server.begin();
  Serial.println("HTTP server on port 80");
  Serial.println("=== ready ===");
}

void loop() {
  server.handleClient();
  vidTick();
}
