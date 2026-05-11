/*
 * NV3030B + PCM5102 Double Pendulum
 * Landscape display, dual encoder control.
 *
 * The X and Y position of the tip of the second pendulum is sent
 * to the L and R audio channels respectively (literal chaos audio).
 *
 * Encoder 1 (GP31 SW, GP33 A, GP32 B) - SPEED:
 *   Rotate = simulation speed up/down
 *   Press  = reset speed to default (1.0x)
 *
 * Encoder 2 (GP28 SW, GP30 A, GP29 B) - PARAMETERS:
 *   Rotate = adjust currently-selected parameter
 *   Press  = cycle through parameters:
 *           L1 (length 1) → L2 (length 2) →
 *           M1 (mass 1)   → M2 (mass 2)   →
 *           GRAV (gravity)→ DAMP (damping)→ TRAIL → back
 *
 * Board: "Raspberry Pi Pico 2"
 */

#include <SPI.h>
#include <I2S.h>
#include "hardware/dma.h"
#include "hardware/spi.h"
#include "hardware/gpio.h"
#include <math.h>

// ============================================================
// Pins
// ============================================================
#define PIN_SCK  14
#define PIN_MOSI 15
#define PIN_CS   13
#define PIN_DC   12
#define PIN_RST  16
#define PIN_BLK  17

#define PIN_BCK  6
#define PIN_DIN  8

#define ENC1_SW  31
#define ENC1_A   33
#define ENC1_B   32

#define ENC2_SW  28
#define ENC2_A   30
#define ENC2_B   29

// ============================================================
// Display (landscape)
// ============================================================
#define W    280
#define H    240
#define NPIX (W * H)
#define BSIZ (NPIX * 2)

// ============================================================
// Audio
// ============================================================
#define SAMPLE_RATE  48000
#define BITS         16

// ============================================================
// Pendulum state
// ============================================================
static volatile float p_theta1 = M_PI * 0.5f;   // angle of pendulum 1 (rad)
static volatile float p_theta2 = M_PI * 0.5f;
static volatile float p_omega1 = 0.0f;          // angular velocity
static volatile float p_omega2 = 0.0f;

// Parameters (all volatile because core 1 reads them)
static volatile float p_L1   = 1.0f;            // length 1 (normalized)
static volatile float p_L2   = 1.0f;
static volatile float p_M1   = 1.0f;            // mass 1
static volatile float p_M2   = 1.0f;
static volatile float p_grav = 9.81f;
static volatile float p_damp = 0.0f;            // damping (energy loss)
static volatile float p_speed = 1.0f;           // sim speed multiplier

// Trail length parameter (number of points to remember)
#define MAX_TRAIL 200
static volatile int p_trail_len = 80;

// Reported tip position (core 1 reads, core 0 displays)
static volatile float tip_x = 0.0f;
static volatile float tip_y = 0.0f;

// ============================================================
// Trail buffer (xy positions of tip in pixels)
// ============================================================
static int16_t trail_x[MAX_TRAIL];
static int16_t trail_y[MAX_TRAIL];
static int trail_head = 0;
static int trail_count = 0;

// ============================================================
// Framebuffers
// ============================================================
static uint16_t __attribute__((aligned(4))) fb0[NPIX];
static uint16_t __attribute__((aligned(4))) fb1[NPIX];
static uint16_t* back  = fb0;
static uint16_t* front = fb1;
static int dma_ch = -1;
static bool dma_on = false;
SPISettings spiCfg(125000000, MSBFIRST, SPI_MODE0);
I2S i2s(OUTPUT);

