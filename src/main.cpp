#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>
#include <Arduino.h>
#include <FlexCAN_T4.h>
#include <SPI.h>
#include <Wire.h>
#include <string.h>
#include "pong.h"

namespace carrier
{
  namespace pin
  {
    constexpr uint8_t joyLeft{18};
    constexpr uint8_t joyRight{17};
    constexpr uint8_t joyClick{19};
    constexpr uint8_t joyUp{22};
    constexpr uint8_t joyDown{23};

    constexpr uint8_t oledDcPower{6};
    constexpr uint8_t oledCs{10};
    constexpr uint8_t oledReset{5};
  }

  namespace oled
  {
    constexpr uint8_t screenWidth{128}; // OLED display width in pixels
    constexpr uint8_t screenHeight{64}; // OLED display height in pixels
  }
}

namespace
{
  // Project CAN ID configuration (change GROUP once per team)
  constexpr uint8_t GROUP = 5; // <--- set your group number here
  constexpr uint16_t PADDLE_ID = static_cast<uint16_t>(GROUP + 20);
  constexpr uint16_t BALL_ID   = static_cast<uint16_t>(GROUP + 50);

  FlexCAN_T4<CAN0, RX_SIZE_256, TX_SIZE_16> can0;
  // FlexCAN_T4<CAN1, RX_SIZE_256, TX_SIZE_16> can1;

  Adafruit_SSD1306 display(carrier::oled::screenWidth,
                           carrier::oled::screenHeight,
                           &SPI,
                           carrier::pin::oledDcPower,
                           carrier::pin::oledReset,
                           carrier::pin::oledCs);
}

void canReceiveAndEcho();
// Pong game instance (created in setup)
Pong *pongGame = nullptr;

// Master flags and ball state for Part 3 (master sends ball at 100 Hz)
static bool isMaster = false;
static bool masterAssigned = false;
static int16_t netBallX = 0;
static int16_t netBallY = 0;
static int8_t netBallVX = 0;
static int8_t netBallVY = 0;
static uint32_t lastBallSend = 0;

void sendBall();

void setup()
{
  Serial.begin(9600);
  // Wait a short while for the USB serial to enumerate so messages are not lost
  unsigned long serialWaitStart = millis();
  while (!Serial && (millis() - serialWaitStart) < 2000)
  {
    ; // wait up to 2s for host to open virtual COM port
  }
  Serial.println();
  Serial.println(F("Booting Teensy project..."));
  can0.begin();
  can0.setBaudRate(500000);


  // Gen. display voltage from 3.3V (https://adafruit.github.io/Adafruit_SSD1306/html/_adafruit___s_s_d1306_8h.html#ad9d18b92ad68b542033c7e5ccbdcced0)
  if (!display.begin(SSD1306_SWITCHCAPVCC))
  {
    Serial.println(F("ERROR: display.begin(SSD1306_SWITCHCAPVCC) failed."));
    Serial.println(F("Continuing without OLED (for debugging)."));
    // Do not block here; continue so serial debug is available.
  }

  display.clearDisplay();
  display.display();

  Serial.print(F("float size in bytes: "));
  Serial.println(sizeof(float));

  // Initialize Pong object (do not start yet)
  pongGame = new Pong(&display, carrier::pin::joyUp, carrier::pin::joyDown, carrier::pin::joyClick);
  pongGame->begin();
  // enable multiplayer mode so local joystick controls the right paddle and AI is disabled
  // Bytt til false hvis du vil spille alene
  pongGame->setMultiplayer(true);
  // Helpful debug info for troubleshooting
  Serial.println(F("Pong initialized (waiting to start)."));
  Serial.println(F("CAN interface started at 500000 bps."));
}

