// ============================================================
// LED Tug Shooter v2 — 2 graczy
// Nowe funkcje:
//   - Buzzer (pin 10)
//   - LCD 16x2 I2C (adres 0x27, A4=SDA, A5=SCL)
//   - Czarny przycisk START (pin 11):
//       * wlacza konsole ze stanu OFF
//       * w menu: zatwierdza wybor trybu
//       * w grze: krotkie = pauza/wzznow, przytrzymanie = wroc do menu
//   - Tryb B (Wyjscie): wylacza cala konsole (LCD off, LED black)
//   - Tryb Solo: doty leca z prawej, gracz strzela z lewej
// ============================================================
 
#include <FastLED.h>
#include <Wire.h>
#include <LiquidCrystal_I2C.h>
 
// ============================================================
// KONFIGURACJA HARDWARE
// ============================================================
 
// --- Taśma LED ---
#define LED_PIN     6
#define NUM_LEDS    60
#define BRIGHTNESS  150
#define CENTER      (NUM_LEDS / 2)   // = 30
 
// --- Piny przycisków graczy ---
#define P1_RED    2
#define P1_GREEN  3
#define P1_BLUE   4
 
#define P2_RED    7
#define P2_GREEN  8
#define P2_BLUE   9
 
// --- Przycisk START ---
#define BTN_START 11
 
// --- Buzzer ---
#define BUZZER_PIN 10
 
// --- LCD I2C ---
// Adres 0x27 (najpopularniejszy). Jeśli LCD nie działa, spróbuj 0x3F
LiquidCrystal_I2C lcd(0x27, 16, 2);
 
// ============================================================
// STAŁE GRY
// ============================================================
 
#define P1_BASE   0
#define P2_BASE   (NUM_LEDS - 1)     // = 29
 
#define SEP_MIN   (CENTER / 2)       // =  7  -> P1 przegrywa
#define SEP_MAX   (NUM_LEDS - CENTER / 2) // = 22 -> P2 przegrywa
 
#define FORCE_TO_MOVE   2
#define MAX_DOTS        6   // po 2 naraz w trybie klasycznym
 
// ============================================================
// TRYBY GRY
// ============================================================
 
// Tryb wybierany przyciskiem P1/P2: R=0, G=1, Y=2
// Przycisk START zatwierdza wybor
// Y = wyjscie do menu glownego (resetuje wyniki)
#define MODE_CLASSIC    0   // 2 graczy - klasyczna gra
#define MODE_SOLO       1   // 1 gracz - doty leca z prawej, gracz strzela z lewej
#define MODE_EXIT       2   // Wyjscie do menu glownego
 
const char* MODE_NAMES[3] = {
  "2 graczy      ",
  "Solo          ",
  "Wyjscie       "
};
 
const char* MODE_DESC[3] = {
  "START aby grac",
  "START aby grac",
  "START=powrot  "
};
 
// ============================================================
// KOLORY
// ============================================================
 
CRGB COLORS[3] = {
  CRGB(255,   0,   0),  // czerwony
  CRGB(  0, 255,   0),  // zielony
  CRGB(255, 255,   0),  // żółty
};
 
// ============================================================
// STRUKTURY
// ============================================================
 
struct Dot {
  int  pos;
  int  prevPos;
  int  colorIdx;
  int  dir;
  bool active;
};
 
struct Bullet {
  int  pos;
  int  prevPos;
  int  colorIdx;
  int  dir;
  bool active;
};
 
// ============================================================
// ZMIENNE GLOBALNE
// ============================================================
 
// --- Taśma ---
CRGB leds[NUM_LEDS];
 
// --- Stan maszyny stanów ---
// -1=OFF (wylaczone), 0=menu glowne, 1=wybor trybu, 2=gra, 3=game over, 4=pauza
int  appState        = -1;   // startuje jako OFF
int  selectedMode    = MODE_CLASSIC;
 
// --- Obiekty gry ---
Dot    dots[MAX_DOTS];
Bullet p1Bullet = {0, 0, 0, +1, false};
Bullet p2Bullet = {0, 0, 0, -1, false};
 
// --- Separator ---
int separatorPos   = CENTER;
int separatorForce = 0;
 
// --- Wyniki sesji (ile razy kto wygrał od ostatniego resetu menu) ---
int p1Wins = 0;
int p2Wins = 0;
int soloScore = 0;  // punkty w trybie solo (trafione doty)
 
// --- Timery ---
unsigned long lastMoveTime   = 0;
unsigned long lastSpawnTime  = 0;
unsigned long lastBulletTime = 0;
unsigned long stateEnterTime = 0;   // kiedy weszliśmy w bieżący stan
 
// --- Prędkości (ms) — zmieniane wg trybu ---
int MIGRATE_MS = 350;
int BULLET_MS  = 50;
int SPAWN_MS   = 1200;
 