// ============================================================
// LCD helpers
// ============================================================
static void lcmd(uint8_t c) { digitalWrite(PIN_DC,LOW); digitalWrite(PIN_CS,LOW); SPI1.transfer(c); digitalWrite(PIN_CS,HIGH); }
static void ldat(uint8_t d) { digitalWrite(PIN_DC,HIGH); digitalWrite(PIN_CS,LOW); SPI1.transfer(d); digitalWrite(PIN_CS,HIGH); }
static void lreg(uint8_t c, const uint8_t* d, int n) { lcmd(c); for(int i=0;i<n;i++) ldat(d[i]); }
#define R1(c,a)           do{uint8_t d[]={a};             lreg(c,d,1);}while(0)
#define R2(c,a,b)         do{uint8_t d[]={a,b};           lreg(c,d,2);}while(0)
#define R3(c,a,b,e)       do{uint8_t d[]={a,b,e};         lreg(c,d,3);}while(0)
#define R4(c,a,b,e,f)     do{uint8_t d[]={a,b,e,f};       lreg(c,d,4);}while(0)
#define R5(c,a,b,e,f,g)   do{uint8_t d[]={a,b,e,f,g};     lreg(c,d,5);}while(0)
#define R6(c,a,b,e,f,g,h) do{uint8_t d[]={a,b,e,f,g,h};   lreg(c,d,6);}while(0)
#define R8(c,a,b,e,f,g,h,i,j) do{uint8_t d[]={a,b,e,f,g,h,i,j};lreg(c,d,8);}while(0)

static void lcd_init() {
  digitalWrite(PIN_RST,HIGH); delay(5);
  digitalWrite(PIN_RST,LOW);  delay(5);
  digitalWrite(PIN_RST,HIGH); delay(50);
  lcmd(0x01); delay(150);
  R2(0xFD,0x06,0x08); R2(0x61,0x07,0x04);
  R3(0x62,0x00,0x44,0x45); R4(0x63,0x41,0x07,0x12,0x12);
  R1(0x64,0x37); R3(0x65,0x09,0x10,0x21);
  R3(0x66,0x09,0x10,0x21); R2(0x67,0x20,0x40);
  R4(0x68,0x90,0x4C,0x7C,0x66);
  R3(0xB1,0x0F,0x02,0x01); R1(0xB4,0x01);
  R4(0xB5,0x02,0x02,0x0A,0x14);
  R5(0xB6,0x04,0x01,0x9F,0x00,0x02);
  R1(0xDF,0x11);
  R6(0xE2,0x13,0x00,0x00,0x30,0x33,0x3F);
  R6(0xE5,0x3F,0x33,0x30,0x00,0x00,0x13);
  R2(0xE1,0x00,0x57); R2(0xE4,0x58,0x00);
  R8(0xE0,0x01,0x03,0x0D,0x0E,0x0E,0x0C,0x15,0x19);
  R8(0xE3,0x1A,0x16,0x0C,0x0F,0x0E,0x0D,0x02,0x01);
  R2(0xE6,0x00,0xFF);
  R6(0xE7,0x01,0x04,0x03,0x03,0x00,0x12);
  R3(0xE8,0x00,0x70,0x00); R1(0xEC,0x52);
  R3(0xF1,0x01,0x01,0x02); R4(0xF6,0x09,0x10,0x00,0x00);
  R2(0xFD,0xFA,0xFC);
  R1(0x35,0x00); R1(0x3A,0x05);
  R1(0x36,0x68);  // 90° CW
  lcmd(0x21); lcmd(0x11); delay(120);
  lcmd(0x29); delay(50);
  digitalWrite(PIN_BLK, HIGH);
}

// ============================================================
// DMA
// ============================================================
static void dma_setup() {
  dma_ch = dma_claim_unused_channel(true);
  dma_channel_config cfg = dma_channel_get_default_config(dma_ch);
  channel_config_set_transfer_data_size(&cfg, DMA_SIZE_8);
  channel_config_set_dreq(&cfg, spi_get_dreq(spi1, true));
  channel_config_set_read_increment(&cfg, true);
  channel_config_set_write_increment(&cfg, false);
  dma_channel_configure(dma_ch, &cfg, &spi_get_hw(spi1)->dr, nullptr, BSIZ, false);
}

static void dma_wait() {
  if (!dma_on) return;
  dma_channel_wait_for_finish_blocking(dma_ch);
  while (spi_is_busy(spi1)) tight_loop_contents();
  digitalWrite(PIN_CS, HIGH);
  dma_on = false;
}

