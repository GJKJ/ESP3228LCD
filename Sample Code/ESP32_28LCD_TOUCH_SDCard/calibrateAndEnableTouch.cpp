/* =====================================================================
 *  触摸位置校准 —— 模块版（供 ESP32_28LCD_TOUCH_SDCard.ino 调用）
 *
 *  与原独立程序的差异：
 *    1. 删除 setup() / loop()          —— 由 .ino 提供
 *    2. 删除 lcdScreen / touchScreen   —— 由 .ino 定义，这里 extern 引用
 *    3. 删除所有引脚和分辨率 #define   —— 统一取自 config.h
 *    4. 不再调用 init()/setRotation()/invertDisplay()
 *       屏幕由 .ino 初始化，本模块只负责绘制
 *    5. runCalibration() 由递归改为循环，避免反复失败爆栈
 *    6. 加入 calValid 标志，未校准时 getTouch() 不会输出垃圾坐标
 *    7. 等待触摸默认不超时（TAP_TIMEOUT_MS = 0），一直等操作完成
 * ===================================================================== */

#include <Arduino.h>
#include <SPI.h>
#include <Preferences.h>
#include <math.h>
#include <string.h>

#include "config.h"
#include "calibrateAndEnableTouch.h"

// ===================== 本模块内部参数 =====================
#define Z_MIN            400      // 压力下限
#define SAMPLES           16      // 每个靶点采样数
#define NPT                5      // 靶点数量
#define CAL_RMS_MAX     6.0f      // 残差上限，超过则重来
#define TAP_TIMEOUT_MS     0      // 单步等待触摸的超时（毫秒）。0 = 不超时，一直等操作
                                  // 注意：设为 0 时，触摸芯片误判/损坏会让校准永久阻塞
#define MAX_ATTEMPT        3      // 整体重试次数

// ===================== 标定系数 =====================
struct Cal {
  float    a, b, c, d, e, f;
  uint8_t  rotation;             // 保存时的屏幕方向，方向变了系数就失效
  uint32_t magic;
};
static const uint32_t CAL_MAGIC = 0x43414C33;   // 'CAL3'

static Cal         cal;
static bool        calValid = false;
static Preferences prefs;

static double rawX[NPT], rawY[NPT];

// 绘图坐标系尺寸：跟随当前 rotation，rotation 2 下即 config.h 的 240x320
static inline int16_t scrW() { return (int16_t)lcdScreen.width();  }
static inline int16_t scrH() { return (int16_t)lcdScreen.height(); }

// =====================================================================
//  绘图小工具
// =====================================================================
static void centerText(const char *s, int16_t cy, uint16_t color, uint8_t size) {
  int16_t x1, y1; uint16_t w, h;
  lcdScreen.setTextSize(size);
  lcdScreen.setTextColor(color);
  lcdScreen.getTextBounds(s, 0, 0, &x1, &y1, &w, &h);
  lcdScreen.setCursor((scrW() - (int16_t)w) / 2, cy);
  lcdScreen.print(s);
}

static void drawTarget(int16_t x, int16_t y, uint16_t col) {
  lcdScreen.drawFastHLine(x - 12, y, 25, col);
  lcdScreen.drawFastVLine(x, y - 12, 25, col);
  lcdScreen.drawCircle(x, y, 8, col);
  lcdScreen.drawCircle(x, y, 3, col);
}

// =====================================================================
//  采样：固定采 16 个，排序后取中间一半求平均
// =====================================================================
static void isort(uint16_t *v, int n) {
  for (int i = 1; i < n; i++) {
    uint16_t k = v[i]; int j = i - 1;
    while (j >= 0 && v[j] > k) { v[j + 1] = v[j]; j--; }
    v[j + 1] = k;
  }
}

static bool sampleRaw(double &rx, double &ry) {
  uint16_t bx[SAMPLES], by[SAMPLES];
  int n = 0;
  uint32_t t0 = millis();
  while (n < SAMPLES) {
    if (millis() - t0 > 700) return false;
    if (!touchScreen.touched()) return false;
    TS_Point p = touchScreen.getPoint();
    if (p.z < Z_MIN) { delay(2); continue; }
    bx[n] = p.x; by[n] = p.y; n++;
    delay(3);
  }
  isort(bx, n); isort(by, n);
  int lo = n / 4, hi = n - n / 4;
  double sx = 0, sy = 0;
  for (int i = lo; i < hi; i++) { sx += bx[i]; sy += by[i]; }
  rx = sx / (hi - lo);
  ry = sy / (hi - lo);
  return true;
}

