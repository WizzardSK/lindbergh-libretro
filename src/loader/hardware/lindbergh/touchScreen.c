#include <SDL3/SDL_events.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>

#include "../../graphics/customCursor.h"
#include "../../config/config.h"
#include "touchScreen.h"

extern int gGrp;

char phSendBuf[10];
bool phBufferFilled = false;
int phCommand = 1;
int phCommandByte = 0;
uint16_t coordX;
uint16_t coordY;
extern int phX, phY, phW, phH;
extern int phX2, phY2, phW2, phH2;
extern int phYWin, phY2Win;
extern void *customCursor;
extern void *touchCursor;
extern bool phShowCursorInGame;

#define TOUCH_START 0x81
#define TOUCH_TOUCHING 0x82
#define TOUCH_END 0x84

#define ELO_PACKET_SIZE 10
uint8_t eloResponseBuf[ELO_PACKET_SIZE] = {0};
bool eloMousePressed = false;
extern int eloMouseX;
extern int eloMouseY;
bool eloResponseReady = false;
bool eloTouchedInsideScreen = false;

void phCoordinates(int x, int y, int w, int h, int button)
{
    coordX = (float)x / ((float)w * 0.0002441406);
    coordY = (float)y / ((float)h * 0.0002441406);
    memset(phSendBuf, 0, sizeof(phSendBuf));

    if (button == SDL_EVENT_MOUSE_BUTTON_DOWN)
    {
        phSendBuf[2] = TOUCH_START;
        phCommand = 1;
    }
    else if (button == SDL_EVENT_MOUSE_BUTTON_UP)
    {
        phSendBuf[2] = TOUCH_END;
        phCommand = 3;
    }
    else if (button == SDL_EVENT_MOUSE_MOTION)
    {
        phSendBuf[2] = TOUCH_TOUCHING;
        phCommand = 2;
    }
    phSendBuf[0] = 0x55;
    phSendBuf[1] = 0x54;
    phSendBuf[3] = (char)(coordX & 0xFF);
    phSendBuf[4] = (char)((coordX >> 8) & 0xff);
    phSendBuf[5] = (char)(coordY & 0xFF);
    phSendBuf[6] = (char)((coordY >> 8) & 0xff);

    phBufferFilled = true;
}

uint8_t phCalcChecksum()
{
    int8_t checksum = -0x56;
    for (int x = 0; x < 9; x++)
    {
        checksum += phSendBuf[x];
    }
    return (uint8_t)(checksum & 0xff);
}

ssize_t phRead(int fd, void *buf, size_t count)
{
    if (!phBufferFilled)
        return 0;

    if (phCommandByte == 9)
    {
        switch (phCommand)
        {
            case 1:
                phCommand = 2;
                break;
            case 2:
                phSendBuf[2] = TOUCH_TOUCHING;
                break;
            case 3:
                phBufferFilled = false;
        }
        phSendBuf[9] = phCalcChecksum();
    }
    memcpy(buf, &phSendBuf[phCommandByte], sizeof(char));
    phCommandByte++;
    if (phCommandByte == 10)
        phCommandByte = 0;
    return 1;
}

void phTouchScreenCursor(int mX, int mY, int *motX, int *motY)
{
    int dummyX, dummyY;

    if (getConfig()->emulateTouchscreen)
    {
        if (phIsInsideTouchScreen(mX, mY, &dummyX, &dummyY) && getConfig()->hideCursor == 0)
        {
            setCursor(touchCursor);
            showPhCursor();
        }
        else if (getConfig()->hideCursor == 1 || phShowCursorInGame == false)
        {
            hideCursor();
        }
        else if (phShowCursorInGame && getConfig()->hideCursor == 0)
        {
            setCursor(customCursor);
            showCursor();
        }
    }
    if (mX < phX)
        *motX = phX;
    if (mX > (phW + phX))
        *motX = (phW + phX) - 1;
    if (mY > phH + phYWin)
        *motY = (phH + phYWin) - 1;
    if (mY < phYWin)
        *motY = phYWin;
}

bool phIsInsideTouchScreen(int mX, int mY, int *x, int *y)
{
    /* Mode 1 has no touch screen, and the screens are unset until the game is patched. */
    if (phW2 <= 0 || phH2 <= 0)
        return false;

    if (mX >= phX2 && mX <= (phX2 + phW2) && mY >= phY2Win && mY <= (phY2Win + phH2))
    {
        *x = mX - phX2;
        *y = mY - phY2Win;
        return true;
    }
    return false;
}

void phTouchClick(int x, int y, int type)
{
    if (type == SDL_EVENT_MOUSE_BUTTON_DOWN)
        phCoordinates(x, y, phW2, phH2, SDL_EVENT_MOUSE_BUTTON_DOWN);
    else if (type == SDL_EVENT_MOUSE_BUTTON_UP)
        phCoordinates(x, y, phW2, phH2, SDL_EVENT_MOUSE_BUTTON_UP);
    else if (type == SDL_EVENT_MOUSE_MOTION)
        phCoordinates(x, y, phW2, phH2, SDL_EVENT_MOUSE_MOTION);
}

