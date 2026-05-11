/*
 * NV3030B + PCM5102 - TEMPEST CLONE
 * Vector-style arcade game inspired by Atari Tempest (1981).
 *
 * Encoder 1 (GP31 SW, GP33 A, GP32 B):
 *   Rotate = move ship around the rim
 *   Press  = SUPERZAPPER (clears all enemies, 1 per level)
 *
 * Encoder 2 (GP28 SW, GP30 A, GP29 B):
 *   Rotate = unused
 *   Press  = FIRE
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
// Display
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
// Game constants
// ============================================================
#define NUM_LANES        12
#define MAX_ENEMIES      24
#define MAX_BULLETS      8
#define MAX_ENEMY_BULLETS 8
#define DEPTH_FAR        1.0f
#define DEPTH_NEAR       0.0f

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

static void line(int x0, int y0, int x1, int y1, uint16_t c) {
  int dx = abs(x1 - x0), sx = x0 < x1 ? 1 : -1;
  int dy = -abs(y1 - y0), sy = y0 < y1 ? 1 : -1;
  int err = dx + dy;
  while (true) {
    px(x0, y0, c);
    if (x0 == x1 && y0 == y1) break;
    int e2 = 2 * err;
    if (e2 >= dy) { err += dy; x0 += sx; }
    if (e2 <= dx) { err += dx; y0 += sy; }
  }
}

static void thick_line(int x0, int y0, int x1, int y1, uint16_t c, int thick) {
  int dx = abs(x1 - x0), sx = x0 < x1 ? 1 : -1;
  int dy = -abs(y1 - y0), sy = y0 < y1 ? 1 : -1;
  int err = dx + dy, half = thick / 2;
  while (true) {
    for (int t = -half; t <= half; t++) {
      int py2 = y0 + t;
      if ((unsigned)x0 < W && (unsigned)py2 < H) back[py2 * W + x0] = c;
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
  {0x3E,0x51,0x49,0x45,0x3E},{0x00,0x42,0x7F,0x40,0x00},
  {0x42,0x61,0x51,0x49,0x46},{0x21,0x41,0x45,0x4B,0x31},
  {0x18,0x14,0x12,0x7F,0x10},{0x27,0x45,0x45,0x45,0x39},
  {0x3C,0x4A,0x49,0x49,0x30},{0x01,0x71,0x09,0x05,0x03},
  {0x36,0x49,0x49,0x49,0x36},{0x06,0x49,0x49,0x29,0x1E},
  {0x7E,0x11,0x11,0x11,0x7E},{0x7F,0x49,0x49,0x49,0x36},
  {0x3E,0x41,0x41,0x41,0x22},{0x7F,0x41,0x41,0x22,0x1C},
  {0x7F,0x49,0x49,0x49,0x41},{0x7F,0x09,0x09,0x09,0x01},
  {0x3E,0x41,0x49,0x49,0x7A},{0x7F,0x08,0x08,0x08,0x7F},
  {0x00,0x41,0x7F,0x41,0x00},{0x20,0x40,0x41,0x3F,0x01},
  {0x7F,0x08,0x14,0x22,0x41},{0x7F,0x40,0x40,0x40,0x40},
  {0x7F,0x02,0x0C,0x02,0x7F},{0x7F,0x04,0x08,0x10,0x7F},
  {0x3E,0x41,0x41,0x41,0x3E},{0x7F,0x09,0x09,0x09,0x06},
  {0x3E,0x41,0x51,0x21,0x5E},{0x7F,0x09,0x19,0x29,0x46},
  {0x46,0x49,0x49,0x49,0x31},{0x01,0x01,0x7F,0x01,0x01},
  {0x3F,0x40,0x40,0x40,0x3F},{0x1F,0x20,0x40,0x20,0x1F},
  {0x3F,0x40,0x38,0x40,0x3F},{0x63,0x14,0x08,0x14,0x63},
  {0x07,0x08,0x70,0x08,0x07},{0x61,0x51,0x49,0x45,0x43},
  {0x00,0x00,0x00,0x00,0x00},{0x00,0x60,0x60,0x00,0x00},
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
// Encoders
// ============================================================
static int      enc_pin_a[2]      = { ENC1_A, ENC2_A };
static int      enc_pin_b[2]      = { ENC1_B, ENC2_B };
static int      enc_pin_sw[2]     = { ENC1_SW, ENC2_SW };
static uint8_t  enc_last_ab[2]    = { 0, 0 };
static bool     enc_sw_last[2]    = { true, true };
static uint32_t enc_sw_debounce[2]= { 0, 0 };

static void enc_init(int idx) {
  gpio_init(enc_pin_a[idx]); gpio_set_dir(enc_pin_a[idx], GPIO_IN); gpio_pull_up(enc_pin_a[idx]);
  gpio_init(enc_pin_b[idx]); gpio_set_dir(enc_pin_b[idx], GPIO_IN); gpio_pull_up(enc_pin_b[idx]);
  gpio_init(enc_pin_sw[idx]); gpio_set_dir(enc_pin_sw[idx], GPIO_IN); gpio_pull_up(enc_pin_sw[idx]);
  enc_last_ab[idx] = (gpio_get(enc_pin_a[idx]) << 1) | gpio_get(enc_pin_b[idx]);
  enc_sw_last[idx] = true;
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
// Game state
// ============================================================
// Web geometry: NUM_LANES segments around a circle.
// Each lane has a near point (at the rim, where the player is)
// and a far point (toward the center vanishing point).
// "Depth" goes from 1.0 (far, at center) to 0.0 (near, at rim).

// Lane vertices computed in setup
static float lane_cos[NUM_LANES + 1];
static float lane_sin[NUM_LANES + 1];

// Center of web on screen (vanishing point)
#define CX (W / 2)
#define CY (H / 2 - 6)

// Rim radius (at depth=0, where player sits)
#define RIM_RADIUS 100.0f

// Player
static int player_lane = 0;        // 0..NUM_LANES-1
static int score = 0;
static int lives = 3;
static int level = 1;
static bool superzap_available = true;

// Bullets (player shots)
struct Bullet { int lane; float depth; bool active; };
static Bullet bullets[MAX_BULLETS];

// Enemies
enum EnemyType { E_FLIPPER = 0, E_TANKER, E_SPIKER };
struct Enemy {
  bool active;
  int type;
  int lane;
  float depth;        // 1.0 = far, 0.0 = at rim
  float lane_offset;  // for flippers transitioning between lanes
  float anim;         // animation timer
  int target_lane;    // for flippers
};
static Enemy enemies[MAX_ENEMIES];
static int enemies_killed = 0;
static int enemies_to_spawn = 0;
static uint32_t next_spawn_ms = 0;

// Enemy bullets
struct EBullet { int lane; float depth; bool active; };
static EBullet ebullets[MAX_ENEMY_BULLETS];

// Game phase
enum Phase { PHASE_PLAY, PHASE_WARP, PHASE_DEAD, PHASE_GAMEOVER, PHASE_TITLE };
static int phase = PHASE_TITLE;
static uint32_t phase_timer = 0;

// Audio (core 1 reads these)
static volatile uint8_t snd_fire = 0;
static volatile uint8_t snd_explode = 0;
static volatile uint8_t snd_zap = 0;
static volatile uint8_t snd_thump = 0;

// ============================================================
// Colors (green theme)
// ============================================================
static uint16_t col_bg, col_grid, col_dim, col_text;
static uint16_t col_web, col_web_far, col_player, col_player_glow;
static uint16_t col_bullet, col_enemy_flipper, col_enemy_tanker, col_enemy_spiker;
static uint16_t col_ebullet, col_score, col_zap;

// ============================================================
// Coordinate transform
// ============================================================
// Given a lane index and depth, return screen XY
static void web_xy(float lane_f, float depth, int* sx, int* sy) {
  // depth=0 → at rim, depth=1 → at center
  // Wrap lane_f
  while (lane_f < 0) lane_f += NUM_LANES;
  while (lane_f >= NUM_LANES) lane_f -= NUM_LANES;

  float angle = lane_f * 2.0f * (float)M_PI / NUM_LANES;
  float cs = cosf(angle), sn = sinf(angle);
  float r = RIM_RADIUS * (1.0f - depth);
  *sx = CX + (int)(r * sn);
  *sy = CY - (int)(r * cs);
}

// ============================================================
// Web rendering
// ============================================================
static void draw_web() {
  // Concentric "rings" for depth perspective
  // Plus radial lines for each lane

  // Draw radial lines from center to rim
  for (int i = 0; i < NUM_LANES; i++) {
    int sx0, sy0, sx1, sy1;
    web_xy((float)i, 1.0f, &sx0, &sy0);  // center
    web_xy((float)i, 0.0f, &sx1, &sy1);  // rim
    line(sx0, sy0, sx1, sy1, col_web);
  }

  // A few perspective "rings" at depths 0.0, 0.3, 0.6, 0.85
  static const float ring_depths[] = { 0.0f, 0.35f, 0.65f, 0.88f };
  for (int r = 0; r < 4; r++) {
    uint16_t c = (r == 0) ? col_web : col_web_far;
    for (int i = 0; i < NUM_LANES; i++) {
      int sx0, sy0, sx1, sy1;
      web_xy((float)i, ring_depths[r], &sx0, &sy0);
      web_xy((float)(i + 1), ring_depths[r], &sx1, &sy1);
      line(sx0, sy0, sx1, sy1, c);
    }
  }
}

// ============================================================
// Player rendering (claw shape on the rim)
// ============================================================
static void draw_player() {
  if (phase == PHASE_DEAD) return;

  // Player sits on the rim of player_lane, spanning between
  // lane_radial[player_lane] and lane_radial[player_lane + 1]
  int x0, y0, x1, y1, xm, ym;
  web_xy((float)player_lane, 0.0f, &x0, &y0);
  web_xy((float)(player_lane + 1), 0.0f, &x1, &y1);

  // Center of segment
  xm = (x0 + x1) / 2;
  ym = (y0 + y1) / 2;

  // Inward direction (toward center)
  float dx = (CX - xm) * 0.18f;
  float dy = (CY - ym) * 0.18f;

  // Claw shape: triangle pointing inward
  thick_line(x0, y0, xm + (int)dx, ym + (int)dy, col_player, 2);
  thick_line(x1, y1, xm + (int)dx, ym + (int)dy, col_player, 2);
  thick_line(x0, y0, x1, y1, col_player, 2);

  // Glow dot
  fill_circle(xm + (int)dx, ym + (int)dy, 2, col_player_glow);
}

// ============================================================
// Bullet rendering (small bright dot moving inward)
// ============================================================
static void draw_bullets() {
  for (int i = 0; i < MAX_BULLETS; i++) {
    if (!bullets[i].active) continue;
    int sx, sy;
    web_xy((float)bullets[i].lane + 0.5f, bullets[i].depth, &sx, &sy);
    fill_circle(sx, sy, 2, col_bullet);
    // Add a small streak
    int sx2, sy2;
    web_xy((float)bullets[i].lane + 0.5f, max(0.0f, bullets[i].depth - 0.05f), &sx2, &sy2);
    line(sx, sy, sx2, sy2, col_bullet);
  }
}

// ============================================================
// Enemy bullet rendering
// ============================================================
static void draw_ebullets() {
  for (int i = 0; i < MAX_ENEMY_BULLETS; i++) {
    if (!ebullets[i].active) continue;
    int sx, sy;
    web_xy((float)ebullets[i].lane + 0.5f, ebullets[i].depth, &sx, &sy);
    // Cross pattern
    int s = 1 + (int)((1.0f - ebullets[i].depth) * 2);
    drawrect(sx - s, sy, 2*s+1, 1, col_ebullet);
    drawrect(sx, sy - s, 1, 2*s+1, col_ebullet);
  }
}

// ============================================================
// Enemy rendering
// ============================================================
static void draw_enemies() {
  for (int i = 0; i < MAX_ENEMIES; i++) {
    if (!enemies[i].active) continue;

    Enemy& e = enemies[i];
    float lane_f = e.lane + e.lane_offset;
    int sx, sy;
    web_xy(lane_f + 0.5f, e.depth, &sx, &sy);

    // Size scales with depth (closer = bigger)
    int sz = (int)(2 + (1.0f - e.depth) * 6);

    if (e.type == E_FLIPPER) {
      // Flipper: X shape
      uint16_t c = col_enemy_flipper;
      // Animate rotation
      float a = e.anim;
      int r = sz;
      int dx1 = (int)(r * cosf(a)), dy1 = (int)(r * sinf(a));
      int dx2 = (int)(r * cosf(a + 1.57f)), dy2 = (int)(r * sinf(a + 1.57f));
      thick_line(sx - dx1, sy - dy1, sx + dx1, sy + dy1, c, 1);
      thick_line(sx - dx2, sy - dy2, sx + dx2, sy + dy2, c, 1);
    } else if (e.type == E_TANKER) {
      // Tanker: hexagon outline
      uint16_t c = col_enemy_tanker;
      int prev_x = sx + sz, prev_y = sy;
      for (int j = 1; j <= 6; j++) {
        float a = j * (float)M_PI / 3.0f;
        int nx = sx + (int)(sz * cosf(a));
        int ny = sy + (int)(sz * sinf(a));
        line(prev_x, prev_y, nx, ny, c);
        prev_x = nx; prev_y = ny;
      }
    } else { // E_SPIKER
      // Spiker: spiral / diamond
      uint16_t c = col_enemy_spiker;
      int s = sz;
      line(sx - s, sy, sx, sy - s, c);
      line(sx, sy - s, sx + s, sy, c);
      line(sx + s, sy, sx, sy + s, c);
      line(sx, sy + s, sx - s, sy, c);
    }
  }
}

// ============================================================
// Game logic
// ============================================================
static void reset_level() {
  // Clear arrays
  for (int i = 0; i < MAX_ENEMIES; i++) enemies[i].active = false;
  for (int i = 0; i < MAX_BULLETS; i++) bullets[i].active = false;
  for (int i = 0; i < MAX_ENEMY_BULLETS; i++) ebullets[i].active = false;

  enemies_killed = 0;
  enemies_to_spawn = 8 + level * 2;
  if (enemies_to_spawn > MAX_ENEMIES * 2) enemies_to_spawn = MAX_ENEMIES * 2;
  next_spawn_ms = millis() + 1000;
  superzap_available = true;
}

static void start_game() {
  score = 0;
  lives = 3;
  level = 1;
  player_lane = 0;
  reset_level();
  phase = PHASE_PLAY;
}

static void fire_bullet() {
  for (int i = 0; i < MAX_BULLETS; i++) {
    if (!bullets[i].active) {
      bullets[i].active = true;
      bullets[i].lane = player_lane;
      bullets[i].depth = 0.0f;
      snd_fire = 8;  // ~8 frames of fire sound
      return;
    }
  }
}

static void spawn_enemy() {
  for (int i = 0; i < MAX_ENEMIES; i++) {
    if (!enemies[i].active) {
      enemies[i].active = true;
      // Random type, biased toward flippers at low levels
      int r = rand() % 100;
      if (level >= 3 && r < 20) enemies[i].type = E_TANKER;
      else if (level >= 5 && r < 40) enemies[i].type = E_SPIKER;
      else enemies[i].type = E_FLIPPER;

      enemies[i].lane = rand() % NUM_LANES;
      enemies[i].depth = 1.0f;
      enemies[i].lane_offset = 0;
      enemies[i].anim = 0;
      enemies[i].target_lane = enemies[i].lane;
      return;
    }
  }
}

static void enemy_shoot(int idx) {
  for (int i = 0; i < MAX_ENEMY_BULLETS; i++) {
    if (!ebullets[i].active) {
      ebullets[i].active = true;
      ebullets[i].lane = enemies[idx].lane;
      ebullets[i].depth = enemies[idx].depth;
      return;
    }
  }
}

static void superzap() {
  if (!superzap_available) return;
  superzap_available = false;
  for (int i = 0; i < MAX_ENEMIES; i++) {
    if (enemies[i].active) {
      enemies[i].active = false;
      enemies_killed++;
      score += 50;
    }
  }
  for (int i = 0; i < MAX_ENEMY_BULLETS; i++) {
    ebullets[i].active = false;
  }
  snd_zap = 30;
}

static void player_die() {
  lives--;
  snd_explode = 30;
  if (lives <= 0) {
    phase = PHASE_GAMEOVER;
    phase_timer = millis();
  } else {
    phase = PHASE_DEAD;
    phase_timer = millis();
  }
}

static void update_game(float dt) {
  uint32_t now = millis();

  // --- Spawn enemies ---
  if (enemies_to_spawn > 0 && now >= next_spawn_ms) {
    spawn_enemy();
    enemies_to_spawn--;
    next_spawn_ms = now + 600 + (rand() % 800);
  }

  // --- Update bullets ---
  for (int i = 0; i < MAX_BULLETS; i++) {
    if (!bullets[i].active) continue;
    bullets[i].depth += 2.5f * dt;
    if (bullets[i].depth > 1.0f) bullets[i].active = false;

    // Check enemy collisions
    for (int j = 0; j < MAX_ENEMIES; j++) {
      if (!enemies[j].active) continue;
      // Same lane and close depth
      int el = enemies[j].lane;
      if (bullets[i].lane == el && fabsf(bullets[i].depth - enemies[j].depth) < 0.08f) {
        enemies[j].active = false;
        bullets[i].active = false;
        enemies_killed++;
        int pts = (enemies[j].type == E_FLIPPER) ? 100 :
                  (enemies[j].type == E_TANKER) ? 200 : 300;
        score += pts;
        snd_explode = 12;
        break;
      }
    }
  }

  // --- Update enemies ---
  for (int i = 0; i < MAX_ENEMIES; i++) {
    if (!enemies[i].active) continue;
    Enemy& e = enemies[i];

    // Movement: come toward player
    float speed = 0.06f + level * 0.015f;
    if (e.type == E_TANKER) speed *= 0.6f;
    if (e.type == E_SPIKER) speed *= 1.3f;
    e.depth -= speed * dt;
    e.anim += 8.0f * dt;

    // Flippers may shift lanes occasionally
    if (e.type == E_FLIPPER && e.depth < 0.7f) {
      if (e.lane_offset == 0 && (rand() % 100) < 2) {
        // Start a flip
        int dir = (rand() & 1) ? 1 : -1;
        e.target_lane = (e.lane + dir + NUM_LANES) % NUM_LANES;
        e.lane_offset = (dir > 0) ? 0.001f : -0.001f;
      }
      if (e.lane_offset != 0) {
        e.lane_offset += (e.lane_offset > 0 ? 1.0f : -1.0f) * 1.5f * dt;
        if (fabsf(e.lane_offset) >= 1.0f) {
          e.lane = e.target_lane;
          e.lane_offset = 0;
        }
      }
    }

    // Spikers shoot occasionally
    if (e.type == E_SPIKER && e.depth < 0.6f && (rand() % 200) < 1) {
      enemy_shoot(i);
    }

    // Tankers shoot more
    if (e.type == E_TANKER && e.depth < 0.5f && (rand() % 100) < 1) {
      enemy_shoot(i);
    }

    // Reached rim - if on player's lane, kill player
    if (e.depth <= 0.0f) {
      if (e.lane == player_lane) {
        e.active = false;
        player_die();
      } else {
        // Enemy survives at the rim and slides to attack
        e.depth = 0;
        // Try to flip toward player
        int diff = (player_lane - e.lane + NUM_LANES) % NUM_LANES;
        int dir = (diff <= NUM_LANES / 2) ? 1 : -1;
        e.lane = (e.lane + dir + NUM_LANES) % NUM_LANES;
        e.depth = 0;
        // Eventually reach player
        if (e.lane == player_lane) {
          e.active = false;
          player_die();
        }
      }
    }
  }

  // --- Update enemy bullets ---
  for (int i = 0; i < MAX_ENEMY_BULLETS; i++) {
    if (!ebullets[i].active) continue;
    ebullets[i].depth -= 0.8f * dt;
    if (ebullets[i].depth <= 0.0f) {
      if (ebullets[i].lane == player_lane) {
        ebullets[i].active = false;
        player_die();
      } else {
        ebullets[i].active = false;
      }
    }
  }

  // --- Level complete ---
  if (enemies_to_spawn == 0) {
    bool any_alive = false;
    for (int i = 0; i < MAX_ENEMIES; i++) {
      if (enemies[i].active) { any_alive = true; break; }
    }
    if (!any_alive) {
      level++;
      score += 500;
      reset_level();
      phase = PHASE_WARP;
      phase_timer = millis();
    }
  }
}

// ============================================================
// HUD
// ============================================================
static void draw_hud() {
  // Top: score, level, lives
  char buf[32];
  snprintf(buf, sizeof(buf), "SCORE %05d", score);
  text(4, 4, buf, col_score, 1);

  snprintf(buf, sizeof(buf), "LEVEL %d", level);
  text(W - 60, 4, buf, col_text, 1);

  // Lives as small claws bottom-left
  for (int i = 0; i < lives; i++) {
    int x = 6 + i * 14;
    int y = H - 12;
    line(x, y + 6, x + 4, y, col_player);
    line(x + 4, y, x + 8, y + 6, col_player);
    line(x, y + 6, x + 8, y + 6, col_player);
  }

  // Superzap indicator bottom-right
  if (superzap_available) {
    text(W - 60, H - 12, "ZAP READY", col_zap, 1);
  }
}

// ============================================================
// Title / Game over screens
// ============================================================
static void draw_title() {
  draw_web();

  // Pulsing title
  float pulse = (sinf(millis() * 0.005f) + 1.0f) * 0.5f;
  uint8_t bright = (uint8_t)(150 + pulse * 105);
  uint16_t title_col = rgb(0, bright, (uint8_t)(bright * 0.4f));

  text((W - 7 * 18) / 2, 50, "TEMPEST", title_col, 3);
  text((W - 7 * 12) / 2, 84, "ARCADE", col_text, 2);

  text((W - 16 * 6) / 2, 130, "ENC1 ROTATE MOVE", col_dim, 1);
  text((W - 13 * 6) / 2, 142, "ENC1 PRESS ZAP", col_dim, 1);
  text((W - 14 * 6) / 2, 154, "ENC2 PRESS FIRE", col_dim, 1);

  // Blink prompt
  if ((millis() / 400) & 1) {
    text((W - 14 * 6) / 2, 190, "PRESS TO START", col_score, 1);
  }
}

static void draw_gameover() {
  text((W - 9 * 18) / 2, 80, "GAME OVER", rgb(255, 60, 60), 3);

  char buf[24];
  snprintf(buf, sizeof(buf), "FINAL %05d", score);
  text((W - 11 * 12) / 2, 120, buf, col_score, 2);

  if ((millis() / 400) & 1) {
    text((W - 14 * 6) / 2, 170, "PRESS TO RESTART", col_dim, 1);
  }
}

// ============================================================
// Frame
// ============================================================
static uint32_t last_frame_ms = 0;

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

static void draw_frame() {
  memset(back, 0, BSIZ);

  uint32_t now = millis();
  float dt = (now - last_frame_ms) * 0.001f;
  if (dt > 0.1f) dt = 0.1f;
  last_frame_ms = now;

  if (phase == PHASE_TITLE) {
    draw_title();
  } else if (phase == PHASE_GAMEOVER) {
    draw_gameover();
  } else if (phase == PHASE_WARP) {
    draw_web();
    text((W - 12 * 12) / 2, H/2 - 8, "LEVEL UP", col_score, 2);
    if (now - phase_timer > 1500) phase = PHASE_PLAY;
  } else if (phase == PHASE_DEAD) {
    draw_web();
    draw_enemies();
    text((W - 9 * 12) / 2, H/2 - 8, "ZAPPED", rgb(255, 60, 60), 2);
    if (now - phase_timer > 1200) phase = PHASE_PLAY;
  } else {
    // Normal play
    update_game(dt);
    draw_web();
    draw_ebullets();
    draw_enemies();
    draw_bullets();
    draw_player();
    draw_hud();
  }
}

// ============================================================
// CORE 1: Audio (procedural sound effects)
// ============================================================
void setup1() {}

void loop1() {
  // Mix simple effects: sawtooth-y blips for fire,
  // noise burst for explosion, descending tone for zap.
  static uint32_t phase_fire = 0;
  static uint32_t phase_thump = 0;
  static uint32_t phase_zap = 0;
  static uint32_t rng = 0xCAFEBABE;

  // Simple xorshift for noise
  rng ^= rng << 13; rng ^= rng >> 17; rng ^= rng << 5;

  int32_t mix = 0;

  // Background "thump" (pulses with game tension)
  static uint8_t thump_t = 0;
  thump_t++;
  if (thump_t == 0) snd_thump = 60;  // wraps every ~5ms (?)
  if (snd_thump > 0) {
    phase_thump += 0x00800000;  // ~62 Hz
    int16_t v = (phase_thump & 0x80000000) ? 4000 : -4000;
    mix += v * snd_thump / 60;
    snd_thump--;
  }

  // Fire: descending zap-like
  if (snd_fire > 0) {
    phase_fire += 0x04000000 + (snd_fire * 0x00100000);
    int16_t v = (phase_fire & 0x80000000) ? 8000 : -8000;
    mix += v;
    snd_fire--;
  }

  // Explode: noise burst
  static uint8_t exp_local = 0;
  if (snd_explode > 0) { exp_local = snd_explode; snd_explode = 0; }
  if (exp_local > 0) {
    int16_t n = (int16_t)(rng & 0xFFFF) - 32768;
    mix += (n * exp_local) / 64;
    if ((rng & 7) == 0) exp_local--;
  }

  // Superzap: long sweep
  static uint8_t zap_local = 0;
  if (snd_zap > 0) { zap_local = snd_zap; snd_zap = 0; }
  if (zap_local > 0) {
    phase_zap += 0x10000000 - zap_local * 0x00400000;
    int16_t v = (phase_zap & 0x80000000) ? 12000 : -12000;
    mix += v;
    if ((rng & 31) == 0) zap_local--;
  }

  // Clamp
  if (mix > 32000) mix = 32000;
  if (mix < -32000) mix = -32000;

  i2s.write((int16_t)mix);
  i2s.write((int16_t)mix);
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

  i2s.setBCLK(PIN_BCK);
  i2s.setDATA(PIN_DIN);
  i2s.setBitsPerSample(BITS);
  i2s.setBuffers(8, 256);
  if (!i2s.begin(SAMPLE_RATE)) {
    Serial.println("I2S failed");
  }

  enc_init(0);
  enc_init(1);

  // Green theme
  col_bg            = rgb(0, 0, 0);
  col_grid          = rgb(40, 40, 50);
  col_dim           = rgb(100, 100, 120);
  col_text          = rgb(200, 200, 200);
  col_web           = rgb(0, 200, 80);
  col_web_far       = rgb(0, 80, 30);
  col_player        = rgb(180, 255, 100);
  col_player_glow   = rgb(255, 255, 200);
  col_bullet        = rgb(220, 255, 200);
  col_enemy_flipper = rgb(0, 255, 100);
  col_enemy_tanker  = rgb(0, 220, 140);
  col_enemy_spiker  = rgb(120, 255, 80);
  col_ebullet       = rgb(255, 60, 60);
  col_score         = rgb(0, 255, 100);
  col_zap           = rgb(180, 255, 100);

  Serial.println("Tempest Clone");
}

// ============================================================
// CORE 0: Main loop
// ============================================================
void loop() {
  // --- Encoder 1: Move + Superzap ---
  int dir1 = enc_read(0);
  if (dir1 && phase == PHASE_PLAY) {
    player_lane = (player_lane + dir1 + NUM_LANES) % NUM_LANES;
  }

  if (enc_button(0)) {
    if (phase == PHASE_PLAY) {
      superzap();
    } else if (phase == PHASE_TITLE || phase == PHASE_GAMEOVER) {
      start_game();
    }
  }

  // --- Encoder 2: Fire (button) ---
  enc_read(1);  // discard rotation
  if (enc_button(1)) {
    if (phase == PHASE_PLAY) {
      fire_bullet();
    } else if (phase == PHASE_TITLE || phase == PHASE_GAMEOVER) {
      start_game();
    }
  }

  draw_frame();
  swap_and_send();
  fps_tick();
}