static void waitRelease() {
  uint32_t t0 = millis();
  while (touchScreen.touched()) {
    if (TAP_TIMEOUT_MS && (millis() - t0 > TAP_TIMEOUT_MS)) break;
    delay(10);
  }
  delay(150);
}

// 等待一次按下。TAP_TIMEOUT_MS 为 0 时永不返回 false，一直等
static bool waitForTap() {
  uint32_t t0 = millis();
  while (!touchScreen.touched()) {
    if (TAP_TIMEOUT_MS && (millis() - t0 > TAP_TIMEOUT_MS)) return false;
    delay(20);
  }
  return true;
}

// =====================================================================
//  3x3 方程组（Cramer 法则）
// =====================================================================
static double det3(double m[3][3]) {
  return m[0][0] * (m[1][1]*m[2][2] - m[1][2]*m[2][1])
       - m[0][1] * (m[1][0]*m[2][2] - m[1][2]*m[2][0])
       + m[0][2] * (m[1][0]*m[2][1] - m[1][1]*m[2][0]);
}

static bool solve3(double M[3][3], double v[3], double out[3]) {
  double D = det3(M);
  if (fabs(D) < 1e-9) return false;
  for (int c = 0; c < 3; c++) {
    double T[3][3];
    memcpy(T, M, sizeof(T));
    for (int r = 0; r < 3; r++) T[r][c] = v[r];
    out[c] = det3(T) / D;
  }
  return true;
}

// 最小二乘拟合
static bool fitAffine(const int16_t *tgtX, const int16_t *tgtY) {
  double S[3][3] = {{0}}, vx[3] = {0}, vy[3] = {0};
  for (int i = 0; i < NPT; i++) {
    double X = rawX[i], Y = rawY[i];
    S[0][0] += X*X; S[0][1] += X*Y; S[0][2] += X;
    S[1][0] += X*Y; S[1][1] += Y*Y; S[1][2] += Y;
    S[2][0] += X;   S[2][1] += Y;   S[2][2] += 1;
    vx[0] += X * tgtX[i]; vx[1] += Y * tgtX[i]; vx[2] += tgtX[i];
    vy[0] += X * tgtY[i]; vy[1] += Y * tgtY[i]; vy[2] += tgtY[i];
  }
  double cx[3], cy[3];
  if (!solve3(S, vx, cx) || !solve3(S, vy, cy)) return false;
  cal.a = cx[0]; cal.b = cx[1]; cal.c = cx[2];
  cal.d = cy[0]; cal.e = cy[1]; cal.f = cy[2];
  cal.rotation = lcdScreen.getRotation();
  cal.magic    = CAL_MAGIC;
  return true;
}

// =====================================================================
//  逐点残差报告
// =====================================================================
static float report(const int16_t *tgtX, const int16_t *tgtY) {
  double s = 0, mx = 0;
  Serial.println();
  Serial.println("pt   target        fit             dx      dy");
  for (int i = 0; i < NPT; i++) {
    double px = cal.a * rawX[i] + cal.b * rawY[i] + cal.c;
    double py = cal.d * rawX[i] + cal.e * rawY[i] + cal.f;
    double dx = px - tgtX[i], dy = py - tgtY[i];
    double e  = sqrt(dx*dx + dy*dy);
    if (e > mx) mx = e;
    s += dx*dx + dy*dy;
    Serial.printf("%2d   (%3d,%3d)    (%6.1f,%6.1f)   %+6.2f  %+6.2f\n",
                  i + 1, tgtX[i], tgtY[i], px, py, dx, dy);
  }
  float rms = sqrt(s / NPT);
  Serial.printf("RMS = %.2f px    MAX = %.2f px\n", rms, mx);
  return rms;
}

// =====================================================================
//  NVS
// =====================================================================
static void saveCal() {
  prefs.begin("touchcal", false);
  prefs.putBytes("cal3", &cal, sizeof(cal));
  prefs.end();
}

bool loadCalibration() {
  prefs.begin("touchcal", true);
  bool ok = (prefs.getBytesLength("cal3") == sizeof(cal))
         && (prefs.getBytes("cal3", &cal, sizeof(cal)) == sizeof(cal))
         && (cal.magic == CAL_MAGIC);
  prefs.end();

  // 屏幕方向变了，旧系数不能用
  if (ok && cal.rotation != lcdScreen.getRotation()) {
    Serial.printf("NVS 系数是 rotation %d 的，当前是 rotation %d，需重新校准\n",
                  cal.rotation, lcdScreen.getRotation());
    ok = false;
  }

  calValid = ok;
  if (ok) Serial.println("已从 NVS 载入触摸校准系数");
  return ok;
}

