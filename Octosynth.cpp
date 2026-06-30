#include <Arduino.h>
#include <SPI.h>
#include <Adafruit_GFX.h>
#include <Adafruit_ILI9341.h>
#include <math.h>
#include <driver/i2s.h>

// =====================================================
// PINLER
// =====================================================

#define TFT_CS   5
#define TFT_DC   21
#define TFT_RST  22

#define SPI_MOSI 23
#define SPI_MISO 19
#define SPI_SCK  18

// ESP32 internal DAC output
// D25 = GPIO25 = DAC1
#define AUDIO_PIN 25

// Potansiyometreler
#define POT_WOBBLE 34
#define POT_PITCH  35
#define POT_ATTACK 36   // VP
#define POT_CUTOFF 39   // VN

// Butonlar
#define BTN_MENU 16
#define BTN_OK   17

// Bakır touch notalar
#define TOUCH_C4  4
#define TOUCH_D4  12
#define TOUCH_E4  13
#define TOUCH_F4  14
#define TOUCH_G4  15
#define TOUCH_A4  27
#define TOUCH_B4  32
#define TOUCH_C5  33

// =====================================================
// TFT
// =====================================================

Adafruit_ILI9341 tft = Adafruit_ILI9341(TFT_CS, TFT_DC, TFT_RST);

// =====================================================
// SES AYARLARI
// =====================================================

#define SAMPLE_RATE 16000
#define MAX_VOICES 8
#define WAVETABLE_SIZE 256
#define AUDIO_BUFFER_SAMPLES 128

enum Waveform {
  WAVE_SINE = 0,
  WAVE_SQUARE,
  WAVE_TRIANGLE,
  WAVE_SAW
};

volatile Waveform currentWave = WAVE_SINE;

const char* waveNames[] = {
  "SINE",
  "SQUARE",
  "TRIANGLE",
  "SAW"
};

const char* noteNames[8] = {
  "C4", "D4", "E4", "F4",
  "G4", "A4", "B4", "C5"
};

float baseFreqs[8] = {
  261.63, 293.66, 329.63, 349.23,
  392.00, 440.00, 493.88, 523.25
};

uint8_t sineTable[WAVETABLE_SIZE];

struct Voice {
  bool active;
  bool gate;
  uint8_t note;
  uint32_t phase;
  uint32_t phaseIncBase;
  int env;
};

Voice voices[MAX_VOICES];

portMUX_TYPE audioMux = portMUX_INITIALIZER_UNLOCKED;

// Pot parametreleri
volatile int wobbleDepth = 0;
volatile int pitchShift = 0;
volatile int attackStep = 12;
volatile int cutoffAmount = 650;

// Pitch pot hata koruması
bool pitchPotError = false;
int lastRawPitch = 0;

// Ses değişkenleri
int filterY = 0;
uint32_t lfoPhase = 0;

// =====================================================
// TOUCH
// =====================================================

int touchPins[8] = {
  TOUCH_C4, TOUCH_D4, TOUCH_E4, TOUCH_F4,
  TOUCH_G4, TOUCH_A4, TOUCH_B4, TOUCH_C5
};

int touchBaseline[8];
int touchThreshold[8];
bool touchState[8];
bool lastTouchState[8];

// =====================================================
// UI / MENÜ
// =====================================================

enum AppMode {
  MODE_MENU = 0,
  MODE_STANDARD,
  MODE_TUTORIAL,
  MODE_RECORD,
  MODE_PLAYBACK
};

AppMode mode = MODE_MENU;

const char* menuItems[] = {
  "Standart Mod",
  "Tutorial Mod",
  "Kayit Modu",
  "Kayit Dinleme"
};

int menuIndex = 0;

bool screenDirty = true;
bool noteBarDirty = true;
bool paramDirty = true;

bool standardStaticDrawn = false;

int shownWobble = -999;
int shownPitch  = -999;
int shownAttack = -999;
int shownCutoff = -999;
int shownPitchRaw = -999;

