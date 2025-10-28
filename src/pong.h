// Simple Pong implementation for 128x64 OLED
#ifndef PONG_H
#define PONG_H

#include <Arduino.h>
#include <Adafruit_SSD1306.h>

class Pong
{
public:
    Pong(Adafruit_SSD1306 *disp, uint8_t upPin, uint8_t downPin, uint8_t clickPin);
    void begin();
    void update();
    void draw();
    void toggleActive();
    bool isActive() const { return active_; } // Her er funksjonen implemetert i header, siden den er så liten. de over er deklarert.

    // Multiplayer helpers
    void setOpponentY(int y); // set left paddle Y from network
    int getOwnY() const;      // get right paddle Y to send over CAN
    void setRemoteBall(int x, int y); // set ball position received from network
    void setRemoteBallActive(bool active);
    void setMultiplayer(bool m);

private:
    Adafruit_SSD1306 *disp_;
    uint8_t upPin_, downPin_, clickPin_;
    bool active_ = false;

    // game state
    int leftY;
    int rightY;
    const int paddleW = 2;
    const int paddleH = 10;
    const int paddleMargin = 4;

    float ballX, ballY;
    float ballVX, ballVY;
    const int ballSize = 2;

    int scoreL, scoreR;

    unsigned long lastClickRead = 0;
    bool lastClickState = HIGH;
    void resetRound(bool toRight = true);
    void handleInput();
    // remote-drawn ball (when this device is not master)
    int remoteBallX = -1;
    int remoteBallY = -1;
    bool remoteBallActive = false;
    // multiplayer mode: when true, right paddle is controlled locally (joystick) and AI is disabled
    bool multiplayerMode = false;
};

#endif // PONG_H
