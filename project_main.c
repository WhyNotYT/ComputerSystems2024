
/* C libraries */
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <ti/drivers/PIN.h> // Include this if using TI drivers, adjust according to your platform

/* XDCtools files */
#include <xdc/runtime/System.h>
#include <xdc/std.h>
/* Additional includes for display */
#include <ti/mw/display/Display.h>

/* BIOS Header files */
#include "Board.h"
#include "utils.h"
#include <ti/drivers/I2C.h>
#include <ti/drivers/PIN.h>
#include <ti/drivers/Power.h>
#include <ti/drivers/SPI.h>
#include <ti/drivers/UART.h>
#include <ti/drivers/i2c/I2CCC26XX.h>
#include <ti/drivers/pin/PINCC26XX.h>
#include <ti/drivers/power/PowerCC26XX.h>
#include <ti/mw/display/Display.h>
#include <ti/mw/display/DisplayExt.h>
#include <ti/sysbios/BIOS.h>
#include <ti/sysbios/knl/Clock.h>
#include <ti/sysbios/knl/Task.h>

#include "sensors/mpu9250.h"
#include "sensors/opt3001.h"


#define PI 3.14
#define STACKSIZE 2048
Char sensorTaskStack[STACKSIZE];
Char displayTaskStack[STACKSIZE];

static PIN_Handle button0Handle;
static PIN_State button0State;

static PIN_Handle button1Handle;
static PIN_State button1State;

Display_Handle displayHandle;
Display_Params displayParams;

PIN_Config buttonConfig[] = {Board_BUTTON0 | PIN_INPUT_EN | PIN_PULLUP | PIN_IRQ_NEGEDGE, PIN_TERMINATE};

PIN_Config button1Config[] = {Board_BUTTON1 | PIN_INPUT_EN | PIN_PULLUP | PIN_IRQ_NEGEDGE, PIN_TERMINATE};

static PIN_Handle hMpuPin;
static PIN_State MpuPinState;
static PIN_Config MpuPinConfig[] = {Board_MPU_POWER | PIN_GPIO_OUTPUT_EN | PIN_GPIO_HIGH | PIN_PUSHPULL |
                                        PIN_DRVSTR_MAX,
                                    PIN_TERMINATE};

static const I2CCC26XX_I2CPinCfg i2cMPUCfg = {.pinSDA = Board_I2C0_SDA1, .pinSCL = Board_I2C0_SCL1};

#define SENSOR_HISTORY_SIZE 4

float acclx[SENSOR_HISTORY_SIZE] = {0};
float accly[SENSOR_HISTORY_SIZE] = {0};
float acclz[SENSOR_HISTORY_SIZE] = {0};

unsigned int timeCounter = 0;
unsigned int spaceDetectionTime = 0;

void updateSensorData(float ax, float ay, float az)
{
    int i;
    for (i = SENSOR_HISTORY_SIZE - 1; i > 0; i--)
    {
        acclx[i] = acclx[i - 1];
        accly[i] = accly[i - 1];
        acclz[i] = acclz[i - 1];
    }
    acclx[0] = ax;
    accly[0] = ay;
    acclz[0] = az;
}
#define DOT_THRESHOLD 0.7
#define DASH_THRESHOLD 0.6
#define DEAD_BAND 0.4
#define MAX_SAMPLES_TO_CHECK 30
#define EPSILON 0.05
#define MAX_MORSE_LENGTH 32
#define IGNORE_SAMPLES 150
#define RECIEVE_BUFFER_SIZE 128
typedef enum
{
    RESTING,
    DETECTING_DOT,
    DETECTING_DASH,
    DETECTING_SPACE
} GestureState;

GestureState currentState = RESTING;
int samplesChecked = 0;
char morseCode[MAX_MORSE_LENGTH + 1];
int morseIndex = 0;
int ignoreCount = 0;

#define BUZZER_PIN Board_BUZZER

#define DOT_BEEP_DURATION 200
#define DASH_BEEP_DURATION 400
#define SPACE_BEEP_DURATION 800

static PIN_Handle buzzerHandle;
static PIN_State buzzerState;

static PIN_Config buzzerConfig[] = {BUZZER_PIN | PIN_GPIO_OUTPUT_EN | PIN_GPIO_LOW | PIN_PUSHPULL | PIN_DRVSTR_MAX,
                                    PIN_TERMINATE};

Char uartTaskStack[STACKSIZE];
Char displayTaskStack[STACKSIZE];

static UART_Handle uartHandle;
static UART_Params uartParams;
double ambientLight = -1000.0;


char recievedMessageStr[RECIEVE_BUFFER_SIZE];