Waveform shownWave = (Waveform)99;

bool lastDrawnTouchState[8] = {
  false, false, false, false,
  false, false, false, false
};

bool lastMenuBtn = HIGH;
bool lastOkBtn = HIGH;
unsigned long lastButtonTime = 0;

// =====================================================
// KAYIT
// =====================================================

#define MAX_EVENTS 300

struct NoteEvent {
  uint32_t timeMs;
  uint8_t note;
  uint8_t on;
};

NoteEvent events[MAX_EVENTS];
int eventCount = 0;

bool isRecording = false;
bool isPlayingRecord = false;

uint32_t recordStartMs = 0;
uint32_t playbackStartMs = 0;
int playbackIndex = 0;

// =====================================================
// TUTORIAL
// =====================================================

bool tutorialActive = false;
int targetNote = -1;
int score = 0;
String tutorialMessage = "OK ile basla";

// =====================================================
// SES YARDIMCI
// =====================================================

void initSineTable() {
  for (int i = 0; i < WAVETABLE_SIZE; i++) {
    float angle = 2.0 * PI * i / WAVETABLE_SIZE;
    sineTable[i] = 128 + sin(angle) * 127;
  }
}

uint32_t freqToPhaseInc(float freq) {
  double inc = (freq * 4294967296.0) / SAMPLE_RATE;
  return (uint32_t)inc;
}

float semitoneToRatio(int semitone) {
  return pow(2.0, semitone / 12.0);
}

int getWaveSample(Waveform w, uint8_t idx) {
  switch (w) {
    case WAVE_SINE:
      return (int)sineTable[idx] - 128;

    case WAVE_SQUARE:
      return idx < 128 ? 120 : -120;

    case WAVE_TRIANGLE:
      if (idx < 128) return map(idx, 0, 127, -120, 120);
      else return map(idx, 128, 255, 120, -120);

    case WAVE_SAW:
      return map(idx, 0, 255, -120, 120);
  }

  return 0;
}

void clearVoices() {
  portENTER_CRITICAL(&audioMux);

  for (int i = 0; i < MAX_VOICES; i++) {
    voices[i].active = false;
    voices[i].gate = false;
    voices[i].note = 0;
    voices[i].phase = 0;
    voices[i].phaseIncBase = 0;
    voices[i].env = 0;
  }

  portEXIT_CRITICAL(&audioMux);
}

int findVoiceForNoteUnsafe(uint8_t note) {
  for (int i = 0; i < MAX_VOICES; i++) {
    if (voices[i].active && voices[i].note == note) return i;
  }
  return -1;
}

int findFreeVoiceUnsafe() {
  for (int i = 0; i < MAX_VOICES; i++) {
    if (!voices[i].active) return i;
  }
  return 0;
}

void addRecordEvent(uint8_t note, bool on) {
  if (!isRecording) return;
  if (eventCount >= MAX_EVENTS) return;

  events[eventCount].timeMs = millis() - recordStartMs;
  events[eventCount].note = note;
  events[eventCount].on = on ? 1 : 0;
  eventCount++;
}

void noteOn(uint8_t note, bool fromPlayback = false) {
  portENTER_CRITICAL(&audioMux);

  int existing = findVoiceForNoteUnsafe(note);

  if (existing >= 0) {
    voices[existing].gate = true;
    portEXIT_CRITICAL(&audioMux);
    return;
  }

  int v = findFreeVoiceUnsafe();

  int safePitch = pitchShift;
  if (pitchPotError) safePitch = 0;

  float shiftedFreq = baseFreqs[note] * semitoneToRatio(safePitch);

  voices[v].active = true;
  voices[v].gate = true;
  voices[v].note = note;
  voices[v].phase = 0;
  voices[v].phaseIncBase = freqToPhaseInc(shiftedFreq);
  voices[v].env = 0;

  portEXIT_CRITICAL(&audioMux);

  if (!fromPlayback && isRecording) {
    addRecordEvent(note, true);
  }
}