// --- Stare stany przycisków ---
bool lastP1R = HIGH, lastP1G = HIGH, lastP1B = HIGH;
bool lastP2R = HIGH, lastP2G = HIGH, lastP2B = HIGH;
bool lastStart = HIGH;
 
// --- Przycisk START: hold detection ---
unsigned long startPressedAt = 0;
bool startHeld               = false;
#define HOLD_MS 1500   // ms przytrzymania = akcja hold (powrot do menu z gry)
 
// --- Stan gry ---
bool gameOver = false;
int  loser    = 0;   // 1 lub 2
 
// ============================================================
// DEKLARACJE FUNKCJI
// ============================================================
 
// Maszyna stanów
void enterOff();
void loopOff();
void enterMainMenu();
void loopMainMenu();
void enterModeSelect();
void loopModeSelect();
void enterGame();
void loopGame();
void enterPause();
void loopPause();
void enterGameOver();
void loopGameOver();
 
// Gameplay
void handleButtons();
void spawnDot();
void migrateDots();
void shootP1(int colorIdx);
void shootP2(int colorIdx);
void moveBullet(Bullet &b);
void updateSeparator();
 
// Rendering
void drawAll();
void drawBullet(Bullet &b);
void flashHit(int pos);
void flashWrongHit(int pos);
 
// Animacje LED
void introAnimation();
void gameOverAnimation(int loserPlayer);
 
// LCD
void lcdMainMenu();
void lcdModeSelect(int mode);
void lcdGameScore();
void lcdGameOver(int loserPlayer);
 
// Buzzer
void buzzShoot();
void buzzHit();
void buzzWrongHit();
void buzzGameOver(int loserPlayer);
void buzzIntro();
void buzzModeSelect();
 
// Reset
void softReset();
void applyModeSettings(int mode);
 
// ============================================================
// SETUP
// ============================================================
 
void setup() {
  Serial.begin(9600);
  delay(500);
 
  // LCD
  lcd.init();
  lcd.backlight();
 
  // LED taśma
  FastLED.addLeds<NEOPIXEL, LED_PIN>(leds, NUM_LEDS);
  FastLED.setBrightness(BRIGHTNESS);
 
  // Przyciski graczy
  pinMode(P1_RED,   INPUT_PULLUP);
  pinMode(P1_GREEN, INPUT_PULLUP);
  pinMode(P1_BLUE,  INPUT_PULLUP);
  pinMode(P2_RED,   INPUT_PULLUP);
  pinMode(P2_GREEN, INPUT_PULLUP);
  pinMode(P2_BLUE,  INPUT_PULLUP);
 
  // Przycisk START
  pinMode(BTN_START, INPUT_PULLUP);
 
  // Buzzer
  pinMode(BUZZER_PIN, OUTPUT);
  digitalWrite(BUZZER_PIN, LOW);
 
  randomSeed(analogRead(A0));
 
  for (int i = 0; i < MAX_DOTS; i++) dots[i].active = false;
 
  // Zacznij w stanie OFF — czekaj na czarny przycisk
  enterOff();
}
 
// ============================================================
// GŁÓWNA PĘTLA — maszyna stanów
// ============================================================
 
void loop() {
  switch (appState) {
    case -1: loopOff();        break;
    case  0: loopMainMenu();   break;
    case  1: loopModeSelect(); break;
    case  2: loopGame();       break;
    case  3: loopGameOver();   break;
    case  4: loopPause();      break;
  }
}
 
// ============================================================
// STAN -1 — OFF (konsola wyłączona)
// ============================================================
 
void enterOff() {
  appState = -1;
  fill_solid(leds, NUM_LEDS, CRGB::Black);
  FastLED.show();
  lcd.noBacklight();
  lcd.clear();
  startHeld    = false;
  lastStart    = HIGH;
}
 
void loopOff() {
  bool curStart = digitalRead(BTN_START);
 
  if (curStart == LOW && lastStart == HIGH) {
    // Wciśnięto czarny przycisk — włącz konsolę
    lcd.backlight();
    fill_solid(leds, NUM_LEDS, CRGB::White);
    FastLED.show();
    buzzIntro();
    delay(200);
    fill_solid(leds, NUM_LEDS, CRGB::Black);
    FastLED.show();
    lastStart = curStart;
    enterMainMenu();
    return;
  }
 
  lastStart = curStart;
}
 
// ============================================================
// STAN 0 — MENU GŁÓWNE (powitanie)
// ============================================================
 
void enterMainMenu() {
  appState = 0;
  stateEnterTime = millis();
  fill_solid(leds, NUM_LEDS, CRGB::Black);
  FastLED.show();
  lcdMainMenu();
}
 