void loop()
{
  // Non-blocking main loop: poll CAN frequently, show start screen until user starts Pong.
  static const uint32_t FRAME_MS = 33; // ~30 FPS
  static uint32_t lastFrame = 0;
  static bool startShown = false;

  // Always poll CAN so echo will work even before/after gameplay
  canReceiveAndEcho();

  // Periodically send own paddle position (50 Hz) so opponent can draw it
  if (pongGame)
  {
    static uint32_t lastPaddleSend = 0;
    uint32_t now = millis();
    const uint32_t PADDLE_MS = 20; // 50 Hz
    if ((now - lastPaddleSend) >= PADDLE_MS)
    {
      lastPaddleSend = now;
      int ownY = pongGame->getOwnY();
      // clamp and send as uint8_t (0..63)
      if (ownY < 0)
        ownY = 0;
      if (ownY > (int)display.height() - 1)
        ownY = display.height() - 1;
      // send paddle message
      CAN_message_t pm;
      pm.id = PADDLE_ID;
      pm.len = 1;
      pm.buf[0] = static_cast<uint8_t>(ownY);
      can0.write(pm);
    }
  }

    // Poll the click button to start the game (active LOW)
  if (digitalRead(carrier::pin::joyClick) == LOW)
  {
    // debounce
    delay(50);
    if (digitalRead(carrier::pin::joyClick) == LOW)
    {
      // Assign master to first presser after startup
      if (!masterAssigned)
      {
        masterAssigned = true;
        isMaster = true;
        // initialize network ball in center moving towards opponent
        netBallX = display.width() / 2;
        netBallY = display.height() / 2;
        netBallVX = -1; // one pixel per 10ms to the left
        netBallVY = 0;
        lastBallSend = millis();
        Serial.println(F("Master assigned: sending ball at 100Hz."));
        // disable drawing remote ball locally (we will simulate locally)
        if (pongGame)
          pongGame->setRemoteBallActive(false);
        // immediately send first ball state
        sendBall();
      }

      pongGame->toggleActive();
      // clear screen to prepare for game drawing
      display.clearDisplay();
      display.display();
      startShown = false; // reset so screen will show again if toggled off
      // Wait for the user to release the click button. Without this, the
      // same physical press can still be read by Pong::handleInput() during
      // the first update and cause an immediate second toggle (double-toggle).
      while (digitalRead(carrier::pin::joyClick) == LOW)
      {
        delay(10);
      }
    }
  }

  // If this device is master, update and send ball at 100Hz
  if (isMaster)
  {
    uint32_t now = millis();
    if ((now - lastBallSend) >= 10)
    {
      lastBallSend = now;
      // Advance network ball
      netBallX += netBallVX;
      netBallY += netBallVY;
      // simple bounds and bounce
      if (netBallY < 0)
      {
        netBallY = 0;
        netBallVY = -netBallVY;
      }
      if (netBallY > (int)display.height() - 1)
      {
        netBallY = display.height() - 1;
        netBallVY = -netBallVY;
      }
      if (netBallX < 0)
      {
        netBallX = 0;
        netBallVX = -netBallVX;
      }
      if (netBallX > (int)display.width() - 1)
      {
        netBallX = display.width() - 1;
        netBallVX = -netBallVX;
      }

      // send current ball state
      sendBall();
    }
  }

  // If pong is active, update/draw at fixed frame rate
  if (pongGame && pongGame->isActive())
  {
    uint32_t now = millis();
    if ((now - lastFrame) >= FRAME_MS)
    {
      pongGame->update();
      pongGame->draw();
      lastFrame = now;
    }
    return;
  }

  // Show a simple start screen once
  if (!startShown)
  {
    display.clearDisplay();
    // Draw a rounded rectangle around the screen
    display.drawRoundRect(0, 0, display.width(), display.height(), 5, SSD1306_WHITE);
    display.setTextSize(1);
    display.setTextColor(SSD1306_WHITE);
    display.setCursor(10, 20);
    display.println(F("PONG - group 5"));
    display.setCursor(10, 36);
    display.println(F("Press joystick to"));
    display.setCursor(10, 48);
    display.println(F("start"));
    display.display();
    startShown = true;
  }


  // NOTE: paddle and master send logic was moved earlier in loop() so
  // it runs while the game is active.
}

// removed test sendCan helpers to keep project clean (use PADDLE_ID / BALL_ID senders)

// Read CAN frames and handle game messages (paddle + ball). Other messages are echoed for debug.
void canReceiveAndEcho()
{
  CAN_message_t rx;
  // Read all available messages
  while (can0.read(rx))
  {
    if (rx.id == PADDLE_ID && rx.len >= 1)
    {
      uint8_t remoteY = rx.buf[0];
      // mirror vertical coordinate because opponent may have opposite coord system
      int drawY = (int)display.height() - 1 - (int)remoteY;
      if (pongGame)
        pongGame->setOpponentY(drawY);
    }
    else if (rx.id == BALL_ID && rx.len >= 4)
    {
      uint8_t bx = rx.buf[0];
      uint8_t by = rx.buf[1];

      // Mirror X and Y to draw from this player's perspective
      int drawX = (int)display.width() - 1 - (int)bx;
      int drawY = (int)display.height() - 1 - (int)by;
      if (pongGame)
      {
        pongGame->setRemoteBall(drawX, drawY);
        pongGame->setRemoteBallActive(true);
      }
    }
    else
    {
      // default: print and echo for debugging
      Serial.print(F("Received CAN id=0x"));
      Serial.print(rx.id, HEX);
      Serial.print(F(" len="));
      Serial.print(rx.len);
      Serial.print(F(" data:"));
      for (uint8_t i = 0; i < rx.len; ++i)
      {
        Serial.print(F(" 0x"));
        if (rx.buf[i] < 0x10)
          Serial.print('0');
        Serial.print(rx.buf[i], HEX);
      }
      Serial.println();

      int32_t res = can0.write(rx);
      if (res < 0)
        Serial.println(F("Failed to echo CAN message."));
    }
  }
}

// Send ball (4 bytes: x, y, vx, vy) on BALL_ID
void sendBall()
{
  CAN_message_t m;
  m.id = BALL_ID;
  m.len = 4;
  // clamp values to 0..255 range
  int bx = netBallX;
  int by = netBallY;
  if (bx < 0)
    bx = 0;
  if (bx > 255)
    bx = 255;
  if (by < 0)
    by = 0;
  if (by > 255)
    by = 255;

  m.buf[0] = static_cast<uint8_t>(bx);
  m.buf[1] = static_cast<uint8_t>(by);
  m.buf[2] = static_cast<uint8_t>(static_cast<int8_t>(netBallVX));
  m.buf[3] = static_cast<uint8_t>(static_cast<int8_t>(netBallVY));

  if (can0.write(m) < 0)
  {
    Serial.println(F("Failed to send ball CAN message."));
  }
}