static void swap_and_send() {
  dma_wait();
  uint16_t* t = back; back = front; front = t;
  digitalWrite(PIN_CS, LOW);
  digitalWrite(PIN_DC, LOW); SPI1.transfer(0x2A);
  digitalWrite(PIN_DC, HIGH);
  SPI1.transfer(0x00); SPI1.transfer(0x14);
  SPI1.transfer(0x01); SPI1.transfer(0x2B);
  digitalWrite(PIN_DC, LOW); SPI1.transfer(0x2B);
  digitalWrite(PIN_DC, HIGH);
  SPI1.transfer(0x00); SPI1.transfer(0x00);
  SPI1.transfer(0x00); SPI1.transfer(0xEF);
  digitalWrite(PIN_DC, LOW); SPI1.transfer(0x2C);
  digitalWrite(PIN_DC, HIGH);
  dma_channel_set_read_addr(dma_ch, front, false);
  dma_channel_set_trans_count(dma_ch, BSIZ, true);
  dma_on = true;
}

// ============================================================
// Drawing
// ============================================================
static inline uint16_t rgb(uint8_t r, uint8_t g, uint8_t b) {
  uint16_t c = ((r&0xF8)<<8)|((g&0xFC)<<3)|(b>>3);
  return (c>>8)|(c<<8);
}

static inline void px(int x, int y, uint16_t c) {
  if ((unsigned)x < W && (unsigned)y < H) back[y * W + x] = c;
}

static void drawrect(int x0, int y0, int w, int h, uint16_t c) {
  for (int y = max(0,y0); y < min(H,y0+h); y++)
    for (int x = max(0,x0); x < min(W,x0+w); x++)
      back[y * W + x] = c;
}

static void hline(int x0, int x1, int y, uint16_t c) {
  if (y < 0 || y >= H) return;
  x0 = max(0, x0); x1 = min(W-1, x1);
  for (int x = x0; x <= x1; x++) back[y * W + x] = c;
}

static void thick_line(int x0, int y0, int x1, int y1, uint16_t c, int thick) {
  int dx = abs(x1 - x0), sx = x0 < x1 ? 1 : -1;
  int dy = -abs(y1 - y0), sy = y0 < y1 ? 1 : -1;
  int err = dx + dy, half = thick / 2;
  while (true) {
    for (int t = -half; t <= half; t++) {
      int py = y0 + t;
      if ((unsigned)x0 < W && (unsigned)py < H) back[py * W + x0] = c;
      // Also expand horizontally for thicker diagonals
      int px2 = x0 + t;
      if ((unsigned)px2 < W && (unsigned)y0 < H) back[y0 * W + px2] = c;
    }
    if (x0 == x1 && y0 == y1) break;
    int e2 = 2 * err;
    if (e2 >= dy) { err += dy; x0 += sx; }
    if (e2 <= dx) { err += dx; y0 += sy; }
  }
}

static void fill_circle(int cx, int cy, int r, uint16_t c) {
  int r2 = r * r;
  int y0 = max(0, cy - r), y1 = min(H - 1, cy + r);
  for (int y = y0; y <= y1; y++) {
    int dy = y - cy;
    int dx = (int)sqrtf((float)(r2 - dy * dy));
    int x0 = max(0, cx - dx), x1 = min(W - 1, cx + dx);
    uint16_t* row = &back[y * W + x0];
    for (int x = x0; x <= x1; x++) *row++ = c;
  }
}

