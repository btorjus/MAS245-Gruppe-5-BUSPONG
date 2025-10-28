#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>
#include <Arduino.h>
#include <FlexCAN_T4.h>
#include <SPI.h>
#include <Wire.h>

// ============================================================================
// Pin definitions
// ============================================================================
namespace pin
{
  constexpr uint8_t joyUp{22};
  constexpr uint8_t joyDown{23};
  constexpr uint8_t joyClick{19};

  constexpr uint8_t oledDcPower{6};
  constexpr uint8_t oledCs{10};
  constexpr uint8_t oledReset{5};
}

// ============================================================================
// Display and CAN configuration
// ============================================================================
constexpr uint8_t SCREEN_WIDTH = 128;
constexpr uint8_t SCREEN_HEIGHT = 64;
constexpr uint8_t GROUP = 5; // <--- Set your group number here
constexpr uint8_t NODE_ID = 1; // <--- IMPORTANT: Set to 1 on first Teensy, 2 on second Teensy

constexpr uint16_t PADDLE_ID = GROUP + 20;
constexpr uint16_t BALL_ID = GROUP + 50;

FlexCAN_T4<CAN0, RX_SIZE_256, TX_SIZE_16> can0;
Adafruit_SSD1306 display(SCREEN_WIDTH, SCREEN_HEIGHT, &SPI, 
                         pin::oledDcPower, pin::oledReset, pin::oledCs);

// ============================================================================
// Game state
// ============================================================================
constexpr int PADDLE_WIDTH = 2;
constexpr int PADDLE_HEIGHT = 20;
constexpr int PADDLE_MARGIN = 4;

int ownPaddleY = (SCREEN_HEIGHT - PADDLE_HEIGHT) / 2;
int opponentPaddleY = (SCREEN_HEIGHT - PADDLE_HEIGHT) / 2;

int ballX = -1;
int ballY = -1;
bool ballActive = false;

bool isMaster = false;
bool masterAssigned = false;
bool gameActive = false;

// Ball physics (master only)
float masterBallX = 0;
float masterBallY = 0;
float masterBallVX = 0;
float masterBallVY = 0;

int scoreOwn = 0;
int scoreOpponent = 0;

// Timing
uint32_t lastPaddleSend = 0;
uint32_t lastBallSend = 0;
uint32_t lastGameUpdate = 0;

// ============================================================================
// Function declarations
// ============================================================================
void processCAN();
void sendPaddle();
void sendBall();
void updateGame();
void drawGame();
void handleInput();
void resetBall(bool towardsOpponent);

// ============================================================================
// Setup
// ============================================================================
void setup()
{
  Serial.begin(9600);
  unsigned long serialWaitStart = millis();
  while (!Serial && (millis() - serialWaitStart) < 2000) {}
  
  Serial.println(F("\n=== CAN Pong - Group 5 ==="));
  Serial.print(F("Node ID: "));
  Serial.println(NODE_ID);

  // Initialize CAN
  can0.begin();
  can0.setBaudRate(500000);
  Serial.println(F("CAN initialized at 500kbps"));

  // Initialize display
  if (!display.begin(SSD1306_SWITCHCAPVCC))
  {
    Serial.println(F("ERROR: Display init failed!"));
  }

  display.clearDisplay();
  display.setTextSize(1);
  display.setTextColor(SSD1306_WHITE);
  display.setCursor(20, 20);
  display.println(F("CAN PONG"));
  display.setCursor(10, 35);
  display.print(F("Node ID: "));
  display.println(NODE_ID);
  display.setCursor(5, 50);
  display.println(F("Press joystick"));
  display.display();

  // Initialize joystick pins
  pinMode(pin::joyUp, INPUT_PULLUP);
  pinMode(pin::joyDown, INPUT_PULLUP);
  pinMode(pin::joyClick, INPUT_PULLUP);

  Serial.println(F("Ready! Press joystick to start."));
}

