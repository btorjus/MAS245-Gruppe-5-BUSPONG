#include <Arduino.h>
#include <FlexCAN_T4.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>
#include <SPI.h>

// --- SK Pang / Teensy 3.6 pins ---
namespace carrier {
  namespace pin {
    constexpr uint8_t joyClick {19}; // velg master
    constexpr uint8_t joyUp    {22};
    constexpr uint8_t joyDown  {23};

    constexpr uint8_t oledDcPower {6};
    constexpr uint8_t oledCs      {10};
    constexpr uint8_t oledReset   {5};
  }
  namespace oled { constexpr int W=128, H=64; }
}

// OLED via SPI
Adafruit_SSD1306 display(
  carrier::oled::W, carrier::oled::H,
  &SPI, carrier::pin::oledDcPower,
  carrier::pin::oledReset, carrier::pin::oledCs
);

// CAN0
FlexCAN_T4<CAN0, RX_SIZE_256, TX_SIZE_16> can0;

// --- IDs (sett riktig gruppenr) ---
constexpr uint8_t  GROUP_ID   = 5;
constexpr uint32_t PADDLE_ID  = GROUP_ID + 20; // egen/remote paddlepos
constexpr uint32_t BALL_ID    = GROUP_ID + 50; // ballpos
constexpr uint32_t SCORE_ID   = GROUP_ID + 55; // poeng

// --- Geometri / spillefelt ---
constexpr int SW = carrier::oled::W;
constexpr int SH = carrier::oled::H;

constexpr int BORDER     = 3;
constexpr int TITLE_H    = 0;                    // ingen tittel
constexpr int PLAY_LEFT  = BORDER + 1;
constexpr int PLAY_RIGHT = SW - BORDER - 2;
constexpr int PLAY_TOP   = BORDER + TITLE_H + 1;
constexpr int PLAY_BOT   = SH - BORDER - 2;

constexpr int PAD_W = 2;
constexpr int PAD_H = 20;                        // høyde, god dekning av midten
constexpr int PAD_X_MY  = PLAY_RIGHT - 2 - PAD_W;
constexpr int PAD_X_OPP = PLAY_LEFT  + 2;

constexpr uint8_t BALL_R = 2;

// --- Hastigheter/rater ---
constexpr uint32_t TX_PADDLE_MS = 40;  // 25 Hz
constexpr uint32_t TX_BALL_MS   = 10;  // 100 Hz
constexpr uint32_t STEP_DT_MS   = 15;  // paddle step-rate
constexpr int      VEL_X_INIT   = -1;  // 1 px / 10ms = 100 px/s
constexpr int      VEL_Y_INIT   =  1;

// --- Tilstand ---
volatile bool isMaster = false;
int myY  = (PLAY_TOP + PLAY_BOT - PAD_H)/2;
int oppY = (PLAY_TOP + PLAY_BOT - PAD_H)/2;

bool  ballVisible = false;
int   ballX = (PLAY_LEFT + PLAY_RIGHT)/2;
int   ballY = (PLAY_TOP  + PLAY_BOT)/2;
int   vx = VEL_X_INIT, vy = VEL_Y_INIT;

uint8_t scoreL = 0;    // venstre (motstander)
uint8_t scoreR = 0;    // høyre (deg)

// --- Utils ---
static inline int clampi(int v,int lo,int hi){ return v<lo?lo:(v>hi?hi:v); }
static inline uint8_t centerByteFromTop(int top){ return (uint8_t)clampi(top + PAD_H/2, 0, 63); }
static inline int topFromCenterByte(uint8_t c){ return clampi((int)c - PAD_H/2, PLAY_TOP, PLAY_BOT - PAD_H); }

// --- Tegn statisk ramme ---
static void drawStaticFrame() {
  display.clearDisplay();
  display.drawRect(0, 0, SW, SH, SSD1306_WHITE);
  display.drawRect(BORDER, BORDER, SW-2*BORDER, SH-2*BORDER, SSD1306_WHITE);
  display.display();
}

// --- Render dynamikk (felt, midtstrek, score, paddler, ball) ---
static void render() {
  // Tøm felt + margin = BALL_R for å fjerne evt. ghost langs kant
  int clearX = max(PLAY_LEFT - BALL_R, 1);
  int clearY = max(PLAY_TOP  - BALL_R, 1);
  int clearW = min(PLAY_RIGHT + BALL_R, SW-2) - clearX + 1;
  int clearH = min(PLAY_BOT   + BALL_R, SH-2) - clearY + 1;
  display.fillRect(clearX, clearY, clearW, clearH, SSD1306_BLACK);

  // Midtstrek
  for (int y = PLAY_TOP; y <= PLAY_BOT; y += 4)
    display.drawFastVLine((SW/2), y, 2, SSD1306_WHITE);

  // --- FIX: poengtelling justert for tosifrede tall ---
  display.setTextSize(1);
  display.setTextColor(SSD1306_WHITE);

  // Venstre score
  display.setCursor(PLAY_LEFT + 2, BORDER + 1);
  display.print(scoreL);

  // Høyre score – flytt litt til venstre hvis tosifret
  int xRight = (scoreR >= 10) ? (PLAY_RIGHT - 12) : (PLAY_RIGHT - 6);
  display.setCursor(xRight, BORDER + 1);
  display.print(scoreR);

  // Paddler
  display.fillRect(PAD_X_OPP, oppY, PAD_W, PAD_H, SSD1306_WHITE);
  display.fillRect(PAD_X_MY,  myY,  PAD_W, PAD_H, SSD1306_WHITE);

  // Ball
  if (ballVisible) display.fillCircle(ballX, ballY, BALL_R, SSD1306_WHITE);

  display.display();
}