void noteOff(uint8_t note, bool fromPlayback = false) {
  portENTER_CRITICAL(&audioMux);

  int v = findVoiceForNoteUnsafe(note);

  if (v >= 0) voices[v].gate = false;

  portEXIT_CRITICAL(&audioMux);

  if (!fromPlayback && isRecording) {
    addRecordEvent(note, false);
  }
}

// =====================================================
// I2S INTERNAL DAC SES
// =====================================================

void setupI2SInternalDAC() {
  i2s_config_t i2s_config = {
    .mode = (i2s_mode_t)(I2S_MODE_MASTER | I2S_MODE_TX | I2S_MODE_DAC_BUILT_IN),
    .sample_rate = SAMPLE_RATE,
    .bits_per_sample = I2S_BITS_PER_SAMPLE_16BIT,
    .channel_format = I2S_CHANNEL_FMT_ONLY_RIGHT,
    .communication_format = I2S_COMM_FORMAT_STAND_MSB,
    .intr_alloc_flags = 0,
    .dma_buf_count = 8,
    .dma_buf_len = AUDIO_BUFFER_SAMPLES,
    .use_apll = false,
    .tx_desc_auto_clear = true,
    .fixed_mclk = 0
  };

  i2s_driver_install(I2S_NUM_0, &i2s_config, 0, NULL);

  // GPIO25 = DAC1 = RIGHT channel
  i2s_set_dac_mode(I2S_DAC_CHANNEL_RIGHT_EN);

  i2s_zero_dma_buffer(I2S_NUM_0);
}

uint8_t generateAudioByte() {
  int mix = 0;
  int activeCount = 0;

  lfoPhase += 12000;
  uint8_t lfoIdx = lfoPhase >> 24;
  int lfo = getWaveSample(WAVE_SINE, lfoIdx);

  for (int i = 0; i < MAX_VOICES; i++) {
    if (voices[i].active) {
      if (voices[i].gate) {
        voices[i].env += attackStep;
        if (voices[i].env > 1024) voices[i].env = 1024;
      } else {
        voices[i].env -= 10;
        if (voices[i].env <= 0) {
          voices[i].env = 0;
          voices[i].active = false;
          continue;
        }
      }

      int wobbleAmount = (lfo * wobbleDepth) / 128;
      uint32_t inc = voices[i].phaseIncBase + ((voices[i].phaseIncBase / 1000) * wobbleAmount);

      voices[i].phase += inc;
      uint8_t idx = voices[i].phase >> 24;

      int s = getWaveSample(currentWave, idx);
      s = (s * voices[i].env) / 1024;

      mix += s;
      activeCount++;
    }
  }

  if (activeCount > 1) mix = mix / activeCount;

  filterY += ((mix - filterY) * cutoffAmount) / 1024;
  int filtered = filterY;

  int out = 128 + filtered;
  out = constrain(out, 0, 255);

  return (uint8_t)out;
}

void audioTask(void* parameter) {
  uint16_t audioBuffer[AUDIO_BUFFER_SAMPLES];

  while (true) {
    portENTER_CRITICAL(&audioMux);

    for (int i = 0; i < AUDIO_BUFFER_SAMPLES; i++) {
      uint8_t out8 = generateAudioByte();
      audioBuffer[i] = ((uint16_t)out8) << 8;
    }

    portEXIT_CRITICAL(&audioMux);

    size_t bytesWritten = 0;
    i2s_write(I2S_NUM_0, audioBuffer, sizeof(audioBuffer), &bytesWritten, portMAX_DELAY);
  }
}

// =====================================================
// TOUCH KALİBRASYON
// =====================================================

void calibrateTouch() {
  tft.fillScreen(ILI9341_BLACK);
  tft.setTextColor(ILI9341_WHITE);
  tft.setTextSize(2);
  tft.setCursor(20, 70);
  tft.print("Kalibrasyon...");
  tft.setCursor(20, 105);
  tft.print("Bakirlara dokunma");

  delay(1500);

  for (int i = 0; i < 8; i++) {
    long total = 0;

    for (int j = 0; j < 50; j++) {
      total += touchRead(touchPins[i]);
      delay(15);
    }

    touchBaseline[i] = total / 50;
    touchThreshold[i] = touchBaseline[i] * 82 / 100;

    touchState[i] = false;
    lastTouchState[i] = false;
  }

  screenDirty = true;
  noteBarDirty = true;
  standardStaticDrawn = false;
}