// ============================================================================
// Main loop
// ============================================================================
void loop()
{
  processCAN();
  
  // Check for game start
  if (!gameActive && digitalRead(pin::joyClick) == LOW)
  {
    delay(50); // debounce
    if (digitalRead(pin::joyClick) == LOW)
    {
      gameActive = true;
      
      // First to press becomes master
      if (!masterAssigned)
      {
        masterAssigned = true;
        isMaster = true;
        resetBall(true);
        Serial.println(F("*** MASTER MODE ***"));
      }
      
      Serial.println(F("Game started!"));
      
      // Wait for button release
      while (digitalRead(pin::joyClick) == LOW)
      {
        delay(10);
      }
    }
  }

  if (gameActive)
  {
    handleInput();
    
    // Send paddle position at 50 Hz
    uint32_t now = millis();
    if (now - lastPaddleSend >= 20)
    {
      lastPaddleSend = now;
      sendPaddle();
    }

    // Master updates and sends ball at 100 Hz
    if (isMaster && (now - lastBallSend >= 10))
    {
      lastBallSend = now;
      updateGame();
      sendBall();
    }

    // Draw at ~30 FPS
    if (now - lastGameUpdate >= 33)
    {
      lastGameUpdate = now;
      drawGame();
    }
  }
}

// ============================================================================
// CAN message processing
// ============================================================================
void processCAN()
{
  CAN_message_t msg;
  while (can0.read(msg))
  {
    // Paddle position message
    if (msg.id == PADDLE_ID && msg.len >= 2)
    {
      uint8_t senderId = msg.buf[0];
      if (senderId == NODE_ID) continue; // Ignore own messages
      
      uint8_t paddleY = msg.buf[1];
      // Mirror Y coordinate for opponent
      opponentPaddleY = SCREEN_HEIGHT - 1 - paddleY;
      // Clamp to valid paddle position
      if (opponentPaddleY < 0) opponentPaddleY = 0;
      if (opponentPaddleY > SCREEN_HEIGHT - PADDLE_HEIGHT)
        opponentPaddleY = SCREEN_HEIGHT - PADDLE_HEIGHT;
    }
    // Ball position message
    else if (msg.id == BALL_ID && msg.len >= 5)
    {
      uint8_t senderId = msg.buf[0];
      if (senderId == NODE_ID) continue; // Ignore own messages
      
      uint8_t bx = msg.buf[1];
      uint8_t by = msg.buf[2];
      
      // Mirror X and Y for opponent's perspective
      ballX = SCREEN_WIDTH - 1 - bx;
      ballY = SCREEN_HEIGHT - 1 - by;
      ballActive = true;
    }
  }
}

// ============================================================================
// Send paddle position
// ============================================================================
void sendPaddle()
{
  CAN_message_t msg;
  msg.id = PADDLE_ID;
  msg.len = 2;
  msg.buf[0] = NODE_ID;
  msg.buf[1] = static_cast<uint8_t>(ownPaddleY);
  
  if (can0.write(msg) < 0)
  {
    Serial.println(F("Failed to send paddle"));
  }
}

// ============================================================================
// Send ball position (master only)
// ============================================================================
void sendBall()
{
  CAN_message_t msg;
  msg.id = BALL_ID;
  msg.len = 5;
  
  int bx = (int)masterBallX;
  int by = (int)masterBallY;
  
  // Clamp to screen bounds
  if (bx < 0) bx = 0;
  if (bx >= SCREEN_WIDTH) bx = SCREEN_WIDTH - 1;
  if (by < 0) by = 0;
  if (by >= SCREEN_HEIGHT) by = SCREEN_HEIGHT - 1;
  
  msg.buf[0] = NODE_ID;
  msg.buf[1] = static_cast<uint8_t>(bx);
  msg.buf[2] = static_cast<uint8_t>(by);
  msg.buf[3] = static_cast<uint8_t>(static_cast<int8_t>(masterBallVX));
  msg.buf[4] = static_cast<uint8_t>(static_cast<int8_t>(masterBallVY));
  
  if (can0.write(msg) < 0)
  {
    Serial.println(F("Failed to send ball"));
  }
}

// ============================================================================
// Handle joystick input
// ============================================================================
void handleInput()
{
  if (digitalRead(pin::joyUp) == LOW)
  {
    ownPaddleY -= 2;
    if (ownPaddleY < 0)
      ownPaddleY = 0;
  }
  
  if (digitalRead(pin::joyDown) == LOW)
  {
    ownPaddleY += 2;
    if (ownPaddleY > SCREEN_HEIGHT - PADDLE_HEIGHT)
      ownPaddleY = SCREEN_HEIGHT - PADDLE_HEIGHT;
  }
}