void loopMainMenu() {
  // Animacja pulsowania całego paska na biało
  uint8_t glow = beatsin8(30, 60, 200);
  fill_solid(leds, NUM_LEDS, CRGB(glow, glow, glow));
  FastLED.show();
  delay(20);
 
  // Odczyt przycisków
  bool curStart = digitalRead(BTN_START);
  bool curP1R   = digitalRead(P1_RED);
  bool curP1G   = digitalRead(P1_GREEN);
  bool curP1B   = digitalRead(P1_BLUE);
  bool curP2R   = digitalRead(P2_RED);
  bool curP2G   = digitalRead(P2_GREEN);
  bool curP2B   = digitalRead(P2_BLUE);
 
  bool anyPressed = false;
 
  // Dowolny przycisk gracza lub START → przejście do wyboru trybu
  if ((curStart == LOW && lastStart == HIGH) ||
      (curP1R   == LOW && lastP1R   == HIGH) ||
      (curP1G   == LOW && lastP1G   == HIGH) ||
      (curP1B   == LOW && lastP1B   == HIGH) ||
      (curP2R   == LOW && lastP2R   == HIGH) ||
      (curP2G   == LOW && lastP2G   == HIGH) ||
      (curP2B   == LOW && lastP2B   == HIGH)) {
    anyPressed = true;
  }
 
  lastStart = curStart;
  lastP1R = curP1R; lastP1G = curP1G; lastP1B = curP1B;
  lastP2R = curP2R; lastP2G = curP2G; lastP2B = curP2B;
 
  if (anyPressed) {
    buzzModeSelect();
    enterModeSelect();
  }
}
 
void lcdMainMenu() {
  lcd.clear();
  lcd.setCursor(0, 0);
  lcd.print("  LED TUG GAME  ");
  lcd.setCursor(0, 1);
  lcd.print(" Nacisnij START ");
}
 
// ============================================================
// STAN 1 — WYBÓR TRYBU GRY
// ============================================================
 
void enterModeSelect() {
  appState = 1;
  selectedMode = MODE_CLASSIC;
  stateEnterTime = millis();

  // Cały pasek: 3 bloki po 20 diód — R (0-19), G (20-39), Y (40-59)
  // Domyślnie wybrany tryb 0 (R) świeci jasno, pozostałe przyciemnione
  fill_solid(leds, NUM_LEDS, CRGB::Black);
  for (int i =  0; i < 20; i++) leds[i] = CRGB(255,  0,  0);   // R jasny (aktywny)
  for (int i = 20; i < 40; i++) leds[i] = CRGB(  0, 20,  0);   // G ciemny
  for (int i = 40; i < 60; i++) leds[i] = CRGB( 20, 20,  0);   // Y ciemny
  FastLED.show();

  lcdModeSelect(selectedMode);
}
 
void loopModeSelect() {
  bool curP1R = digitalRead(P1_RED);
  bool curP1G = digitalRead(P1_GREEN);
  bool curP1B = digitalRead(P1_BLUE);
  bool curP2R = digitalRead(P2_RED);
  bool curP2G = digitalRead(P2_GREEN);
  bool curP2B = digitalRead(P2_BLUE);
  bool curStart = digitalRead(BTN_START);
 
  bool changed = false;
 
  // Gracz 1 wybiera tryb (R=0, G=1, Y=2)
  if (curP1R == LOW && lastP1R == HIGH) { selectedMode = MODE_CLASSIC; changed = true; }
  if (curP1G == LOW && lastP1G == HIGH) { selectedMode = MODE_SOLO;    changed = true; }
  if (curP1B == LOW && lastP1B == HIGH) { selectedMode = MODE_EXIT;    changed = true; }
 
  // Gracz 2 też może wybrać tryb (R=0, G=1, Y=2)
  if (curP2R == LOW && lastP2R == HIGH) { selectedMode = MODE_CLASSIC; changed = true; }
  if (curP2G == LOW && lastP2G == HIGH) { selectedMode = MODE_SOLO;    changed = true; }
  if (curP2B == LOW && lastP2B == HIGH) { selectedMode = MODE_EXIT;    changed = true; }
 
  if (changed) {
    buzzModeSelect();
    lcdModeSelect(selectedMode);

    // Podświetl wybrany blok jasno, pozostałe przyciemnione
    fill_solid(leds, NUM_LEDS, CRGB::Black);
    for (int i =  0; i < 20; i++) leds[i] = (selectedMode == 0) ? CRGB(255,   0,   0) : CRGB( 20,  0,  0);
    for (int i = 20; i < 40; i++) leds[i] = (selectedMode == 1) ? CRGB(  0, 255,   0) : CRGB(  0, 20,  0);
    for (int i = 40; i < 60; i++) leds[i] = (selectedMode == 2) ? CRGB(200, 200,   0) : CRGB( 20, 20,  0);
    FastLED.show();
  }
 
  // START lub ponowne naciśnięcie tego samego koloru = start gry
  bool startPressed = (curStart == LOW && lastStart == HIGH);
  // (opcjonalnie: drugie kliknięcie tego samego przycisku startuje grę)
 
  lastStart = curStart;
  lastP1R = curP1R; lastP1G = curP1G; lastP1B = curP1B;
  lastP2R = curP2R; lastP2G = curP2G; lastP2B = curP2B;
 
  if (startPressed) {
    enterGame();
  }
}
 