// =====================================================
// POT OKUMA
// =====================================================

void updatePots() {
  static int smoothW = 0;
  static int smoothP = 0;
  static int smoothA = 0;
  static int smoothC = 0;
  static bool firstRun = true;

  int rawW = analogRead(POT_WOBBLE);
  int rawP = analogRead(POT_PITCH);
  int rawA = analogRead(POT_ATTACK);
  int rawC = analogRead(POT_CUTOFF);

  lastRawPitch = rawP;

  if (firstRun) {
    smoothW = rawW;
    smoothP = rawP;
    smoothA = rawA;
    smoothC = rawC;
    firstRun = false;
  }

  smoothW = (smoothW * 7 + rawW) / 8;
  smoothP = (smoothP * 7 + rawP) / 8;
  smoothA = (smoothA * 7 + rawA) / 8;
  smoothC = (smoothC * 7 + rawC) / 8;

  int newWobble = map(smoothW, 0, 4095, 0, 80);

  // Pitch failsafe:
  // D35 sürekli 0 civarı okuyorsa pot bağlantısı hatalıdır.
  // Bu durumda sesi bozmasın diye pitch 0 yapılır.
  int newPitch = 0;
  if (smoothP < 80) {
    pitchPotError = true;
    newPitch = 0;
  } else {
    pitchPotError = false;
    newPitch = map(smoothP, 80, 4095, -7, 7);
  }

  int newAttack = map(smoothA, 0, 4095, 40, 2);
  int newCutoff = map(smoothC, 0, 4095, 150, 950);

  bool changed = false;

  if (abs(newWobble - wobbleDepth) >= 3) {
    wobbleDepth = newWobble;
    changed = true;
  }

  if (newPitch != pitchShift || pitchPotError) {
    pitchShift = newPitch;
    changed = true;
  }

  if (abs(newAttack - attackStep) >= 3) {
    attackStep = newAttack;
    changed = true;
  }

  if (abs(newCutoff - cutoffAmount) >= 25) {
    cutoffAmount = newCutoff;
    changed = true;
  }

  if (changed && mode == MODE_STANDARD) {
    paramDirty = true;
  }
}

// =====================================================
// KAYIT / PLAYBACK
// =====================================================

void startRecording() {
  clearVoices();
  eventCount = 0;
  isRecording = true;
  isPlayingRecord = false;
  recordStartMs = millis();
  screenDirty = true;
}

void stopRecording() {
  isRecording = false;
  clearVoices();
  screenDirty = true;
}

void startPlayback() {
  if (eventCount == 0) return;

  clearVoices();
  isPlayingRecord = true;
  isRecording = false;
  playbackStartMs = millis();
  playbackIndex = 0;
  screenDirty = true;
}

void stopPlayback() {
  isPlayingRecord = false;
  clearVoices();
  screenDirty = true;
}

void updatePlayback() {
  if (!isPlayingRecord) return;

  if (playbackIndex >= eventCount) {
    stopPlayback();
    return;
  }

  uint32_t now = millis() - playbackStartMs;

  while (playbackIndex < eventCount && events[playbackIndex].timeMs <= now) {
    uint8_t n = events[playbackIndex].note;

    if (events[playbackIndex].on) noteOn(n, true);
    else noteOff(n, true);

    playbackIndex++;
  }

  if (playbackIndex >= eventCount) stopPlayback();
}

// =====================================================
// TUTORIAL
// =====================================================

void newTutorialTarget() {
  targetNote = random(0, 8);
  tutorialActive = true;
  tutorialMessage = "Bu notayi bul:";
  screenDirty = true;
}