// --- Font ---
static const uint8_t font[][5] = {
  {0x3E,0x51,0x49,0x45,0x3E},{0x00,0x42,0x7F,0x40,0x00}, // 0 1
  {0x42,0x61,0x51,0x49,0x46},{0x21,0x41,0x45,0x4B,0x31}, // 2 3
  {0x18,0x14,0x12,0x7F,0x10},{0x27,0x45,0x45,0x45,0x39}, // 4 5
  {0x3C,0x4A,0x49,0x49,0x30},{0x01,0x71,0x09,0x05,0x03}, // 6 7
  {0x36,0x49,0x49,0x49,0x36},{0x06,0x49,0x49,0x29,0x1E}, // 8 9
  {0x7E,0x11,0x11,0x11,0x7E},{0x7F,0x49,0x49,0x49,0x36}, // A B
  {0x3E,0x41,0x41,0x41,0x22},{0x7F,0x41,0x41,0x22,0x1C}, // C D
  {0x7F,0x49,0x49,0x49,0x41},{0x7F,0x09,0x09,0x09,0x01}, // E F
  {0x3E,0x41,0x49,0x49,0x7A},{0x7F,0x08,0x08,0x08,0x7F}, // G H
  {0x00,0x41,0x7F,0x41,0x00},{0x20,0x40,0x41,0x3F,0x01}, // I J
  {0x7F,0x08,0x14,0x22,0x41},{0x7F,0x40,0x40,0x40,0x40}, // K L
  {0x7F,0x02,0x0C,0x02,0x7F},{0x7F,0x04,0x08,0x10,0x7F}, // M N
  {0x3E,0x41,0x41,0x41,0x3E},{0x7F,0x09,0x09,0x09,0x06}, // O P
  {0x3E,0x41,0x51,0x21,0x5E},{0x7F,0x09,0x19,0x29,0x46}, // Q R
  {0x46,0x49,0x49,0x49,0x31},{0x01,0x01,0x7F,0x01,0x01}, // S T
  {0x3F,0x40,0x40,0x40,0x3F},{0x1F,0x20,0x40,0x20,0x1F}, // U V
  {0x3F,0x40,0x38,0x40,0x3F},{0x63,0x14,0x08,0x14,0x63}, // W X
  {0x07,0x08,0x70,0x08,0x07},{0x61,0x51,0x49,0x45,0x43}, // Y Z
  {0x00,0x00,0x00,0x00,0x00},{0x00,0x60,0x60,0x00,0x00}, // sp .
};

static void text(int x, int y, const char* s, uint16_t c, int sc) {
  while (*s) {
    int idx = -1;
    if (*s>='0' && *s<='9') idx = *s-'0';
    else if (*s>='A' && *s<='Z') idx = *s-'A'+10;
    else if (*s>='a' && *s<='z') idx = *s-'a'+10;
    else if (*s==' ') idx = 36;
    else if (*s=='.') idx = 37;
    if (idx >= 0) {
      const uint8_t* g = font[idx];
      for (int col=0; col<5; col++)
        for (int row=0; row<7; row++)
          if (g[col] & (1<<row))
            drawrect(x+col*sc, y+row*sc, sc, sc, c);
    }
    x += 6*sc;
    s++;
  }
}

// ============================================================
// Encoder (parallel arrays, GPIO SDK)
// ============================================================
static int      enc_pin_a[2]      = { ENC1_A, ENC2_A };
static int      enc_pin_b[2]      = { ENC1_B, ENC2_B };
static int      enc_pin_sw[2]     = { ENC1_SW, ENC2_SW };
static uint8_t  enc_last_ab[2]    = { 0, 0 };
static bool     enc_sw_last[2]    = { true, true };
static uint32_t enc_sw_debounce[2]= { 0, 0 };

static void enc_init(int idx) {
  gpio_init(enc_pin_a[idx]);
  gpio_set_dir(enc_pin_a[idx], GPIO_IN);
  gpio_pull_up(enc_pin_a[idx]);
  gpio_init(enc_pin_b[idx]);
  gpio_set_dir(enc_pin_b[idx], GPIO_IN);
  gpio_pull_up(enc_pin_b[idx]);
  gpio_init(enc_pin_sw[idx]);
  gpio_set_dir(enc_pin_sw[idx], GPIO_IN);
  gpio_pull_up(enc_pin_sw[idx]);
  enc_last_ab[idx] = (gpio_get(enc_pin_a[idx]) << 1) | gpio_get(enc_pin_b[idx]);
  enc_sw_last[idx] = true;
  enc_sw_debounce[idx] = 0;
}

static int enc_read(int idx) {
  uint8_t a = gpio_get(enc_pin_a[idx]);
  uint8_t b = gpio_get(enc_pin_b[idx]);
  uint8_t ab = (a << 1) | b;
  int dir = 0;
  if (ab != enc_last_ab[idx]) {
    uint8_t transition = (enc_last_ab[idx] << 2) | ab;
    switch (transition) {
      case 0b0001: case 0b0111: case 0b1110: case 0b1000: dir =  1; break;
      case 0b0010: case 0b1011: case 0b1101: case 0b0100: dir = -1; break;
    }
    enc_last_ab[idx] = ab;
  }
  return dir;
}

