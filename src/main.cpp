
#include <Arduino.h>
#include <FlexCAN_T4.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>
#include <SPI.h>

// ============================================================================
// HARDWARE KONFIGURASJON (SK Pang / Teensy 3.6)
// ============================================================================
namespace carrier 
{
  namespace pin 
  {
    constexpr uint8_t joyClick    = 19;
    constexpr uint8_t joyUp       = 22;
    constexpr uint8_t joyDown     = 23;

    constexpr uint8_t oledDcPower = 6;
    constexpr uint8_t oledCs      = 10;
    constexpr uint8_t oledReset   = 5;
  }
  
  namespace oled 
  { 
    constexpr int W = 128;
    constexpr int H = 64;
  }
}

// OLED skjerm via SPI
Adafruit_SSD1306 display(
  carrier::oled::W, 
  carrier::oled::H,
  &SPI, 
  carrier::pin::oledDcPower,
  carrier::pin::oledReset, 
  carrier::pin::oledCs
);

// CAN-buss
FlexCAN_T4<CAN0, RX_SIZE_256, TX_SIZE_16> can0;

// ============================================================================
// CAN MELDINGSTYPER (sett riktig gruppenummer)
// ============================================================================
constexpr uint8_t  GROUP_ID  = 5;
constexpr uint32_t PADDLE_ID = GROUP_ID + 20;  // Paddle-posisjon
constexpr uint32_t BALL_ID   = GROUP_ID + 50;  // Ball-posisjon
constexpr uint32_t SCORE_ID  = GROUP_ID + 55;  // Poengstilling

// ============================================================================
// SPILLEFELT GEOMETRI
// ============================================================================
constexpr int SCREEN_WIDTH  = carrier::oled::W;
constexpr int SCREEN_HEIGHT = carrier::oled::H;

// Ramme
constexpr int BORDER   = 3;
constexpr int TITLE_H  = 0;  // Ingen tittel

// Spillbart område
constexpr int PLAY_LEFT  = BORDER + 1;
constexpr int PLAY_RIGHT = SCREEN_WIDTH - BORDER - 2;
constexpr int PLAY_TOP   = BORDER + TITLE_H + 1;
constexpr int PLAY_BOT   = SCREEN_HEIGHT - BORDER - 2;

// Paddle
constexpr int PADDLE_WIDTH  = 2;
constexpr int PADDLE_HEIGHT = 20;
constexpr int PADDLE_X_OWN      = PLAY_RIGHT - 2 - PADDLE_WIDTH;  // Høyre side (deg)
constexpr int PADDLE_X_OPPONENT = PLAY_LEFT + 2;                   // Venstre side (motstander)

// Ball
constexpr int BALL_RADIUS = 2;

// ============================================================================
// OPPDATERINGSRATER
// ============================================================================
constexpr uint32_t PADDLE_SEND_INTERVAL_MS = 40;  // 25 Hz - Send paddle-posisjon
constexpr uint32_t BALL_UPDATE_INTERVAL_MS  = 10;  // 100 Hz - Ball-fysikk (kun master)
constexpr uint32_t PADDLE_MOVE_INTERVAL_MS  = 15;  // Paddle-bevegelse (1 pixel per 15 ms)
constexpr uint32_t DRAW_INTERVAL_MS         = 33;  // ~30 FPS - Tegning

// Ballhastighet
constexpr int BALL_VELOCITY_X_INIT = -1;  // Starter mot venstre
constexpr int BALL_VELOCITY_Y_INIT = 1;

// ============================================================================
// SPILLTILSTAND
// ============================================================================
volatile bool isMaster = false;

// Paddle-posisjoner (Y-koordinat for toppen av paddle)
int ownPaddleY      = (PLAY_TOP + PLAY_BOT - PADDLE_HEIGHT) / 2;
int opponentPaddleY = (PLAY_TOP + PLAY_BOT - PADDLE_HEIGHT) / 2;

// Ball-tilstand
bool ballVisible = false;
int  ballX       = (PLAY_LEFT + PLAY_RIGHT) / 2;
int  ballY       = (PLAY_TOP + PLAY_BOT) / 2;
int  ballVelocityX = BALL_VELOCITY_X_INIT;
int  ballVelocityY = BALL_VELOCITY_Y_INIT;

// Poengstilling
uint8_t scoreLeft  = 0;  // Venstre side (motstander)
uint8_t scoreRight = 0;  // Høyre side (deg)

// ============================================================================
// HJELPEFUNKSJONER
// ============================================================================

// Begrens verdi til gitt område
static inline int clamp(int value, int minValue, int maxValue) 
{
  if (value < minValue) return minValue;
  if (value > maxValue) return maxValue;
  return value;
}

// Konverter paddle-topp til senter-byte for CAN-sending
static inline uint8_t paddleTopToCenter(int top) 
{
  return (uint8_t)clamp(top + PADDLE_HEIGHT / 2, 0, 63);
}

// Konverter senter-byte fra CAN til paddle-topp
static inline int paddleCenterToTop(uint8_t center) 
{
  return clamp((int)center - PADDLE_HEIGHT / 2, PLAY_TOP, PLAY_BOT - PADDLE_HEIGHT);
}