void clearCalibration() {
  prefs.begin("touchcal", false);
  prefs.remove("cal3");
  prefs.end();
  calValid = false;
  Serial.println("已清除 NVS 中的触摸校准系数");
}

bool isCalibrated() { return calValid; }

// =====================================================================
//  运行时接口
// =====================================================================
bool getTouch(int16_t &x, int16_t &y) {
  if (!calValid) return false;                 // 未校准，不输出垃圾坐标
  if (!touchScreen.touched()) return false;
  TS_Point p = touchScreen.getPoint();
  if (p.z < Z_MIN) return false;
  float px = cal.a * p.x + cal.b * p.y + cal.c;
  float py = cal.d * p.x + cal.e * p.y + cal.f;
  x = constrain((int16_t)lroundf(px), 0, scrW() - 1);
  y = constrain((int16_t)lroundf(py), 0, scrH() - 1);
  return true;
}

// =====================================================================
//  打印可粘贴的代码片段
// =====================================================================
static void printSnippet(float rms) {
  Serial.println();
  Serial.println("================ 复制下面这段到你的主程序 ================");
  Serial.printf("const float CAL_A = %.8ff;\n", cal.a);
  Serial.printf("const float CAL_B = %.8ff;\n", cal.b);
  Serial.printf("const float CAL_C = %.4ff;\n",  cal.c);
  Serial.printf("const float CAL_D = %.8ff;\n", cal.d);
  Serial.printf("const float CAL_E = %.8ff;\n", cal.e);
  Serial.printf("const float CAL_F = %.4ff;\n",  cal.f);
  Serial.println();
  Serial.println("bool getTouch(int16_t &x, int16_t &y) {");
  Serial.println("  if (!touchScreen.touched()) return false;");
  Serial.println("  TS_Point p = touchScreen.getPoint();");
  Serial.printf ("  if (p.z < %d) return false;\n", Z_MIN);
  Serial.println("  float px = CAL_A * p.x + CAL_B * p.y + CAL_C;");
  Serial.println("  float py = CAL_D * p.x + CAL_E * p.y + CAL_F;");
  Serial.printf ("  x = constrain((int16_t)lroundf(px), 0, %d);\n", scrW() - 1);
  Serial.printf ("  y = constrain((int16_t)lroundf(py), 0, %d);\n", scrH() - 1);
  Serial.println("  return true;");
  Serial.println("}");
  Serial.printf("// RMS = %.2f px   (rotation %d, %dx%d)\n",
                rms, lcdScreen.getRotation(), scrW(), scrH());
  Serial.println("=========================================================");
}