static bool enc_button(int idx) {
  bool state = gpio_get(enc_pin_sw[idx]);
  uint32_t now = millis();
  if (state != enc_sw_last[idx] && (now - enc_sw_debounce[idx]) > 50) {
    enc_sw_debounce[idx] = now;
    enc_sw_last[idx] = state;
    if (!state) return true;
  }
  return false;
}

// ============================================================
// Parameters
// ============================================================
enum Param { P_L1=0, P_L2, P_M1, P_M2, P_GRAV, P_DAMP, P_TRAIL, P_COUNT };
static const char* param_names[] = { "L1", "L2", "M1", "M2", "GRAV", "DAMP", "TRAIL" };
static int current_param = P_L1;

static void adjust_param(int dir) {
  switch (current_param) {
    case P_L1:    p_L1 = constrain(p_L1 + dir * 0.05f, 0.2f, 2.0f); break;
    case P_L2:    p_L2 = constrain(p_L2 + dir * 0.05f, 0.2f, 2.0f); break;
    case P_M1:    p_M1 = constrain(p_M1 + dir * 0.1f, 0.1f, 5.0f); break;
    case P_M2:    p_M2 = constrain(p_M2 + dir * 0.1f, 0.1f, 5.0f); break;
    case P_GRAV:  p_grav = constrain(p_grav + dir * 0.5f, 0.0f, 30.0f); break;
    case P_DAMP:  p_damp = constrain(p_damp + dir * 0.005f, 0.0f, 0.5f); break;
    case P_TRAIL: p_trail_len = constrain(p_trail_len + dir * 5, 0, MAX_TRAIL); break;
  }
}

static float get_param_value(int p) {
  switch (p) {
    case P_L1:    return p_L1;
    case P_L2:    return p_L2;
    case P_M1:    return p_M1;
    case P_M2:    return p_M2;
    case P_GRAV:  return p_grav;
    case P_DAMP:  return p_damp;
    case P_TRAIL: return (float)p_trail_len;
  }
  return 0;
}

// ============================================================
// Double pendulum physics (RK4 integration on core 1)
// State is (t1, t2, w1, w2) passed as separate args to avoid
// Arduino preprocessor issues with struct types in prototypes.
// ============================================================

// Returns derivatives via output pointers
static void derivs(float t1, float t2, float w1, float w2,
                   float* dt1, float* dt2, float* dw1, float* dw2) {
  float L1 = p_L1, L2 = p_L2, m1 = p_M1, m2 = p_M2, g = p_grav;
  float damp = p_damp;

  float dt = t1 - t2;
  float sin_dt = sinf(dt);
  float cos_dt = cosf(dt);
  float sin_t1 = sinf(t1);

  float den = 2.0f * m1 + m2 - m2 * cosf(2.0f * dt);
  if (fabsf(den) < 1e-6f) den = 1e-6f;

  float num1 = -g * (2.0f * m1 + m2) * sin_t1
               - m2 * g * sinf(t1 - 2.0f * t2)
               - 2.0f * sin_dt * m2 * (w2 * w2 * L2 + w1 * w1 * L1 * cos_dt);
  float a1 = num1 / (L1 * den);

  float num2 = 2.0f * sin_dt
               * (w1 * w1 * L1 * (m1 + m2)
                  + g * (m1 + m2) * cosf(t1)
                  + w2 * w2 * L2 * m2 * cos_dt);
  float a2 = num2 / (L2 * den);

  *dt1 = w1;
  *dt2 = w2;
  *dw1 = a1 - damp * w1;
  *dw2 = a2 - damp * w2;
}