void lcdModeSelect(int mode) {
  lcd.clear();
  lcd.setCursor(0, 0);
  lcd.print("Tryb: ");
  lcd.print(MODE_NAMES[mode]);
  lcd.setCursor(0, 1);
  lcd.print(MODE_DESC[mode]);
}
 
// ============================================================
// STAN 2 — GRA
// ============================================================
 
void enterGame() {
  // Tryb WYJSCIE — wyłącz całą konsolę
  if (selectedMode == MODE_EXIT) {
    p1Wins    = 0;
    p2Wins    = 0;
    // Krótka animacja wyłączania
    for (int i = NUM_LEDS - 1; i >= 0; i--) {
      leds[i] = CRGB::Black;
      FastLED.show();
      delay(15);
    }
    tone(BUZZER_PIN, 330, 200);
    delay(210);
    noTone(BUZZER_PIN);
    enterOff();
    return;
  }
 
  appState = 2;
  stateEnterTime = millis();
 
  softReset();
  applyModeSettings(selectedMode);
  introAnimation();
  lcdGameScore();
 
  unsigned long bootTime = millis();
  lastSpawnTime  = bootTime;
  lastMoveTime   = bootTime;
  lastBulletTime = bootTime;
}
 
void loopGame() {
  if (gameOver) {
    enterGameOver();
    return;
  }
 
  unsigned long now = millis();
 
  // --- Obsługa czarnego przycisku START ---
  bool curStart = digitalRead(BTN_START);
 
  if (curStart == LOW && lastStart == HIGH) {
    // Właśnie wciśnięty
    startPressedAt = now;
    startHeld      = true;
  }
 
  if (startHeld && curStart == LOW) {
    // Trzymany — sprawdź czy przekroczono próg
    if (now - startPressedAt >= HOLD_MS) {
      // Przytrzymanie: wróć do menu
      startHeld = false;
      lastStart = HIGH;
      fill_solid(leds, NUM_LEDS, CRGB::Black);
      FastLED.show();
      buzzModeSelect();
      p1Wins = 0;
      p2Wins = 0;
      enterMainMenu();
      return;
    }
    // Wizualny sygnał że trzymamy — środek świeci pomarańczowo
    uint8_t progress = map(now - startPressedAt, 0, HOLD_MS, 0, NUM_LEDS);
    fill_solid(leds, NUM_LEDS, CRGB::Black);
    for (int i = 0; i < progress && i < NUM_LEDS; i++) {
      leds[i] = CRGB(180, 60, 0);
    }
    FastLED.show();
    lastStart = curStart;
    return;
  }
 
  if (curStart == HIGH && lastStart == LOW && startHeld) {
    // Krótkie wciśnięcie — pauza
    startHeld = false;
    lastStart = curStart;
    enterPause();
    return;
  }
 
  lastStart = curStart;
 
  // --- Normalny przebieg gry ---
  handleButtons();
 
  if (now - lastSpawnTime >= (unsigned long)SPAWN_MS) {
    spawnDot();
    lastSpawnTime = now;
  }
 
  if (now - lastBulletTime >= (unsigned long)BULLET_MS) {
    moveBullet(p1Bullet);
    moveBullet(p2Bullet);
    lastBulletTime = now;
  }
 
  if (now - lastMoveTime >= (unsigned long)MIGRATE_MS) {
    migrateDots();
    lastMoveTime = now;
  }
 
  drawAll();
  FastLED.show();
}
 
// ============================================================
// STAN 4 — PAUZA
// ============================================================
 
void enterPause() {
  appState = 4;
  startHeld = false;
  tone(BUZZER_PIN, 440, 80);
  delay(85);
  noTone(BUZZER_PIN);
  lcd.setCursor(0, 0);
  lcd.print("  --- PAUZA --- ");
  lcd.setCursor(0, 1);
  lcd.print("START = wznow   ");
}
 