void initBuzzer()
{
    buzzerHandle = PIN_open(&buzzerState, buzzerConfig);
    if (!buzzerHandle)
    {
        System_abort("Error initializing buzzer pins\n");
    }
}
void sensorTaskFxn(UArg arg0, UArg arg1)
{
    float ax, ay, az, gx, gy, gz;
    double ambientLight = 0;

    I2C_Handle i2c;
    I2C_Params i2cParams;
    I2C_Params_init(&i2cParams);
    i2cParams.bitRate = I2C_400kHz;
    i2cParams.custom = (uintptr_t)&i2cMPUCfg;

    i2c = I2C_open(Board_I2C, &i2cParams);
    if (i2c == NULL)
    {
        System_abort("Error Initializing I2C\n");
    }

    PIN_setOutputValue(hMpuPin, Board_MPU_POWER, Board_MPU_POWER_ON);

    Task_sleep(100000 / Clock_tickPeriod);

    mpu9250_setup(&i2c);

    I2C_close(i2c);

    i2cParams.custom = NULL;

    i2c = I2C_open(Board_I2C0, &i2cParams);
    if (i2c == NULL)
    {
        System_abort("Error Initializing I2C\n");
    }

    opt3001_setup(&i2c);
    Task_sleep(100000 / Clock_tickPeriod);

    I2C_close(i2c);

    while (1)
    {
        if (spaceDetectionTime < timeCounter)
        {
            i2cParams.custom = NULL;
            i2c = I2C_open(Board_I2C0, &i2cParams);
            if (i2c != NULL)
            {
                ambientLight = opt3001_get_data(&i2c);

                if (ambientLight != -1)
                {

                    if (ambientLight < 0.9)
                    {
                        SpaceDetected();
                        spaceDetectionTime = timeCounter + (IGNORE_SAMPLES / 2);
                    }
                }
            }

            I2C_close(i2c);
        }

        Task_sleep(500 / Clock_tickPeriod);

        i2cParams.custom = (uintptr_t)&i2cMPUCfg;
        i2c = I2C_open(Board_I2C, &i2cParams);
        if (i2c != NULL)
        {
            mpu9250_get_data(&i2c, &ax, &ay, &az, &gx, &gy, &gz);
            updateSensorData(ax, ay, az);

            detectMorseCode();
            I2C_close(i2c);
        }

        timeCounter++;
        Task_sleep(500 / Clock_tickPeriod);
    }
}
static bool buttonPressed = false;

static bool uartConnected = false;

#define SYMBOL_PAUSE 10000
#define WORD_PAUSE 30000
Display_Handle hDisplayLcd;
void playMorseCode(const char *morse)
{
    if (morse == NULL)
        return;
    int i;
    for (i = 0; morse[i] != '\0'; i++)
    {
        switch (morse[i])
        {
        case '.':
            beep(DOT_BEEP_DURATION);
            Task_sleep(SYMBOL_PAUSE);
            break;
        case '-':
            beep(DASH_BEEP_DURATION);
            Task_sleep(SYMBOL_PAUSE);
            break;
        case ' ':
            Task_sleep(WORD_PAUSE);
            break;
        default:
            break;
        }
    }
}

Display_Handle displayHandle = NULL;
bool displayInitialized = false;

void closeUARTandInitDisplay()
{
    if (uartConnected && uartHandle != NULL)
    {
        UART_close(uartHandle);
        uartConnected = false;
    }

    if (!displayInitialized)
    {
        Display_Params displayParams;
        Display_Params_init(&displayParams);
        displayParams.lineClearMode = DISPLAY_CLEAR_BOTH;
        displayHandle = Display_open(Display_Type_LCD, &displayParams);
        Display_print0(displayHandle, 1, 1, "Welcome to morse code device!");
        displayInitialized = true;
    }
}

void closeDisplayAndReopenUART()
{
    if (displayInitialized && displayHandle != NULL)
    {
        Display_close(displayHandle);
        displayInitialized = false;
    }

    if (!uartConnected)
    {
        uartHandle = UART_open(Board_UART0, &uartParams);
        if (uartHandle != NULL)
        {
            uartConnected = true;
        }
    }
}

