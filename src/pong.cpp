#include "pong.h"
#include <Adafruit_SSD1306.h>

Pong::Pong(Adafruit_SSD1306 *disp, uint8_t upPin, uint8_t downPin, uint8_t clickPin)
    : disp_(disp), upPin_(upPin), downPin_(downPin), clickPin_(clickPin)
{
}

void Pong::begin()
{
    pinMode(upPin_, INPUT_PULLUP);
    pinMode(downPin_, INPUT_PULLUP);
    pinMode(clickPin_, INPUT_PULLUP);

    // initialise positions
    leftY = (disp_->height() - paddleH) / 2;
    rightY = leftY;
    scoreL = 0;
    scoreR = 0;
    resetRound(true);
}

void Pong::resetRound(bool toRight)
{
    ballX = disp_->width() / float{2.0};
    ballY = disp_->height() / float{2.0};
    float speed = float{1.6};          // pixels per frame at 30 FPS
    ballVX = toRight ? speed : -speed; // If toRight is true, ball moves right, otherwise left
    ballVY = float{0.6};
}

void Pong::handleInput()
{
    // simple polling of up/down (active LOW)
    if (digitalRead(upPin_) == LOW)
    {
        // move own paddle (right side) up
        rightY -= 2;
        if (rightY < 0)
        {
            rightY = 0;
        }
    }

    if (digitalRead(downPin_) == LOW)
    {
        // move own paddle (right side) down
        rightY += 2;
        if (rightY > disp_->height() - paddleH)
        {
            rightY = disp_->height() - paddleH;
        }
    }

    // click toggles active state (with simple debounce)
    bool clickState = digitalRead(clickPin_);
    unsigned long now = millis();
    if (clickState != lastClickState && (now - lastClickRead) > 200)
    {
        lastClickRead = now;
        lastClickState = clickState;
        if (clickState == LOW)
        {
            toggleActive();
        }
    }
}

void Pong::setOpponentY(int y)
{
    // clamp to display bounds
    if (y < 0)
        y = 0;
    if (y > disp_->height() - paddleH)
        y = disp_->height() - paddleH;
    leftY = y;
}

int Pong::getOwnY() const
{
    return rightY;
}

void Pong::setRemoteBall(int x, int y)
{
    remoteBallX = x;
    remoteBallY = y;
    remoteBallActive = true;
}

void Pong::setRemoteBallActive(bool active)
{
    remoteBallActive = active;
}

void Pong::setMultiplayer(bool m)
{
    multiplayerMode = m;
}

void Pong::update()
{
    if (!active_)
        return;

    handleInput();

    // update ball position
    ballX += ballVX;
    ballY += ballVY;

    // top/bottom collision
    if (ballY <= 0)
    {
        ballY = 0;
        ballVY = -ballVY;
    }

    if (ballY >= disp_->height() - ballSize)
    {
        ballY = disp_->height() - ballSize;
        ballVY = -ballVY;
    }

    // left paddle collision
    if (ballX <= paddleMargin + paddleW)
    {
        if (ballY + ballSize >= leftY && ballY <= leftY + paddleH) //&& er and logikk 
        {
            ballX = paddleMargin + paddleW + 1;
            ballVX = -ballVX;
            // tweak vertical speed by hit position
            float rel = (ballY + ballSize / float{2.0}) - (leftY + paddleH / float{2.0});
            ballVY += rel * float{0.05};
        }
    }

    // right paddle collision
    if (ballX + ballSize >= disp_->width() - paddleMargin - paddleW)
    {
        if (ballY + ballSize >= rightY && ballY <= rightY + paddleH)
        {
            ballX = disp_->width() - paddleMargin - paddleW - ballSize - 1;
            ballVX = -ballVX;
            float rel = (ballY + ballSize / float{2.0}) - (rightY + paddleH / float{2.0});
            ballVY += rel * float{0.05};
        }
    }

    // score checks
    if (ballX < 0)
    {
        scoreR++;
        resetRound(true);
    }

    if (ballX > disp_->width())
    {
        scoreL++;
        resetRound(false);
    }

    // Algorythm for right paddle: follow ball with limit — only when NOT in multiplayer mode
    if (!multiplayerMode)
    {
        float center = leftY + paddleH / float{2.0};
        float diff = ballY - center;
        float maxMove = float{1.2}; // speed per update
        if (diff > maxMove)
            diff = maxMove;
        if (diff < -maxMove)
            diff = -maxMove;
        leftY += (int)diff;
        if (leftY < 0)
            leftY = 0;
        if (leftY > disp_->height() - paddleH)
            leftY = disp_->height() - paddleH;
    }
}

void Pong::draw()
{
    if (!active_)
        return;

    disp_->clearDisplay();

    // center line
    for (int y = 0; y < disp_->height(); y += 4)
    {
        disp_->drawFastVLine(disp_->width() / 2, y, 2, SSD1306_WHITE);
    }

    // paddles
    disp_->fillRect(paddleMargin, leftY, paddleW, paddleH, SSD1306_WHITE);
    disp_->fillRect(disp_->width() - paddleMargin - paddleW, rightY, paddleW, paddleH, SSD1306_WHITE);

    // ball
    // If a remote ball is active (networked game where remote host controls ball), draw that
    if (remoteBallActive)
    {
        disp_->fillRect(remoteBallX, remoteBallY, ballSize, ballSize, SSD1306_WHITE);
    }
    else
    {
        disp_->fillRect((int)ballX, (int)ballY, ballSize, ballSize, SSD1306_WHITE);
    }

    // score
    disp_->setTextSize(1);
    disp_->setTextColor(SSD1306_WHITE);
    disp_->setCursor(disp_->width() / 2 - 20, 0);
    disp_->print(scoreL);
    disp_->setCursor(disp_->width() / 2 + 12, 0);
    disp_->print(scoreR);

    disp_->display();
}

void Pong::toggleActive()
{
    active_ = !active_;
    // when activated, clear screen and reset round
    if (active_)
    {
        scoreL = 0;
        scoreR = 0;
        resetRound(true);
        disp_->clearDisplay();
        disp_->display();
    }
}