// ============================================================================
// TEGNE-FUNKSJONER
// ============================================================================

// Tegn statisk ramme rundt spillefeltet
static void drawStaticFrame() 
{
  display.clearDisplay();
  display.drawRect(0, 0, SCREEN_WIDTH, SCREEN_HEIGHT, SSD1306_WHITE);
  display.drawRect(BORDER, BORDER, SCREEN_WIDTH - 2 * BORDER, SCREEN_HEIGHT - 2 * BORDER, SSD1306_WHITE);
  display.display();
}

// Tegn spillefeltet med alt innhold
static void render() 
{
  // Tøm spillefeltet (med margin for ballradius)
  int clearX = max(PLAY_LEFT - BALL_RADIUS, 1);
  int clearY = max(PLAY_TOP - BALL_RADIUS, 1);
  int clearW = min(PLAY_RIGHT + BALL_RADIUS, SCREEN_WIDTH - 2) - clearX + 1;
  int clearH = min(PLAY_BOT + BALL_RADIUS, SCREEN_HEIGHT - 2) - clearY + 1;
  display.fillRect(clearX, clearY, clearW, clearH, SSD1306_BLACK);

  // Tegn midtstrek
  for (int y = PLAY_TOP; y <= PLAY_BOT; y += 4)
  {
    display.drawFastVLine(SCREEN_WIDTH / 2, y, 2, SSD1306_WHITE);
  }

  // Tegn poengstilling
  display.setTextSize(1);
  display.setTextColor(SSD1306_WHITE);

  // Venstre score (motstander)
  display.setCursor(PLAY_LEFT + 2, BORDER + 1);
  display.print(scoreLeft);

  // Høyre score (deg) - juster for tosifrede tall
  int scoreXPosition = (scoreRight >= 10) ? (PLAY_RIGHT - 12) : (PLAY_RIGHT - 6);
  display.setCursor(scoreXPosition, BORDER + 1);
  display.print(scoreRight);

  // Tegn paddler
  display.fillRect(PADDLE_X_OPPONENT, opponentPaddleY, PADDLE_WIDTH, PADDLE_HEIGHT, SSD1306_WHITE);
  display.fillRect(PADDLE_X_OWN, ownPaddleY, PADDLE_WIDTH, PADDLE_HEIGHT, SSD1306_WHITE);

  // Tegn ball
  if (ballVisible)
  {
    display.fillCircle(ballX, ballY, BALL_RADIUS, SSD1306_WHITE);
  }

  display.display();
}

// ============================================================================
// CAN MELDINGSHÅNDTERING
// ============================================================================

// Callback for mottatte CAN-meldinger
static void onCanReceive(const CAN_message_t& message) 
{
  // Motta paddle-posisjon fra motstander
  if (message.id == PADDLE_ID && message.len >= 1) 
  {
    opponentPaddleY = paddleCenterToTop(message.buf[0]);
  } 
  
  // Motta ball-posisjon fra master
  else if (message.id == BALL_ID && message.len >= 2) 
  {
    // Speil X-koordinat for motstanders koordinatsystem
    uint8_t receivedX = message.buf[0];
    uint8_t receivedY = message.buf[1];
    
    ballX = (SCREEN_WIDTH - 1) - receivedX;
    ballY = receivedY;
    ballVisible = true;
  } 
  
  // Motta poengstilling
  else if (message.id == SCORE_ID && message.len >= 2) 
  {
    scoreLeft = message.buf[0];
    scoreRight = message.buf[1];
  }
}

// ============================================================================
// SETUP
// ============================================================================
void setup() 
{  
  // Initialiser seriell kommunikasjon
  // ---------------------------------------------------------------------------
  // NOTE: initialization (pins, Serial, display, CAN) should be here. 
  // Keep `setup()` focused on hardware init. The per-frame helpers are
  // defined at file scope (moved below) to avoid nested function definitions.
  pinMode(carrier::pin::joyClick, INPUT_PULLUP);
  pinMode(carrier::pin::joyUp, INPUT_PULLUP);
  pinMode(carrier::pin::joyDown, INPUT_PULLUP);

  Serial.begin(115200);

  // Initialize display (Adafruit_SSD1306)
  if (!display.begin(SSD1306_SWITCHCAPVCC)) {
    // If display init fails, hang here to make the failure obvious.
    for (;;) ;
  }
  drawStaticFrame();

  // Initialize CAN
  can0.begin();
  can0.setBaudRate(500000);
}

// ---------------------------------------------------------------------------
// Kompakt hovedløkke og hjelpere (file-scope functions)
// ---------------------------------------------------------------------------
static void processCanEvents() 
{
  // Poll and dispatch any received CAN messages.
  CAN_message_t msg;
  while (can0.read(msg)) {
    onCanReceive(msg);
  }
}