void loopPause() {
  // Pulsuj środek na żółto
  uint8_t glow = beatsin8(40, 40, 180);
  fill_solid(leds, NUM_LEDS, CRGB::Black);
  leds[CENTER] = CRGB(glow, glow, 0);
  FastLED.show();
  delay(20);
 
  bool curStart = digitalRead(BTN_START);
 
  if (curStart == LOW && lastStart == HIGH) {
    startPressedAt = millis();
    startHeld      = true;
  }
 
  if (startHeld && curStart == LOW) {
    if (millis() - startPressedAt >= HOLD_MS) {
      // Przytrzymanie z pauzy: wróć do menu
      startHeld = false;
      lastStart = HIGH;
      fill_solid(leds, NUM_LEDS, CRGB::Black);
      FastLED.show();
      buzzModeSelect();
      p1Wins = 0;
      p2Wins = 0;
      enterMainMenu();
      return;
    }
  }
 
  if (curStart == HIGH && lastStart == LOW && startHeld) {
    // Krótkie wciśnięcie: wznów grę
    startHeld = false;
    lastStart = curStart;
    tone(BUZZER_PIN, 660, 80);
    delay(85);
    noTone(BUZZER_PIN);
    // Zsynchronizuj timery
    unsigned long now = millis();
    lastSpawnTime  = now;
    lastMoveTime   = now;
    lastBulletTime = now;
    lcdGameScore();
    appState = 2;
    return;
  }
 
  lastStart = curStart;
}
 
// ============================================================
// STAN 3 — GAME OVER
// ============================================================
 
void enterGameOver() {
  appState = 3;
  stateEnterTime = millis();
 
  // Zaktualizuj wyniki (tylko w trybie klasycznym)
  if (selectedMode != MODE_SOLO) {
    if (loser == 1) p2Wins++;
    else             p1Wins++;
  }
 
  lcdGameOver(loser);
  buzzGameOver(loser);
  gameOverAnimation(loser);
}
 
void loopGameOver() {
  // Po animacji czekamy na dowolny przycisk
  bool curStart = digitalRead(BTN_START);
  bool curP1R   = digitalRead(P1_RED);
  bool curP1G   = digitalRead(P1_GREEN);
  bool curP1B   = digitalRead(P1_BLUE);
  bool curP2R   = digitalRead(P2_RED);
  bool curP2G   = digitalRead(P2_GREEN);
  bool curP2B   = digitalRead(P2_BLUE);
 
  bool anyPressed = (curStart == LOW && lastStart == HIGH) ||
                    (curP1R   == LOW && lastP1R   == HIGH) ||
                    (curP1G   == LOW && lastP1G   == HIGH) ||
                    (curP1B   == LOW && lastP1B   == HIGH) ||
                    (curP2R   == LOW && lastP2R   == HIGH) ||
                    (curP2G   == LOW && lastP2G   == HIGH) ||
                    (curP2B   == LOW && lastP2B   == HIGH);
 
  lastStart = curStart;
  lastP1R = curP1R; lastP1G = curP1G; lastP1B = curP1B;
  lastP2R = curP2R; lastP2G = curP2G; lastP2B = curP2B;
 
  if (anyPressed) {
    // Wróć do wyboru trybu (nie do menu głównego — zachowaj wyniki)
    buzzModeSelect();
    enterModeSelect();
  }
 
  // Pulsuj taśmą w oczekiwaniu
  uint8_t glow = beatsin8(20, 30, 120);
  fill_solid(leds, NUM_LEDS, CRGB::Black);
  // Gracz 1 przegrał = czerwony, Gracz 2 przegrał = żółty
  CRGB loserColor = (loser == 1) ? CRGB(glow, 0, 0) : CRGB(glow, glow, 0);
  int zone = NUM_LEDS / 6;
  if (loser == 1) {
    for (int i = 0; i < zone; i++) leds[i] = loserColor;
  } else {
    for (int i = NUM_LEDS - zone; i < NUM_LEDS; i++) leds[i] = loserColor;
  }
  FastLED.show();
  delay(20);
}
 
void lcdGameOver(int loserPlayer) {
  lcd.clear();
  lcd.setCursor(0, 0);
  if (selectedMode == MODE_SOLO) {
    lcd.print("Koniec! Wynik:  ");
    lcd.setCursor(0, 1);
    lcd.print("Punkty: ");
    lcd.print(soloScore);
    lcd.print("        ");
  } else {
    if (loserPlayer == 1) {
      lcd.print("Gracz 2 wygral! ");
    } else {
      lcd.print("Gracz 1 wygral! ");
    }
    lcd.setCursor(0, 1);
    lcd.print("P1:");
    lcd.print(p1Wins);
    lcd.print("  P2:");
    lcd.print(p2Wins);
    lcd.print("      ");
  }
}
 
// ============================================================
// LCD — WYNIK PODCZAS GRY
// ============================================================
 