void displayMessage(const char *message)
{
    closeUARTandInitDisplay();

    if (displayHandle)
    {
        Display_clear(displayHandle);

        char decodedMessage[RECIEVE_BUFFER_SIZE] = {0};
        int decodedIndex = 0;

        char morseBuffer[32] = {0};
        int morseIndex = 0;

        int i;
        for (i = 0; message[i] != '\0'; i++)
        {
            if (message[i] == ' ' && morseIndex > 0)
            {
                morseBuffer[morseIndex] = '\0';

                char decodedChar = morseToAscii(morseBuffer);
                if (decodedChar != '\0')
                {
                    decodedMessage[decodedIndex++] = decodedChar;
                }

                memset(morseBuffer, 0, sizeof(morseBuffer));
                morseIndex = 0;

                while (message[i + 1] == ' ')
                    i++;
            }
            else if (message[i] == '.' || message[i] == '-')
            {
                morseBuffer[morseIndex++] = message[i];
            }
        }

        if (morseIndex > 0)
        {
            morseBuffer[morseIndex] = '\0';
            char decodedChar = morseToAscii(morseBuffer);
            if (decodedChar != '\0')
            {
                decodedMessage[decodedIndex++] = decodedChar;
            }
        }


        decodedMessage[decodedIndex] = '\0';


        Display_print0(displayHandle, 1, 1, decodedMessage);


        System_printf("Morse Input: %s\n", message);
        System_printf("Decoded Output: %s\n", decodedMessage);
        System_flush();
    }

    closeDisplayAndReopenUART();
}

void uartTaskFxn(UArg arg0, UArg arg1)
{
    UART_Params_init(&uartParams);
    uartParams.writeDataMode = UART_DATA_TEXT;
    uartParams.readDataMode = UART_DATA_TEXT;
    uartParams.readTimeout = 1;
    uartParams.readReturnMode = UART_RETURN_NEWLINE;
    uartParams.baudRate = 9600;
    uartParams.dataLength = UART_LEN_8;
    uartParams.parityType = UART_PAR_NONE;
    uartParams.stopBits = UART_STOP_ONE;

    uartHandle = UART_open(Board_UART0, &uartParams);
    if (uartHandle == NULL)
    {
        System_printf("Warning: UART not available. Running in offline mode.\n");
        System_flush();
        uartConnected = false;
    }
    else
    {
        uartConnected = true;
        System_printf("UART initialized successfully.\n");
        System_flush();
    }

    char uartBuffer[RECIEVE_BUFFER_SIZE];
    int bytesRead;

    while (1)
    {
        if (uartConnected)
        {
            memset(uartBuffer, 0, RECIEVE_BUFFER_SIZE);
            bytesRead = UART_read(uartHandle, uartBuffer, RECIEVE_BUFFER_SIZE);
            if (bytesRead > 0)
            {
                System_printf("Message received: %s\n", uartBuffer);
                System_flush();

                displayMessage(uartBuffer);
                playMorseCode(uartBuffer);
            }


            int i;
            for (i = 0; i < MAX_MORSE_LENGTH; i++)
            {
                if (morseCode[i] != NULL)
                {
                    int j;
                    for (j = 0; j < MAX_MORSE_LENGTH; j++)
                    {
                        if (morseCode[j] != NULL)
                        {
                            sendCharacterViaUART(morseCode[j]);
                            morseCode[j] = '\0';
                        }
                    }
                    morseIndex = 0;
                }
            }
        }
        Task_sleep(10000 / Clock_tickPeriod);
    }
}


bool sendCharacterViaUART(const char character)
{
    if (!uartConnected || uartHandle == NULL)
    {
        System_printf("Cannot send character - UART not connected\n");
        System_flush();
        return false;
    }

    char buffer[4];
    int len = snprintf(buffer, sizeof(buffer), "%c\r\n\0", character);
    if (len < 0 || len >= sizeof(buffer))
    {
        System_printf("Error formatting character for UART\n");
        System_flush();
        return false;
    }

    if (UART_write(uartHandle, buffer, len) != len)
    {
        System_printf("Error sending character via UART\n");
        System_flush();
        return false;
    }

    System_printf("Successfully sent character %c via UART\n", character);
    System_flush();
    return true;
}

void beep(int duration)
{
    int beepDuration = duration / 2;
    int i;
    for (i = 0; i < duration / 10; i++)
    {
        PIN_setOutputValue(buzzerHandle, BUZZER_PIN, 1);
        Task_sleep(beepDuration);
        PIN_setOutputValue(buzzerHandle, BUZZER_PIN, 0);
        Task_sleep(beepDuration);
    }
}


void button0Fxn(PIN_Handle handle, PIN_Id pinId)
{
    buttonPressed = !buttonPressed;
    System_printf("Button state changed: %s\n", buttonPressed ? "pressed" : "released");
}

void button1Fxn(PIN_Handle handle, PIN_Id pinId)
{
    if (pinId == Board_BUTTON1)
    {
        System_printf("Queuing SOS.");
        morseCode[0] = '.';
        morseCode[1] = '.';
        morseCode[2] = '.';
        morseCode[3] = ' ';
        morseCode[4] = '-';
        morseCode[5] = '-';
        morseCode[6] = '-';
        morseCode[7] = ' ';
        morseCode[8] = '.';
        morseCode[9] = '.';
        morseCode[10] = '.';
        morseCode[11] = '\0';
        beep(DOT_BEEP_DURATION);
        beep(DOT_BEEP_DURATION);
    }
}