void handleTutorialInput(int note) {
  if (!tutorialActive || targetNote < 0) return;

  if (note == targetNote) {
    score++;
    tutorialMessage = "DOGRU! Skor +1";
    targetNote = -1;
    tutorialActive = false;
  } else {
    tutorialMessage = "YANLIS! Tekrar dene";
  }

  screenDirty = true;
}

// =====================================================
// TOUCH OKUMA
// =====================================================

void updateTouch() {
  // ANA MENÜDE BAKIRLAR TAMAMEN PASİF
  // Basılsa bile ses çıkmaz, noteOn çalışmaz.
  if (mode == MODE_MENU) {
    for (int i = 0; i < 8; i++) {
      touchState[i] = false;
      lastTouchState[i] = false;
    }
    return;
  }

  for (int i = 0; i < 8; i++) {
    int val = touchRead(touchPins[i]);

    bool pressed = false;

    if (touchBaseline[i] > 0 && val < touchThreshold[i]) {
      pressed = true;
    }

    touchState[i] = pressed;

    if (touchState[i] && !lastTouchState[i]) {
      if (mode == MODE_STANDARD || mode == MODE_RECORD || mode == MODE_TUTORIAL) {
        noteOn(i);
      }

      if (mode == MODE_TUTORIAL) {
        handleTutorialInput(i);
      }

      noteBarDirty = true;
    }

    if (!touchState[i] && lastTouchState[i]) {
      if (mode == MODE_STANDARD || mode == MODE_RECORD || mode == MODE_TUTORIAL) {
        noteOff(i);
      }

      noteBarDirty = true;
    }

    lastTouchState[i] = touchState[i];
  }
}

// =====================================================
// UI ÇİZİM
// =====================================================

void drawHeader(const char* title) {
  tft.fillRect(0, 0, 320, 34, ILI9341_NAVY);
  tft.setTextColor(ILI9341_WHITE);
  tft.setTextSize(2);
  tft.setCursor(10, 8);
  tft.print("OCTOSYNTH");

  tft.setTextSize(1);
  tft.setCursor(215, 12);
  tft.print(title);
}

void drawNoteBar() {
  int y = 202;
  int w = 39;

  for (int i = 0; i < 8; i++) {
    uint16_t color = touchState[i] ? ILI9341_ORANGE : ILI9341_DARKGREY;

    if (mode == MODE_TUTORIAL && targetNote == i) {
      color = ILI9341_BLUE;
    }

    tft.fillRoundRect(4 + i * w, y, w - 4, 30, 4, color);
    tft.setTextColor(ILI9341_WHITE);
    tft.setTextSize(1);
    tft.setCursor(13 + i * w, y + 11);
    tft.print(noteNames[i]);
  }

  noteBarDirty = false;
}

void drawValueLine(int x, int y, const char* label, int value, int& shownValue) {
  if (shownValue == value) return;

  tft.fillRect(x + 105, y, 70, 20, ILI9341_BLACK);

  tft.setTextColor(ILI9341_WHITE);
  tft.setTextSize(2);

  tft.setCursor(x, y);
  tft.print(label);

  tft.setCursor(x + 105, y);
  tft.print(value);

  shownValue = value;
}

void drawRawPitchLine() {
  if (shownPitchRaw == lastRawPitch && !pitchPotError) return;

  tft.fillRect(12, 162, 150, 14, ILI9341_BLACK);

  tft.setTextSize(1);
  tft.setCursor(12, 162);

  if (pitchPotError) {
    tft.setTextColor(ILI9341_RED);
    tft.print("Pitch pot ERR raw=");
    tft.print(lastRawPitch);
  } else {
    tft.setTextColor(ILI9341_DARKGREY);
    tft.print("Pitch raw=");
    tft.print(lastRawPitch);
  }

  shownPitchRaw = lastRawPitch;
}