void lcdGameScore() {
  lcd.setCursor(0, 0);
  if (selectedMode == MODE_SOLO) {
    lcd.print("Punkty: ");
    lcd.print(soloScore);
    lcd.print("        ");
  } else {
    lcd.print("P1:");
    lcd.print(p1Wins);
    lcd.print("   P2:");
    lcd.print(p2Wins);
    lcd.print("   ");
  }
  lcd.setCursor(0, 1);
  lcd.print(MODE_NAMES[selectedMode]);
}
 
// Odśwież LCD co jakiś czas podczas gry
unsigned long lastLcdUpdate = 0;
const int LCD_UPDATE_MS = 300;
 
// ============================================================
// OBSŁUGA PRZYCISKÓW (podczas gry)
// ============================================================
 
void handleButtons() {
  bool curP1R = digitalRead(P1_RED);
  bool curP1G = digitalRead(P1_GREEN);
  bool curP1B = digitalRead(P1_BLUE);
  bool curP2R = digitalRead(P2_RED);
  bool curP2G = digitalRead(P2_GREEN);
  bool curP2B = digitalRead(P2_BLUE);
 
  if (curP1R == LOW && lastP1R == HIGH) { shootP1(0); buzzShoot(); }
  if (curP1G == LOW && lastP1G == HIGH) { shootP1(1); buzzShoot(); }
  if (curP1B == LOW && lastP1B == HIGH) { shootP1(2); buzzShoot(); }
 
  // W trybie SOLO gracz 2 nie istnieje
  if (selectedMode != MODE_SOLO) {
    if (curP2R == LOW && lastP2R == HIGH) { shootP2(0); buzzShoot(); }
    if (curP2G == LOW && lastP2G == HIGH) { shootP2(1); buzzShoot(); }
    if (curP2B == LOW && lastP2B == HIGH) { shootP2(2); buzzShoot(); }
  }
 
  lastP1R = curP1R; lastP1G = curP1G; lastP1B = curP1B;
  lastP2R = curP2R; lastP2G = curP2G; lastP2B = curP2B;
 
  // Odśwież LCD co LCD_UPDATE_MS
  unsigned long now = millis();
  if (now - lastLcdUpdate >= LCD_UPDATE_MS) {
    lcdGameScore();
    lastLcdUpdate = now;
  }
}
 
// ============================================================
// SPAWN DOTÓW
// ============================================================
 
void spawnDot() {
  if (selectedMode == MODE_SOLO) {
    // Solo — jeden dot lecący w stronę gracza
    for (int i = 0; i < MAX_DOTS; i++) {
      if (!dots[i].active) {
        dots[i].colorIdx = random(0, 3);
        dots[i].pos      = P2_BASE - 1;
        dots[i].prevPos  = dots[i].pos;
        dots[i].dir      = -1;
        dots[i].active   = true;
        break;
      }
    }
  } else {
    // Klasyczny — dwa doty naraz, jeden w każdą stronę, niezależne kolory
    int freeSlots[MAX_DOTS];
    int freeCount = 0;
    for (int i = 0; i < MAX_DOTS; i++) {
      if (!dots[i].active) freeSlots[freeCount++] = i;
    }
 
    // Dot w stronę P1 (leci od separatora w lewo)
    if (freeCount >= 1) {
      int idx = freeSlots[0];
      dots[idx].colorIdx = random(0, 3);
      dots[idx].pos      = separatorPos - 1;
      dots[idx].prevPos  = dots[idx].pos;
      dots[idx].dir      = -1;
      dots[idx].active   = true;
    }
 
    // Dot w stronę P2 (leci od separatora w prawo)
    if (freeCount >= 2) {
      int idx = freeSlots[1];
      dots[idx].colorIdx = random(0, 3);
      dots[idx].pos      = separatorPos + 1;
      dots[idx].prevPos  = dots[idx].pos;
      dots[idx].dir      = +1;
      dots[idx].active   = true;
    }
  }
}
 
// ============================================================
// RUCH DOTÓW
// ============================================================
 
void migrateDots() {
  for (int i = 0; i < MAX_DOTS; i++) {
    if (!dots[i].active) continue;
 
    dots[i].prevPos = dots[i].pos;
    dots[i].pos    += dots[i].dir;
 
    if (dots[i].pos <= P1_BASE) {
      gameOver = true;
      loser    = 1;
      return;
    }
 
    if (selectedMode == MODE_SOLO) {
      // W trybie solo dot który dotrze do prawej krawędzi po prostu znika (już strzelono albo ominięto)
      if (dots[i].pos >= P2_BASE) {
        dots[i].active = false;
      }
    } else {
      if (dots[i].pos >= P2_BASE) {
        gameOver = true;
        loser    = 2;
        return;
      }
    }
  }
}
 
// ============================================================
// STRZELANIE
// ============================================================
 