void SpaceDetected()
{
    System_printf("Button 1 pressed: Adding space\n");
    if (morseIndex < MAX_MORSE_LENGTH)
    {
        morseCode[morseIndex++] = ' ';
        morseCode[morseIndex] = '\0';

        beep(SPACE_BEEP_DURATION);
    }
}


void detectMorseCode()
{
    float filteredAy = calculateFilteredAy(accly[0]);

    switch (currentState)
    {
    case RESTING:
        if (filteredAy > DOT_THRESHOLD && ignoreCount == 0)
        {
            currentState = DETECTING_DOT;
            samplesChecked = 0;
            System_printf("Entering DETECTING_DOT\n");
        }
        else if (filteredAy < -DASH_THRESHOLD && ignoreCount == 0)
        {
            currentState = DETECTING_DASH;
            samplesChecked = 0;
            System_printf("Entering DETECTING_DASH\n");
        }
        break;

    case DETECTING_DOT:
        samplesChecked++;
        if (filteredAy < DEAD_BAND && filteredAy > -DEAD_BAND)
        {
            System_printf("Detected: Dot\n");
            beep(DOT_BEEP_DURATION);

            if (morseIndex < MAX_MORSE_LENGTH)
            {
                morseCode[morseIndex++] = '.';
                if (buttonPressed && morseIndex < MAX_MORSE_LENGTH)
                {
                    morseCode[morseIndex++] = '.';
                    beep(DOT_BEEP_DURATION);
                }
                morseCode[morseIndex] = '\0';
            }

            currentState = RESTING;
            ignoreCount = IGNORE_SAMPLES;
        }
        break;

    case DETECTING_DASH:
        samplesChecked++;
        if (filteredAy > -DEAD_BAND && filteredAy < DEAD_BAND)
        {
            System_printf("Detected: Dash\n");
            beep(DASH_BEEP_DURATION);

            if (morseIndex < MAX_MORSE_LENGTH)
            {
                morseCode[morseIndex++] = '-';
                if (buttonPressed && morseIndex < MAX_MORSE_LENGTH)
                {
                    morseCode[morseIndex++] = '-';
                    beep(DASH_BEEP_DURATION);
                }
                morseCode[morseIndex] = '\0';
            }

            currentState = RESTING;
            ignoreCount = IGNORE_SAMPLES;
        }
        break;
    }

    if (ignoreCount > 0)
    {
        ignoreCount--;
    }
}

Int main(void)
{
    Board_initGeneral();
    Board_initI2C();
    Board_initSPI();
    Board_initUART();

    hMpuPin = PIN_open(&MpuPinState, MpuPinConfig);
    if (hMpuPin == NULL)
    {
        System_abort("Sensor Pin open failed!");
    }

    Task_Handle sensorTask;
    Task_Params sensorTaskParams;

    Task_Params_init(&sensorTaskParams);
    sensorTaskParams.stackSize = STACKSIZE;
    sensorTaskParams.stack = &sensorTaskStack;
    sensorTaskParams.priority = 2;

    sensorTask = Task_create(sensorTaskFxn, &sensorTaskParams, NULL);
    if (sensorTask == NULL)
    {
        System_abort("Sensor task create failed!");
    }


    Task_Handle uartTaskHandle;
    Task_Params uartTaskParams;

    Task_Params_init(&uartTaskParams);
    uartTaskParams.stackSize = STACKSIZE;
    uartTaskParams.stack = &uartTaskStack;
    uartTaskParams.priority = 1;
    uartTaskHandle = Task_create(uartTaskFxn, &uartTaskParams, NULL);
    if (uartTaskHandle == NULL)
    {
        System_abort("UART Task create failed!");
    }

    initBuzzer();

    button0Handle = PIN_open(&button0State, buttonConfig);
    if (!button0Handle)
    {
        System_abort("Error initializing button pins\n");
    }

    button1Handle = PIN_open(&button1State, button1Config);
    if (!button1Handle)
    {
        System_abort("Error initializing second button pins\n");
    }

    if (PIN_registerIntCb(button0Handle, &button0Fxn) != 0)
    {
        System_abort("Error registering button callback function");
    }

    if (PIN_registerIntCb(button1Handle, &button1Fxn) != 0)
    {
        System_abort("Error registering second button callback function");
    }
    BIOS_start();

    return (0);
}