void drawWaveformBox() {
  int x = 168;
  int y = 55;
  int w = 140;
  int h = 80;

  tft.drawRoundRect(x, y, w, h, 6, ILI9341_WHITE);
  tft.setTextColor(ILI9341_WHITE);
  tft.setTextSize(1);
  tft.setCursor(x + 10, y + 8);
  tft.print(waveNames[currentWave]);

  int mid = y + h / 2;
  int prevX = x + 8;
  int prevY = mid;

  for (int i = 0; i < 100; i++) {
    uint8_t idx = map(i, 0, 99, 0, 255);
    int s = getWaveSample(currentWave, idx);
    int py = mid - map(s, -128, 127, -28, 28);
    int px = x + 20 + i;

    if (i > 0) tft.drawLine(prevX, prevY, px, py, ILI9341_CYAN);

    prevX = px;
    prevY = py;
  }
}

void drawStandardStatic() {
  tft.fillScreen(ILI9341_BLACK);
  drawHeader("STANDART");

  tft.setTextColor(ILI9341_WHITE);
  tft.setTextSize(2);

  tft.setCursor(12, 50);
  tft.print("Wobble:");

  tft.setCursor(12, 80);
  tft.print("Pitch:");

  tft.setCursor(12, 110);
  tft.print("Attack:");

  tft.setCursor(12, 140);
  tft.print("Cutoff:");

  tft.setTextSize(1);
  tft.setCursor(12, 180);
  tft.print("OK: waveform degistir | MENU: geri");

  shownWobble = -999;
  shownPitch = -999;
  shownAttack = -999;
  shownCutoff = -999;
  shownPitchRaw = -999;
  shownWave = (Waveform)99;

  for (int i = 0; i < 8; i++) {
    lastDrawnTouchState[i] = !touchState[i];
  }

  standardStaticDrawn = true;
  paramDirty = true;
  noteBarDirty = true;
}

void updateStandardValues() {
  drawValueLine(12, 50, "Wobble:", wobbleDepth, shownWobble);
  drawValueLine(12, 80, "Pitch:", pitchShift, shownPitch);
  drawValueLine(12, 110, "Attack:", attackStep, shownAttack);
  drawValueLine(12, 140, "Cutoff:", cutoffAmount, shownCutoff);
  drawRawPitchLine();
}

void updateWaveformBoxOnly() {
  if (shownWave == currentWave) return;

  int x = 168;
  int y = 55;
  int w = 140;
  int h = 80;

  tft.fillRect(x - 2, y - 2, w + 4, h + 4, ILI9341_BLACK);
  drawWaveformBox();

  shownWave = currentWave;
}

void updateNoteBarOnlyChanged() {
  int y = 202;
  int w = 39;

  for (int i = 0; i < 8; i++) {
    if (lastDrawnTouchState[i] == touchState[i]) continue;

    uint16_t color = touchState[i] ? ILI9341_ORANGE : ILI9341_DARKGREY;

    if (mode == MODE_TUTORIAL && targetNote == i) {
      color = ILI9341_BLUE;
    }

    tft.fillRoundRect(4 + i * w, y, w - 4, 30, 4, color);
    tft.setTextColor(ILI9341_WHITE);
    tft.setTextSize(1);
    tft.setCursor(13 + i * w, y + 11);
    tft.print(noteNames[i]);

    lastDrawnTouchState[i] = touchState[i];
  }

  noteBarDirty = false;
}

// =====================================================
// MENÜ / DİĞER EKRANLAR
// =====================================================

void drawMenuScreen() {
  // Menüye girince ekran bir kere temizlenir.
  tft.fillScreen(ILI9341_BLACK);
  drawHeader("ANA MENU");

  tft.setTextSize(2);

  for (int i = 0; i < 4; i++) {
    int y = 58 + i * 38;

    if (i == menuIndex) {
      tft.fillRoundRect(18, y - 6, 284, 30, 5, ILI9341_BLUE);
      tft.setTextColor(ILI9341_WHITE);
    } else {
      tft.setTextColor(ILI9341_LIGHTGREY);
    }

    tft.setCursor(35, y);
    tft.print(menuItems[i]);
  }

  tft.setTextColor(ILI9341_WHITE);
  tft.setTextSize(1);
  tft.setCursor(20, 220);
  tft.print("MENU: asagi   OK: sec");
}