void shootP1(int colorIdx) {
  if (p1Bullet.active) return;
  p1Bullet.pos      = P1_BASE + 1;
  p1Bullet.prevPos  = p1Bullet.pos;
  p1Bullet.colorIdx = colorIdx;
  p1Bullet.dir      = +1;
  p1Bullet.active   = true;
}
 
void shootP2(int colorIdx) {
  if (p2Bullet.active) return;
  p2Bullet.pos      = P2_BASE - 1;
  p2Bullet.prevPos  = p2Bullet.pos;
  p2Bullet.colorIdx = colorIdx;
  p2Bullet.dir      = -1;
  p2Bullet.active   = true;
}
 
// ============================================================
// RUCH KULI I KOLIZJE
// ============================================================
 
void moveBullet(Bullet &b) {
  if (!b.active) return;
 
  b.prevPos = b.pos;
  b.pos    += b.dir;
 
  if (b.pos <= P1_BASE || b.pos >= P2_BASE) {
    b.active = false;
    return;
  }
 
  if (selectedMode != MODE_SOLO && b.pos == separatorPos) {
    b.active = false;
    return;
  }
 
  for (int i = 0; i < MAX_DOTS; i++) {
    if (!dots[i].active) continue;
 
    bool directHit  = (dots[i].pos == b.pos);
    bool crossedHit = (dots[i].prevPos == b.pos && dots[i].pos == b.prevPos);
 
    if (!directHit && !crossedHit) continue;
 
    if (dots[i].colorIdx == b.colorIdx) {
      flashHit(b.pos);
      buzzHit();
      dots[i].active = false;
      b.active       = false;
      if (selectedMode == MODE_SOLO) {
        soloScore++;
      } else {
        separatorForce += b.dir;
        updateSeparator();
      }
    } else {
      flashWrongHit(b.pos);
      buzzWrongHit();
      b.active = false;
      if (selectedMode != MODE_SOLO) {
        separatorForce -= b.dir;
        updateSeparator();
      }
    }
    return;
  }
}
 
// ============================================================
// AKTUALIZACJA SEPARATORA
// ============================================================
 
void updateSeparator() {
  if (separatorForce >= FORCE_TO_MOVE) {
    separatorPos++;
    separatorForce = 0;
    Serial.print("Separator -> ");
    Serial.println(separatorPos);
  }
 
  if (separatorForce <= -FORCE_TO_MOVE) {
    separatorPos--;
    separatorForce = 0;
    Serial.print("Separator -> ");
    Serial.println(separatorPos);
  }
 
  if (separatorPos <= SEP_MIN) {
    gameOver = true;
    loser    = 1;
  }
 
  if (separatorPos >= SEP_MAX) {
    gameOver = true;
    loser    = 2;
  }
}
 
// ============================================================
// RENDEROWANIE
// ============================================================
 
void drawAll() {
  fadeToBlackBy(leds, NUM_LEDS, 20);
 
  leds[P1_BASE] = CRGB(40, 40, 40);
 
  if (selectedMode == MODE_SOLO) {
    // W trybie solo brak separatora, prawa baza też niewidoczna
  } else {
    leds[P2_BASE] = CRGB(40, 40, 40);
 
    uint8_t glow   = beatsin8(20, 100, 255);
    int     offset = separatorPos - CENTER;
    CRGB    sepColor;
 
    if      (offset < 0) sepColor = CRGB(glow, glow / 3, glow / 3);
    else if (offset > 0) sepColor = CRGB(glow / 3, glow / 3, glow);
    else                 sepColor = CRGB(glow, glow, glow);
 
    leds[separatorPos] = sepColor;
    if (separatorPos > 0)            leds[separatorPos - 1] += sepColor / 4;
    if (separatorPos < NUM_LEDS - 1) leds[separatorPos + 1] += sepColor / 4;
  }
 
  for (int i = 0; i < MAX_DOTS; i++) {
    if (!dots[i].active) continue;
    leds[dots[i].pos] = COLORS[dots[i].colorIdx];
  }
 
  drawBullet(p1Bullet);
  drawBullet(p2Bullet);
}
 
void drawBullet(Bullet &b) {
  if (!b.active) return;
  leds[b.pos] = COLORS[b.colorIdx];
  int trailPos = b.pos - b.dir;
  if (trailPos >= 0 && trailPos < NUM_LEDS) {
    leds[trailPos] += COLORS[b.colorIdx] / 5;
  }
}
 
// ============================================================
// EFEKTY WIZUALNE
// ============================================================
 
void flashHit(int pos) {
  leds[pos] = CRGB::White;
  if (pos > 0)            leds[pos - 1] += CRGB(80, 80, 80);
  if (pos < NUM_LEDS - 1) leds[pos + 1] += CRGB(80, 80, 80);
  FastLED.show();
}
 