// --- CAN RX ---
static void onRx(const CAN_message_t& m) {
  if (m.id == PADDLE_ID && m.len >= 1) {
    oppY = topFromCenterByte(m.buf[0]);
  } else if (m.id == BALL_ID && m.len >= 2) {
    // Speil X for motstanders koordinater
    uint8_t rx = m.buf[0], ry = m.buf[1];
    ballX = (SW - 1) - rx;
    ballY = ry;
    ballVisible = true;
  } else if (m.id == SCORE_ID && m.len >= 2) {
    scoreL = m.buf[0];
    scoreR = m.buf[1];
  }
}

// --- Oppstart ---
void setup() {
  Serial.begin(115200);
  delay(200);

  pinMode(carrier::pin::joyUp,    INPUT_PULLUP);
  pinMode(carrier::pin::joyDown,  INPUT_PULLUP);
  pinMode(carrier::pin::joyClick, INPUT_PULLUP);

  if (!display.begin(SSD1306_SWITCHCAPVCC)) { while(1); }
  drawStaticFrame();

  can0.begin();
  can0.setBaudRate(500000);
  can0.enableFIFO();
  can0.enableFIFOInterrupt();
  can0.onReceive(onRx);

  myY  = clampi(myY,  PLAY_TOP, PLAY_BOT - PAD_H);
  oppY = clampi(oppY, PLAY_TOP, PLAY_BOT - PAD_H);
  ballVisible = false;

  Serial.println(F("Ping-Pong: klar (CAN0 @ 500k)"));
}

// --- Hovedløkke ---
void loop() {
  can0.events();

  // Mastervalg: første klikk
  static bool masterLocked=false;
  if (!masterLocked && digitalRead(carrier::pin::joyClick)==LOW) {
    isMaster = true; masterLocked = true;
    // Serve fra høyre
    ballX = PAD_X_MY - 4;
    ballY = myY + PAD_H/2;
    vx = VEL_X_INIT; vy = VEL_Y_INIT;
    ballVisible = true;
    delay(150); // debounce
  }

  // Din paddle: 1 px hvert 15 ms for mange mellomposisjoner
  static uint32_t tStep = 0;
  const bool up   = (digitalRead(carrier::pin::joyUp)  == LOW);
  const bool down = (digitalRead(carrier::pin::joyDown)== LOW);
  if (millis() - tStep >= STEP_DT_MS) {
    tStep = millis();
    if (up   && !down) myY -= 1;
    if (down && !up)   myY += 1;
    myY = clampi(myY, PLAY_TOP, PLAY_BOT - PAD_H);
  }

  // Send paddlepos @ 25 Hz
  static uint32_t tPad=0;
  if (millis()-tPad >= TX_PADDLE_MS) {
    tPad = millis();
    CAN_message_t tx{};
    tx.id  = PADDLE_ID;
    tx.len = 1;
    tx.buf[0] = centerByteFromTop(myY);
    can0.write(tx);
  }

  // Master: ballfysikk @ 100 Hz
  static uint32_t tBall=0;
  if (isMaster && (millis()-tBall >= TX_BALL_MS)) {
    tBall = millis();

    ballX += vx;
    ballY += vy;

    // Sprett i topp/bunn
    if (ballY <= PLAY_TOP + BALL_R)       { ballY = PLAY_TOP + BALL_R;       vy = -vy; }
    if (ballY >= PLAY_BOT - BALL_R)       { ballY = PLAY_BOT - BALL_R;       vy = -vy; }

    // Kollisjon venstre paddle
    if (ballX - BALL_R <= PAD_X_OPP + PAD_W &&
        ballY >= oppY && ballY <= oppY + PAD_H) {
      ballX = PAD_X_OPP + PAD_W + BALL_R;
      vx = -vx;
    }

    // Kollisjon høyre paddle
    if (ballX + BALL_R >= PAD_X_MY &&
        ballY >= myY && ballY <= myY + PAD_H) {
      ballX = PAD_X_MY - BALL_R;
      vx = -vx;
    }

    // Scoring + clamp X for å unngå at sirkelen tegnes utenfor feltet
    bool scored = false;
    if (ballX < PLAY_LEFT + BALL_R)  { scoreR++; scored = true; }
    if (ballX > PLAY_RIGHT - BALL_R) { scoreL++; scored = true; }

    if (scored) {
      CAN_message_t s{};
      s.id = SCORE_ID; s.len = 2;
      s.buf[0] = scoreL; s.buf[1] = scoreR;
      can0.write(s);

      // Ny serve fra høyre
      ballX = PAD_X_MY - 4;
      ballY = myY + PAD_H/2;
      vx = VEL_X_INIT; vy = (vy>=0)?VEL_Y_INIT:-VEL_Y_INIT;
      ballVisible = true;
    } else {
      // Hold ball-sentrum innenfor feltet i X
      ballX = clampi(ballX, PLAY_LEFT + BALL_R, PLAY_RIGHT - BALL_R);
    }

    // Send ballpos (vårt koordinatsystem)
    CAN_message_t tx{};
    tx.id  = BALL_ID;
    tx.len = 2;
    tx.buf[0] = (uint8_t)clampi(ballX, 0, 127);
    tx.buf[1] = (uint8_t)clampi(ballY, 0,  63);
    can0.write(tx);
  }

  // Tegn alt ~30 Hz
  static uint32_t tDraw=0;
  if (millis()-tDraw >= 33) {
    tDraw = millis();
    render();
  }
}