void drawTutorialScreen() {
  tft.fillScreen(ILI9341_BLACK);
  drawHeader("TUTORIAL");

  tft.setTextColor(ILI9341_WHITE);
  tft.setTextSize(2);

  tft.setCursor(20, 55);
  tft.print(tutorialMessage);

  tft.setCursor(20, 95);
  tft.print("Skor: ");
  tft.print(score);

  tft.setCursor(20, 135);
  tft.print("Hedef: ");

  if (targetNote >= 0) {
    tft.setTextColor(ILI9341_YELLOW);
    tft.print(noteNames[targetNote]);
  } else {
    tft.setTextColor(ILI9341_LIGHTGREY);
    tft.print("-");
  }

  tft.setTextColor(ILI9341_WHITE);
  tft.setTextSize(1);
  tft.setCursor(20, 180);
  tft.print("OK: yeni hedef | MENU: geri");

  drawNoteBar();
}

void drawRecordScreen() {
  tft.fillScreen(ILI9341_BLACK);
  drawHeader("KAYIT");

  tft.setTextSize(2);
  tft.setCursor(20, 60);

  if (isRecording) {
    tft.setTextColor(ILI9341_RED);
    tft.print("KAYIT ALINIYOR");
  } else {
    tft.setTextColor(ILI9341_WHITE);
    tft.print("Kayit hazir");
  }

  tft.setTextColor(ILI9341_WHITE);
  tft.setCursor(20, 105);
  tft.print("Event: ");
  tft.print(eventCount);
  tft.print("/");
  tft.print(MAX_EVENTS);

  tft.setTextSize(1);
  tft.setCursor(20, 170);
  tft.print("OK: baslat/durdur | MENU: geri");

  drawNoteBar();
}

void drawPlaybackScreen() {
  tft.fillScreen(ILI9341_BLACK);
  drawHeader("KAYIT DINLE");

  tft.setTextSize(2);
  tft.setCursor(20, 60);

  if (isPlayingRecord) {
    tft.setTextColor(ILI9341_GREEN);
    tft.print("CALINIYOR");
  } else {
    tft.setTextColor(ILI9341_WHITE);
    tft.print("Hazir");
  }

  tft.setTextColor(ILI9341_WHITE);
  tft.setCursor(20, 105);
  tft.print("Event: ");
  tft.print(eventCount);

  tft.setCursor(20, 135);
  tft.print("Index: ");
  tft.print(playbackIndex);

  tft.setTextSize(1);
  tft.setCursor(20, 170);
  tft.print("OK: cal/durdur | MENU: geri");

  drawNoteBar();
}

void drawCurrentScreen() {
  if (mode == MODE_STANDARD) {
    if (!standardStaticDrawn || screenDirty) {
      screenDirty = false;
      drawStandardStatic();
    }

    if (paramDirty) {
      updateStandardValues();
      paramDirty = false;
    }

    updateWaveformBoxOnly();

    if (noteBarDirty) {
      updateNoteBarOnlyChanged();
    }

    return;
  }

  if (!screenDirty) {
    if (noteBarDirty && mode != MODE_MENU) {
      drawNoteBar();
    }
    return;
  }

  screenDirty = false;
  paramDirty = false;
  standardStaticDrawn = false;

  switch (mode) {
    case MODE_MENU:
      drawMenuScreen();
      break;

    case MODE_TUTORIAL:
      drawTutorialScreen();
      break;

    case MODE_RECORD:
      drawRecordScreen();
      break;

    case MODE_PLAYBACK:
      drawPlaybackScreen();
      break;

    default:
      drawMenuScreen();
      break;
  }
}

// =====================================================
// BUTON KONTROL
// =====================================================