void flashWrongHit(int pos) {
  leds[pos] = CRGB(255, 120, 0);
  if (pos > 0)            leds[pos - 1] += CRGB(80, 30, 0);
  if (pos < NUM_LEDS - 1) leds[pos + 1] += CRGB(80, 30, 0);
  FastLED.show();
}
 
// ============================================================
// ANIMACJA GAME OVER (LED)
// ============================================================
 
void gameOverAnimation(int loserPlayer) {
  int zone = NUM_LEDS / 6;
 
  for (int rep = 0; rep < 6; rep++) {
    fill_solid(leds, NUM_LEDS, CRGB::Black);
 
    if (loserPlayer == 1) {
      for (int i = 0; i < zone; i++) leds[i] = CRGB(255, 0, 0);
    } else {
      for (int i = NUM_LEDS - zone; i < NUM_LEDS; i++) leds[i] = CRGB(255, 0, 0);
    }
 
    FastLED.show();
    delay(180);
 
    fill_solid(leds, NUM_LEDS, CRGB::Black);
    FastLED.show();
    delay(120);
  }
}
 
// ============================================================
// ANIMACJA INTRO (LED)
// ============================================================
 
void introAnimation() {
  fill_solid(leds, NUM_LEDS, CRGB::Black);
 
  for (int i = 0; i < NUM_LEDS; i++) {
    leds[i] = CRGB::White;
    FastLED.show();
    delay(18);
  }
 
  for (int i = 0; i < NUM_LEDS; i++) {
    leds[i] = CRGB::Black;
    FastLED.show();
    delay(10);
  }
 
  leds[CENTER] = CRGB::White;
  FastLED.show();
}
 
// ============================================================
// RESET
// ============================================================
 
void softReset() {
  for (int i = 0; i < MAX_DOTS; i++) dots[i].active = false;
 
  p1Bullet.active = false;
  p2Bullet.active = false;
 
  separatorPos   = CENTER;
  separatorForce = 0;
  soloScore      = 0;
 
  gameOver       = false;
  loser          = 0;
  startHeld      = false;
}
 
void applyModeSettings(int mode) {
  switch (mode) {
    case MODE_CLASSIC:
      MIGRATE_MS = 350;
      BULLET_MS  = 50;
      SPAWN_MS   = 1200;
      break;
 
    case MODE_SOLO:
      // Doty szybsze i częstsze — gracz sam przed falą
      MIGRATE_MS = 280;
      BULLET_MS  = 50;
      SPAWN_MS   = 900;
      break;
 
    default:
      MIGRATE_MS = 350;
      BULLET_MS  = 50;
      SPAWN_MS   = 1200;
      break;
  }
}
 
// ============================================================
// BUZZER — FUNKCJE POMOCNICZE
// ============================================================
 
// Krótki ton o podanej częstotliwości i czasie (ms)
void buzzTone(int freq, int durationMs) {
  tone(BUZZER_PIN, freq, durationMs);
  delay(durationMs + 5);
  noTone(BUZZER_PIN);
}
 
void buzzShoot() {
  // Krótki wysoki pisk przy strzale
  tone(BUZZER_PIN, 1200, 40);
  delay(45);
  noTone(BUZZER_PIN);
}
 
void buzzHit() {
  // Podwójny pozytywny dźwięk przy trafieniu
  tone(BUZZER_PIN, 880, 60);
  delay(70);
  tone(BUZZER_PIN, 1100, 80);
  delay(85);
  noTone(BUZZER_PIN);
}
 
void buzzWrongHit() {
  // Niski nieprzyjemny ton przy złym trafieniu
  tone(BUZZER_PIN, 220, 150);
  delay(155);
  noTone(BUZZER_PIN);
}
 
void buzzGameOver(int loserPlayer) {
  // Opadający motyw dla przegranego
  int freqs[] = {880, 740, 587, 440};
  for (int i = 0; i < 4; i++) {
    tone(BUZZER_PIN, freqs[i], 150);
    delay(160);
  }
  noTone(BUZZER_PIN);
  delay(100);
  // Triumfalny akord dla wygrywającego (jeden ton, bo buzzer)
  tone(BUZZER_PIN, 1047, 300);
  delay(310);
  noTone(BUZZER_PIN);
}
 
void buzzIntro() {
  // Rosnący motyw startowy
  int freqs[] = {440, 523, 659, 880};
  for (int i = 0; i < 4; i++) {
    tone(BUZZER_PIN, freqs[i], 100);
    delay(110);
  }
  noTone(BUZZER_PIN);
}
 
void buzzModeSelect() {
  // Krótki klik potwierdzenia
  tone(BUZZER_PIN, 660, 50);
  delay(55);
  noTone(BUZZER_PIN);
}