// =====================================================================
//  校准主流程（循环重试，不再递归）
// =====================================================================
void runCalibration() {
  const int16_t W = scrW(), H = scrH();

  // 5 个靶点：四角 15%/85% + 正中
  const int16_t tgtX[NPT] = { (int16_t)(W*15/100), (int16_t)(W*85/100),
                              (int16_t)(W*85/100), (int16_t)(W*15/100),
                              (int16_t)(W/2) };
  const int16_t tgtY[NPT] = { (int16_t)(H*15/100), (int16_t)(H*15/100),
                              (int16_t)(H*85/100), (int16_t)(H*85/100),
                              (int16_t)(H/2) };

  calValid = false;
  Serial.printf("===== XPT2046 触摸校准  绘图坐标系 %d x %d (rotation %d) =====\n",
                W, H, lcdScreen.getRotation());

  for (int attempt = 1; attempt <= MAX_ATTEMPT; attempt++) {

    // ---- 开始画面 ----
    char head[24];
    snprintf(head, sizeof(head), "rotation %d  %dx%d", lcdScreen.getRotation(), W, H);
    lcdScreen.fillScreen(ST77XX_BLACK);
    centerText("Hi,ideaspark",               H/2 - 95, ST77XX_GREEN,  3);
    centerText("TOUCH CAL",                  H/2 - 45, ST77XX_WHITE,  2);
    centerText(head,                         H/2 - 20, ST77XX_WHITE,  1);
    centerText("use a stylus or fingernail", H/2 + 5,  ST77XX_YELLOW, 1);
    centerText("tap to start!",              H/2 + 55, ST77XX_CYAN,   2);

    waitRelease();
    if (!waitForTap()) {
      Serial.println("等待触摸超时，放弃本次校准");
      lcdScreen.fillScreen(ST77XX_BLACK);
      centerText("CAL TIMEOUT", H/2 - 10, ST77XX_RED, 2);
      centerText("send 'c' to retry", H/2 + 15, ST77XX_WHITE, 1);
      return;
    }
    waitRelease();

    // ---- 逐点采样 ----
    bool timedOut = false;
    for (int i = 0; i < NPT && !timedOut; i++) {
      lcdScreen.fillScreen(ST77XX_BLACK);
      drawTarget(tgtX[i], tgtY[i], ST77XX_YELLOW);
      char buf[16];
      snprintf(buf, sizeof(buf), "%d / %d", i + 1, NPT);
      centerText(buf, H - 20, ST77XX_WHITE, 1);

      uint32_t t0 = millis();
      while (true) {
        if (touchScreen.touched() && sampleRaw(rawX[i], rawY[i])) break;
        if (TAP_TIMEOUT_MS && (millis() - t0 > TAP_TIMEOUT_MS)) { timedOut = true; break; }
        delay(10);
      }
      if (timedOut) break;

      drawTarget(tgtX[i], tgtY[i], ST77XX_GREEN);
      Serial.printf("P%d  target(%3d,%3d)  raw(%7.1f,%7.1f)\n",
                    i + 1, tgtX[i], tgtY[i], rawX[i], rawY[i]);
      waitRelease();
    }
    if (timedOut) {
      Serial.println("采样超时，放弃本次校准");
      lcdScreen.fillScreen(ST77XX_BLACK);
      centerText("CAL TIMEOUT", H/2 - 10, ST77XX_RED, 2);
      centerText("send 'c' to retry", H/2 + 15, ST77XX_WHITE, 1);
      return;
    }

    // ---- 拟合 ----
    if (!fitAffine(tgtX, tgtY)) {
      lcdScreen.fillScreen(ST77XX_RED);
      centerText("FIT FAILED", H/2, ST77XX_WHITE, 2);
      Serial.println("拟合失败：靶点数据退化，重来");
      delay(2000);
      continue;
    }

    float rms = report(tgtX, tgtY);
    if (rms > CAL_RMS_MAX) {
      char buf[32];
      snprintf(buf, sizeof(buf), "RMS %.1f px", rms);
      lcdScreen.fillScreen(ST77XX_RED);
      centerText("RETRY", H/2 - 15, ST77XX_WHITE, 2);
      centerText(buf,     H/2 + 10, ST77XX_WHITE, 2);
      Serial.println("残差过大，重来");
      delay(2500);
      continue;
    }

    // ---- 成功 ----
    calValid = true;
    saveCal();
    printSnippet(rms);

    char buf[32];
    snprintf(buf, sizeof(buf), "RMS %.2f px", rms);
    lcdScreen.fillScreen(ST77XX_BLACK);
    centerText("SAVED", H/2 - 30, ST77XX_GREEN, 2);
    centerText(buf,     H/2 - 5,  ST77XX_WHITE, 2);
    centerText("see serial monitor", H/2 + 25, ST77XX_CYAN, 1);
    delay(1000);
    return;
  }

  // 三次都没成功
  Serial.println("校准连续失败，保持未校准状态");
  lcdScreen.fillScreen(ST77XX_BLACK);
  centerText("CAL FAILED", H/2 - 10, ST77XX_RED, 2);
  centerText("send 'c' to retry", H/2 + 15, ST77XX_WHITE, 1);
}

// =====================================================================
//  验证画面
// =====================================================================
void drawVerifyScreen() {
  const int16_t W = scrW(), H = scrH();
  lcdScreen.fillScreen(ST77XX_BLACK);
  lcdScreen.drawRect(0, 0, W, H, ST77XX_WHITE);
  lcdScreen.drawRect(1, 1, W - 2, H - 2, ST77XX_WHITE);
  const int s = 24;
  lcdScreen.drawRect(0,     0,     s, s, ST77XX_RED);
  lcdScreen.drawRect(W - s, 0,     s, s, ST77XX_RED);
  lcdScreen.drawRect(0,     H - s, s, s, ST77XX_RED);
  lcdScreen.drawRect(W - s, H - s, s, s, ST77XX_RED);
  lcdScreen.drawFastHLine(0,     H/2, W, 0x2104);
  lcdScreen.drawFastVLine(W/2,   0,   H, 0x2104);
  centerText("trace the border",   6,  ST77XX_CYAN, 1);
  centerText("serial 'c' = recal", 18, ST77XX_CYAN, 1);
}