void resetTouchStates() {
  for (int i = 0; i < 8; i++) {
    touchState[i] = false;
    lastTouchState[i] = false;
    lastDrawnTouchState[i] = false;
  }
}

void goBackToMenu() {
  clearVoices();
  isRecording = false;
  isPlayingRecord = false;
  tutorialActive = false;
  targetNote = -1;
  resetTouchStates();

  mode = MODE_MENU;
  screenDirty = true;
  noteBarDirty = false;
  standardStaticDrawn = false;
}

void handleMenuButton() {
  if (mode == MODE_MENU) {
    menuIndex++;
    if (menuIndex >= 4) menuIndex = 0;
    screenDirty = true;
  } else {
    goBackToMenu();
  }
}

void handleOkButton() {
  if (mode == MODE_MENU) {
    resetTouchStates();

    if (menuIndex == 0) {
      mode = MODE_STANDARD;
      standardStaticDrawn = false;
    }

    if (menuIndex == 1) {
      mode = MODE_TUTORIAL;
    }

    if (menuIndex == 2) {
      mode = MODE_RECORD;
    }

    if (menuIndex == 3) {
      mode = MODE_PLAYBACK;
    }

    screenDirty = true;
    noteBarDirty = true;
    return;
  }

  if (mode == MODE_STANDARD) {
    currentWave = (Waveform)((currentWave + 1) % 4);
    shownWave = (Waveform)99;
    return;
  }

  if (mode == MODE_TUTORIAL) {
    newTutorialTarget();
    return;
  }

  if (mode == MODE_RECORD) {
    if (!isRecording) startRecording();
    else stopRecording();
    return;
  }

  if (mode == MODE_PLAYBACK) {
    if (!isPlayingRecord) startPlayback();
    else stopPlayback();
    return;
  }
}

void updateButtons() {
  bool menuBtn = digitalRead(BTN_MENU);
  bool okBtn = digitalRead(BTN_OK);

  if (millis() - lastButtonTime < 180) {
    lastMenuBtn = menuBtn;
    lastOkBtn = okBtn;
    return;
  }

  if (menuBtn == LOW && lastMenuBtn == HIGH) {
    lastButtonTime = millis();
    handleMenuButton();
  }

  if (okBtn == LOW && lastOkBtn == HIGH) {
    lastButtonTime = millis();
    handleOkButton();
  }

  lastMenuBtn = menuBtn;
  lastOkBtn = okBtn;
}

// =====================================================
// SETUP / LOOP
// =====================================================

void setup() {
  Serial.begin(115200);
  delay(500);

  pinMode(BTN_MENU, INPUT_PULLUP);
  pinMode(BTN_OK, INPUT_PULLUP);

  analogReadResolution(12);

  initSineTable();
  clearVoices();

  SPI.begin(SPI_SCK, SPI_MISO, SPI_MOSI);
  tft.begin();
  tft.setRotation(1);

  // Açılışta ekranı bir kere komple temizle.
  tft.fillScreen(ILI9341_BLACK);

  tft.setTextColor(ILI9341_WHITE);
  tft.setTextSize(3);
  tft.setCursor(35, 70);
  tft.print("OCTOSYNTH");

  tft.setTextSize(2);
  tft.setCursor(35, 120);
  tft.print("Starting...");
  delay(1000);

  randomSeed(analogRead(POT_CUTOFF));

  calibrateTouch();

  setupI2SInternalDAC();

  xTaskCreatePinnedToCore(
    audioTask,
    "audioTask",
    8192,
    NULL,
    1,
    NULL,
    0
  );

  resetTouchStates();

  mode = MODE_MENU;
  screenDirty = true;
  noteBarDirty = false;
  standardStaticDrawn = false;

  Serial.println("Octosynth fixed menu touch + pitch failsafe hazir.");
}

void loop() {
  static unsigned long lastLogicUpdate = 0;

  if (millis() - lastLogicUpdate >= 5) {
    lastLogicUpdate = millis();

    updatePots();
    updateTouch();
    updateButtons();
    updatePlayback();
    drawCurrentScreen();
  }
}