static void handleMasterSelection() 
{
  static bool masterLocked = false;
  if (!masterLocked && digitalRead(carrier::pin::joyClick) == LOW) 
  {
    isMaster = true;
    masterLocked = true;

    // Serve fra høyre
    ballX = PADDLE_X_OWN - 4;
    ballY = ownPaddleY + PADDLE_HEIGHT / 2;
    ballVelocityX = BALL_VELOCITY_X_INIT; 
    ballVelocityY = BALL_VELOCITY_Y_INIT;
    ballVisible = true;

    Serial.println(F("*** DU ER MASTER ***"));
    delay(150); // midlertidig debounce (kan erstattes med ikke-blokkerende debounce)
  }
}

static void handlePaddleInput() 
{
  static uint32_t lastPaddleMove = 0;
  const bool joyUp = (digitalRead(carrier::pin::joyUp) == LOW);
  const bool joyDown = (digitalRead(carrier::pin::joyDown) == LOW);

  if (millis() - lastPaddleMove >= PADDLE_MOVE_INTERVAL_MS) {
    lastPaddleMove = millis();
    if (joyUp && !joyDown) ownPaddleY -= 1;
    if (joyDown && !joyUp) ownPaddleY += 1;
    ownPaddleY = clamp(ownPaddleY, PLAY_TOP, PLAY_BOT - PADDLE_HEIGHT);
  }
}

static void sendPaddleIfDue() 
{
  static uint32_t lastPaddleSend = 0;
  if (millis() - lastPaddleSend >= PADDLE_SEND_INTERVAL_MS) 
  {
    lastPaddleSend = millis();
    CAN_message_t paddleMsg{};
    paddleMsg.id = PADDLE_ID;
    paddleMsg.len = 1;
    paddleMsg.buf[0] = paddleTopToCenter(ownPaddleY);
    can0.write(paddleMsg);
  }
}

static void updateBallIfMaster() 
{
  static uint32_t lastBallUpdate = 0;
  if (!isMaster) return;

  if (millis() - lastBallUpdate >= BALL_UPDATE_INTERVAL_MS) {
    lastBallUpdate = millis();

    // Oppdater ball-posisjon
    ballX += ballVelocityX;
    ballY += ballVelocityY;

    // Sprett i topp og bunn
    if (ballY <= PLAY_TOP + BALL_RADIUS) {
      ballY = PLAY_TOP + BALL_RADIUS;
      ballVelocityY = -ballVelocityY;
    }
    if (ballY >= PLAY_BOT - BALL_RADIUS) {
      ballY = PLAY_BOT - BALL_RADIUS;
      ballVelocityY = -ballVelocityY;
    }

    // Kollisjon mot venstre paddle (motstander)
    if (ballX - BALL_RADIUS <= PADDLE_X_OPPONENT + PADDLE_WIDTH &&
        ballY >= opponentPaddleY && ballY <= opponentPaddleY + PADDLE_HEIGHT) {
      ballX = PADDLE_X_OPPONENT + PADDLE_WIDTH + BALL_RADIUS;
      ballVelocityX = -ballVelocityX;
    }

    // Kollisjon mot høyre paddle (deg)
    if (ballX + BALL_RADIUS >= PADDLE_X_OWN &&
        ballY >= ownPaddleY && ballY <= ownPaddleY + PADDLE_HEIGHT) {
      ballX = PADDLE_X_OWN - BALL_RADIUS;
      ballVelocityX = -ballVelocityX;
    }

    // Scoring
    bool scored = false;
    if (ballX < PLAY_LEFT + BALL_RADIUS) { scoreRight++; scored = true; }
    if (ballX > PLAY_RIGHT - BALL_RADIUS) { scoreLeft++;  scored = true; }

    if (scored) {
      CAN_message_t scoreMsg{};
      scoreMsg.id = SCORE_ID; scoreMsg.len = 2;
      scoreMsg.buf[0] = scoreLeft; scoreMsg.buf[1] = scoreRight;
      can0.write(scoreMsg);

      Serial.print(F("Poeng: ")); Serial.print(scoreLeft);
      Serial.print(F(" - ")); Serial.println(scoreRight);

      // Ny serve
      ballX = PADDLE_X_OWN - 4;
      ballY = ownPaddleY + PADDLE_HEIGHT / 2;
      ballVelocityX = BALL_VELOCITY_X_INIT;
      ballVelocityY = (ballVelocityY >= 0) ? BALL_VELOCITY_Y_INIT : -BALL_VELOCITY_Y_INIT;
      ballVisible = true;
    } else {
      ballX = clamp(ballX, PLAY_LEFT + BALL_RADIUS, PLAY_RIGHT - BALL_RADIUS);
    }

    // Send ball-posisjon
    CAN_message_t ballMsg{};
    ballMsg.id = BALL_ID; ballMsg.len = 2;
    ballMsg.buf[0] = (uint8_t)clamp(ballX, 0, 127);
    ballMsg.buf[1] = (uint8_t)clamp(ballY, 0, 63);
    can0.write(ballMsg);
  }
}

static void drawIfDue() {
  static uint32_t lastDraw = 0;
  if (millis() - lastDraw >= DRAW_INTERVAL_MS) {
    lastDraw = millis();
    render();
  }
}

void loop()
 {
  processCanEvents();
  handleMasterSelection();
  handlePaddleInput();
  sendPaddleIfDue();
  updateBallIfMaster();
  drawIfDue();
}