static void rk4_step(float* t1, float* t2, float* w1, float* w2, float h) {
  float k1_t1, k1_t2, k1_w1, k1_w2;
  float k2_t1, k2_t2, k2_w1, k2_w2;
  float k3_t1, k3_t2, k3_w1, k3_w2;
  float k4_t1, k4_t2, k4_w1, k4_w2;

  derivs(*t1, *t2, *w1, *w2, &k1_t1, &k1_t2, &k1_w1, &k1_w2);

  derivs(*t1 + 0.5f*h*k1_t1, *t2 + 0.5f*h*k1_t2,
         *w1 + 0.5f*h*k1_w1, *w2 + 0.5f*h*k1_w2,
         &k2_t1, &k2_t2, &k2_w1, &k2_w2);

  derivs(*t1 + 0.5f*h*k2_t1, *t2 + 0.5f*h*k2_t2,
         *w1 + 0.5f*h*k2_w1, *w2 + 0.5f*h*k2_w2,
         &k3_t1, &k3_t2, &k3_w1, &k3_w2);

  derivs(*t1 + h*k3_t1, *t2 + h*k3_t2,
         *w1 + h*k3_w1, *w2 + h*k3_w2,
         &k4_t1, &k4_t2, &k4_w1, &k4_w2);

  *t1 += (h/6.0f) * (k1_t1 + 2*k2_t1 + 2*k3_t1 + k4_t1);
  *t2 += (h/6.0f) * (k1_t2 + 2*k2_t2 + 2*k3_t2 + k4_t2);
  *w1 += (h/6.0f) * (k1_w1 + 2*k2_w1 + 2*k3_w1 + k4_w1);
  *w2 += (h/6.0f) * (k1_w2 + 2*k2_w2 + 2*k3_w2 + k4_w2);
}

// ============================================================
// Colors (all green theme)
// ============================================================
static uint16_t col_bg, col_grid, col_dim, col_text;
static uint16_t col_arm1, col_arm2, col_bob1, col_bob2, col_pivot;
static uint16_t col_trail, col_trail_dim, col_param, col_value;
static uint16_t col_speed_norm, col_speed_alt;

// ============================================================
// FPS
// ============================================================
static float fps = 0;
static uint32_t fps_count = 0;
static uint32_t fps_timer = 0;

static void fps_tick() {
  fps_count++;
  uint32_t now = millis();
  if (now - fps_timer >= 500) {
    fps = fps_count * 1000.0f / (now - fps_timer);
    fps_count = 0;
    fps_timer = now;
  }
}

// ============================================================
// Pendulum render
// ============================================================
#define PIVOT_X (W / 2)
#define PIVOT_Y (H / 2 - 10)
#define PX_PER_UNIT 45.0f   // pixel scale per unit length

static void draw_pendulum() {
  memset(back, 0, BSIZ);

  // Read state atomically-ish (worst case is one frame of tearing, fine for visuals)
  float t1 = p_theta1, t2 = p_theta2;
  float L1 = p_L1, L2 = p_L2;
  float M1 = p_M1, M2 = p_M2;

  // Compute joint positions
  int x0 = PIVOT_X;
  int y0 = PIVOT_Y;
  int x1 = x0 + (int)(L1 * PX_PER_UNIT * sinf(t1));
  int y1 = y0 + (int)(L1 * PX_PER_UNIT * cosf(t1));
  int x2 = x1 + (int)(L2 * PX_PER_UNIT * sinf(t2));
  int y2 = y1 + (int)(L2 * PX_PER_UNIT * cosf(t2));

  // Add tip to trail
  trail_x[trail_head] = x2;
  trail_y[trail_head] = y2;
  trail_head = (trail_head + 1) % MAX_TRAIL;
  if (trail_count < MAX_TRAIL) trail_count++;

  // Draw trail (oldest = darkest, newest = brightest)
  int trail_to_draw = min((int)p_trail_len, trail_count);
  for (int i = 0; i < trail_to_draw - 1; i++) {
    int idx_a = (trail_head - trail_to_draw + i + MAX_TRAIL) % MAX_TRAIL;
    int idx_b = (trail_head - trail_to_draw + i + 1 + MAX_TRAIL) % MAX_TRAIL;
    // Brightness ramp from dim to bright
    float fade = (float)i / (float)trail_to_draw;
    uint8_t g = (uint8_t)(40 + fade * 180);
    uint8_t r = (uint8_t)(fade * 30);
    uint16_t c = rgb(r, g, r);
    int ax = trail_x[idx_a], ay = trail_y[idx_a];
    int bx = trail_x[idx_b], by = trail_y[idx_b];
    thick_line(ax, ay, bx, by, c, 1);
  }

  // Draw arms (thick lines)
  thick_line(x0, y0, x1, y1, col_arm1, 2);
  thick_line(x1, y1, x2, y2, col_arm2, 2);

  // Pivot
  fill_circle(x0, y0, 4, col_pivot);

  // Bobs (size scaled by mass)
  int r1 = (int)(4 + sqrtf(M1) * 3);
  int r2 = (int)(4 + sqrtf(M2) * 3);
  fill_circle(x1, y1, r1, col_bob1);
  fill_circle(x2, y2, r2, col_bob2);
}