void handleEloTouch(const SDL_Event *e, int drawableW, int drawableH)
{

    eloMousePressed = 0;
    eloMouseX = e->button.x;
    eloMouseY = drawableH - e->button.y;

    if (e->type == SDL_EVENT_MOUSE_BUTTON_DOWN)
        eloMousePressed = 1;

    float winAspect = (float)drawableW / drawableH;
    float gameAspect = 1024.0f / 768.0f;

    int viewportX, viewportY, viewportW, viewportH;

    if (winAspect > gameAspect)
    {
        viewportH = drawableH;
        viewportW = (int)(drawableH * gameAspect);
        viewportX = (drawableW - viewportW) / 2;
        viewportY = 0;
    }
    else
    {
        viewportW = drawableW;
        viewportH = (int)(drawableW / gameAspect);
        viewportX = 0;
        viewportY = (drawableH - viewportH) / 2;
    }

    if (eloMouseX < viewportX || eloMouseX >= viewportX + viewportW || eloMouseY < viewportY || eloMouseY >= viewportY + viewportH)
    {
        eloTouchedInsideScreen = false;
        return;
    }
    else
    {
        eloTouchedInsideScreen = true;
        float normalizedX = (eloMouseX - viewportX) / (float)viewportW;
        float normalizedY = (eloMouseY - viewportY) / (float)viewportH;

        // if (gGrp == GROUP_VF5)
        // {
        //     eloMouseX = normalizedX * 640.0f;
        //     eloMouseY = normalizedY * 480.0f;
        // }
        // else
        // {
            eloMouseX = normalizedX * 1024.0f;
            eloMouseY = normalizedY * 768.0f;
        // }
    }
    eloBuildResponsePacket(0x54, eloMouseX, eloMouseY, eloMousePressed);
}

uint8_t eloCalcChecksum(uint8_t *packet)
{
    uint8_t sum = 0;
    for (int i = 0; i < ELO_PACKET_SIZE - 1; ++i)
        sum += packet[i];
    return (sum + 0xAA) & 0xFF;
}

void eloBuildResponsePacket(uint8_t cmd, int pckA, int pckB, int pckC)
{
    uint8_t *packet = eloResponseBuf;

    packet[0] = 0x55;
    packet[1] = cmd;

    if (cmd == 0x54)
    {
        packet[2] = pckC; // & 0xFF;
        packet[3] = pckA & 0xFF;
        packet[4] = (pckA >> 8) & 0xFF;
        packet[5] = pckB & 0xFF;
        packet[6] = (pckB >> 8) & 0xFF;
    }
    else
    {
        packet[2] = pckA & 0xFF;
        packet[3] = pckB & 0xFF;
        packet[4] = pckC & 0xFF;
        packet[5] = 0x00;
        packet[6] = 0x00;
    }
    packet[7] = 0x50; // Pressure ?
    packet[8] = 0x00;
    packet[9] = eloCalcChecksum(packet);

    eloResponseReady = true;
}

int eloWriteTouchPacket(const void *buf, size_t count)
{
    static uint8_t packetBuf[10];
    static int packetPos = 0;
    bool degugMsg = getConfig()->showDebugMessages;

    for (size_t i = 0; i < count; ++i)
    {
        uint8_t b = ((uint8_t *)buf)[i];

        if (packetPos == 0 && b != 0x55)
            continue;

        packetBuf[packetPos++] = b;

        if (packetPos == ELO_PACKET_SIZE)
        {
            if (degugMsg)
            {
                printf("Serial1 FULL PACKET: ");
                for (int j = 0; j < ELO_PACKET_SIZE; ++j)
                    printf("%02X ", packetBuf[j]);
                printf("\n");
            }

            uint8_t cmd = packetBuf[1];

            switch (cmd)
            {
                case 0x52:
                    if (degugMsg)
                        printf("ELO: Init/handshake? (0x52)\n");
                    // mj4_send_response(0x52, 0xff, 0xff);
                    // start_streaming = 1;
                    break;
                case 0x50:
                    if (degugMsg)
                        printf("ELO: Config/setup (baud) (0x50)\n");
                    // mj4_send_response(0x50, 0x00, 0x00);
                    break;
                case 0x70:
                    if (degugMsg)
                        printf("ELO: Status check/reply? (0x70)\n");
                    eloBuildResponsePacket(0x50, 0x30, 0x06, 0x04);
                    break;
                case 0x42:
                    if (degugMsg)
                        printf("ELO: (0x42)\n");
                    eloBuildResponsePacket(0x41, 0x03, 0x01, 0x00);
                    break;
                case 0x62:
                    if (degugMsg)
                        printf("ELO: (0x62)\n");
                    eloBuildResponsePacket(0x42, 0x03, 0x01, 0x00);
                    break;
                case 0x6D:
                    if (degugMsg)
                        printf("ELO: (0x6D)\n");
                    eloBuildResponsePacket(0x4D, 0x00, 0x00, 0x00);
                    break;
                case 0x4D:
                    if (degugMsg)
                        printf("ELO: (0x4D)\n");
                    eloBuildResponsePacket(0x41, 0x03, 0x01, 0x00);
                    break;
                default:
                    if (degugMsg)
                        printf("ELO: Unknown command %02X\n", cmd);
                    eloResponseReady = false;
                    break;
            }
            packetPos = 0;
        }
    }
    return count;
}

int eloReadTouchPacket(uint8_t *buf, size_t count)
{
    if (eloResponseReady && count >= ELO_PACKET_SIZE)
    {
        memcpy(buf, eloResponseBuf, ELO_PACKET_SIZE);
        eloResponseReady = false;
        return ELO_PACKET_SIZE;
    }
    return 0;
}

void eloTouchHolding()
{
    if (eloMousePressed && eloTouchedInsideScreen)
        eloBuildResponsePacket(0x54, eloMouseX, eloMouseY, 2);
}