// ============================================================================
// Update game physics (master only)
// ============================================================================
void updateGame()
{
  if (!isMaster) return;

  // Update ball position
  masterBallX += masterBallVX;
  masterBallY += masterBallVY;

  // Bounce off top/bottom
  if (masterBallY <= 0)
  {
    masterBallY = 0;
    masterBallVY = -masterBallVY;
  }
  if (masterBallY >= SCREEN_HEIGHT - 2)
  {
    masterBallY = SCREEN_HEIGHT - 2;
    masterBallVY = -masterBallVY;
  }

  // Left paddle collision (opponent's paddle from master's view)
  if (masterBallX <= PADDLE_MARGIN + PADDLE_WIDTH)
  {
    int opponentY = SCREEN_HEIGHT - 1 - opponentPaddleY; // mirror back
    if (opponentY < 0) opponentY = 0;
    if (opponentY > SCREEN_HEIGHT - PADDLE_HEIGHT) opponentY = SCREEN_HEIGHT - PADDLE_HEIGHT;
    
    if (masterBallY + 2 >= opponentY && masterBallY <= opponentY + PADDLE_HEIGHT)
    {
      masterBallX = PADDLE_MARGIN + PADDLE_WIDTH;
      masterBallVX = -masterBallVX;
      // Add spin based on hit position
      float relativeHit = (masterBallY - opponentY - PADDLE_HEIGHT / 2.0) / (PADDLE_HEIGHT / 2.0);
      masterBallVY += relativeHit * 0.5;
    }
  }

  // Right paddle collision (own paddle from master's view)
  if (masterBallX >= SCREEN_WIDTH - PADDLE_MARGIN - PADDLE_WIDTH - 2)
  {
    int ownY = SCREEN_HEIGHT - 1 - ownPaddleY; // mirror back
    if (ownY < 0) ownY = 0;
    if (ownY > SCREEN_HEIGHT - PADDLE_HEIGHT) ownY = SCREEN_HEIGHT - PADDLE_HEIGHT;
    
    if (masterBallY + 2 >= ownY && masterBallY <= ownY + PADDLE_HEIGHT)
    {
      masterBallX = SCREEN_WIDTH - PADDLE_MARGIN - PADDLE_WIDTH - 2;
      masterBallVX = -masterBallVX;
      float relativeHit = (masterBallY - ownY - PADDLE_HEIGHT / 2.0) / (PADDLE_HEIGHT / 2.0);
      masterBallVY += relativeHit * 0.5;
    }
  }

  // Score detection
  if (masterBallX < 0)
  {
    scoreOwn++;
    Serial.print(F("Score: "));
    Serial.print(scoreOpponent);
    Serial.print(F(" - "));
    Serial.println(scoreOwn);
    resetBall(true);
  }
  else if (masterBallX >= SCREEN_WIDTH)
  {
    scoreOpponent++;
    Serial.print(F("Score: "));
    Serial.print(scoreOpponent);
    Serial.print(F(" - "));
    Serial.println(scoreOwn);
    resetBall(false);
  }
}

// ============================================================================
// Reset ball (master only)
// ============================================================================
void resetBall(bool towardsOpponent)
{
  masterBallX = SCREEN_WIDTH / 2.0;
  masterBallY = SCREEN_HEIGHT / 2.0;
  masterBallVX = towardsOpponent ? -1.5 : 1.5;
  masterBallVY = 0.5;
}

// ============================================================================
// Draw game
// ============================================================================
void drawGame()
{
  display.clearDisplay();

  // Draw center line
  for (int y = 0; y < SCREEN_HEIGHT; y += 4)
  {
    display.drawFastVLine(SCREEN_WIDTH / 2, y, 2, SSD1306_WHITE);
  }

  // Draw opponent paddle (left side)
  display.fillRect(PADDLE_MARGIN, opponentPaddleY, PADDLE_WIDTH, PADDLE_HEIGHT, SSD1306_WHITE);

  // Draw own paddle (right side)
  display.fillRect(SCREEN_WIDTH - PADDLE_MARGIN - PADDLE_WIDTH, ownPaddleY, 
                   PADDLE_WIDTH, PADDLE_HEIGHT, SSD1306_WHITE);

  // Draw ball
  if (isMaster)
  {
    display.fillRect((int)masterBallX, (int)masterBallY, 2, 2, SSD1306_WHITE);
  }
  else if (ballActive)
  {
    display.fillRect(ballX, ballY, 2, 2, SSD1306_WHITE);
  }

  // Draw score
  display.setTextSize(1);
  display.setCursor(SCREEN_WIDTH / 2 - 20, 2);
  display.print(scoreOpponent);
  display.setCursor(SCREEN_WIDTH / 2 + 12, 2);
  display.print(scoreOwn);

  display.display();
}