// ============================================================
// Draw HUD overlay
// ============================================================
static void draw_hud() {
  // --- Title ---
  text(20, 8, "DOUBLE PENDULUM", col_text, 2);

  // --- Speed indicator (top right) ---
  char sbuf[16];
  float speed = p_speed;
  if (fabsf(speed - 1.0f) < 0.01f) {
    snprintf(sbuf, sizeof(sbuf), "%.2fX", (double)speed);
    text(W - 60, 10, sbuf, col_speed_norm, 1);
  } else {
    snprintf(sbuf, sizeof(sbuf), "%.2fX", (double)speed);
    text(W - 60, 10, sbuf, col_speed_alt, 1);
  }

  // --- Parameter panel (left side) ---
  int px_y = 30;
  for (int i = 0; i < P_COUNT; i++) {
    uint16_t name_col = (i == current_param) ? col_param : col_dim;
    uint16_t val_col  = (i == current_param) ? col_value : col_dim;

    // Name
    text(4, px_y + i * 11, param_names[i], name_col, 1);

    // Value
    char vbuf[12];
    float v = get_param_value(i);
    if (i == P_TRAIL) {
      snprintf(vbuf, sizeof(vbuf), "%d", (int)v);
    } else if (i == P_GRAV) {
      snprintf(vbuf, sizeof(vbuf), "%.1f", (double)v);
    } else if (i == P_DAMP) {
      snprintf(vbuf, sizeof(vbuf), "%.3f", (double)v);
    } else {
      snprintf(vbuf, sizeof(vbuf), "%.2f", (double)v);
    }
    text(34, px_y + i * 11, vbuf, val_col, 1);

    // Selector indicator (>)
    if (i == current_param) {
      text(0, px_y + i * 11, ".", col_param, 1);  // small marker
    }
  }

  // --- Bottom info ---
  text(4, H - 20, "ENC1 SPEED", col_dim, 1);
  text(W - 100, H - 20, "ENC2 PARAM", col_dim, 1);

  // FPS center bottom
  char fbuf[16];
  snprintf(fbuf, sizeof(fbuf), "%d.%d FPS", (int)fps, ((int)(fps*10))%10);
  int flen = strlen(fbuf);
  text((W - flen * 6) / 2, H - 10, fbuf, col_dim, 1);
}

// ============================================================
// CORE 1: Audio + physics
// ============================================================
// Run physics here too, at audio rate. Each sample we step the
// physics by dt = 1/SAMPLE_RATE * speed. The X/Y position of the
// tip are normalized and sent to L/R audio channels.

void setup1() {}

void loop1() {
  static const int BATCH = 32;
  static int16_t buf_l[BATCH];
  static int16_t buf_r[BATCH];

  float speed = p_speed;
  float L1 = p_L1, L2 = p_L2;
  float total_L = L1 + L2;
  float dt_audio = (1.0f / (float)SAMPLE_RATE) * speed * 4.0f;

  // Local state copy for speed (avoid volatile access inside RK4)
  float t1 = p_theta1, t2 = p_theta2;
  float w1 = p_omega1, w2 = p_omega2;

  for (int i = 0; i < BATCH; i++) {
    rk4_step(&t1, &t2, &w1, &w2, dt_audio);

    // Compute tip XY
    float x = L1 * sinf(t1) + L2 * sinf(t2);
    float y = L1 * cosf(t1) + L2 * cosf(t2);

    // Normalize to ±1 range
    float nx = x / total_L;
    float ny = y / total_L;

    if (nx > 1.0f) nx = 1.0f; else if (nx < -1.0f) nx = -1.0f;
    if (ny > 1.0f) ny = 1.0f; else if (ny < -1.0f) ny = -1.0f;

    buf_l[i] = (int16_t)(nx * 32000.0f);
    buf_r[i] = (int16_t)(ny * 32000.0f);
  }

  // Write back to shared state
  p_theta1 = t1; p_theta2 = t2;
  p_omega1 = w1; p_omega2 = w2;

  // Tip in normalized coords for display
  float total = p_L1 + p_L2;
  tip_x = (p_L1 * sinf(t1) + p_L2 * sinf(t2)) / total;
  tip_y = (p_L1 * cosf(t1) + p_L2 * cosf(t2)) / total;

  // Send batch to I2S
  for (int i = 0; i < BATCH; i++) {
    i2s.write(buf_l[i]);
    i2s.write(buf_r[i]);
  }
}

// ============================================================
// CORE 0: Setup
// ============================================================
void setup() {
  Serial.begin(115200);

  pinMode(PIN_CS, OUTPUT); pinMode(PIN_DC, OUTPUT);
  pinMode(PIN_RST, OUTPUT); pinMode(PIN_BLK, OUTPUT);
  digitalWrite(PIN_CS, HIGH); digitalWrite(PIN_BLK, LOW);

  SPI1.setTX(PIN_MOSI); SPI1.setSCK(PIN_SCK);
  SPI1.begin(); SPI1.beginTransaction(spiCfg);

  delay(100);
  lcd_init();
  dma_setup();
  memset(fb0, 0, BSIZ);
  memset(fb1, 0, BSIZ);

  // I2S
  i2s.setBCLK(PIN_BCK);
  i2s.setDATA(PIN_DIN);
  i2s.setBitsPerSample(BITS);
  i2s.setBuffers(8, 256);
  if (!i2s.begin(SAMPLE_RATE)) {
    Serial.println("I2S failed");
    while (1) delay(100);
  }

  enc_init(0);
  enc_init(1);

  // All-green theme
  col_bg          = rgb(0, 0, 0);
  col_grid        = rgb(40, 40, 50);     // grey grid (unchanged)
  col_dim         = rgb(100, 100, 120);  // grey dim (unchanged)
  col_text        = rgb(200, 200, 200);  // grey light (unchanged)
  col_arm1        = rgb(0, 220, 100);    // bright green arm 1
  col_arm2        = rgb(0, 200, 80);     // green arm 2
  col_bob1        = rgb(0, 255, 120);    // bright green bob 1
  col_bob2        = rgb(180, 255, 100);  // yellow-green bob 2 (tip)
  col_pivot       = rgb(0, 160, 70);     // darker green pivot
  col_trail       = rgb(0, 180, 60);     // trail bright
  col_trail_dim   = rgb(0, 60, 20);      // trail dim
  col_param       = rgb(0, 255, 100);    // selected param: bright green
  col_value       = rgb(180, 255, 100);  // selected value: yellow-green
  col_speed_norm  = rgb(0, 200, 80);     // speed at default
  col_speed_alt   = rgb(180, 255, 100);  // speed when changed

  // Initial conditions: chaotic starting position
  p_theta1 = M_PI * 0.75f;
  p_theta2 = M_PI * 0.85f;
  p_omega1 = 0.0f;
  p_omega2 = 0.0f;

  Serial.println("Double Pendulum Ready");
}

// ============================================================
// CORE 0: Main loop
// ============================================================
void loop() {
  // --- Encoder 1: Speed ---
  int dir1 = enc_read(0);
  if (dir1) {
    p_speed = constrain(p_speed * powf(1.06f, dir1), 0.05f, 8.0f);
  }
  if (enc_button(0)) {
    p_speed = 1.0f;  // reset to default
  }

  // --- Encoder 2: Parameter ---
  int dir2 = enc_read(1);
  if (dir2) {
    adjust_param(dir2);
  }
  if (enc_button(1)) {
    current_param = (current_param + 1) % P_COUNT;
  }

  // Render
  draw_pendulum();
  draw_hud();
  swap_and_send();
  fps_tick();
}
