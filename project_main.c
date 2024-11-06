
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
#include <ti/sysbios/BIOS.h> // For Task_sleep
#include <ti/sysbios/knl/Clock.h>
#include <ti/sysbios/knl/Task.h>
/* Board Header files */
#include "Board.h"
#include "utils.h"

// #include "wireless/comm_lib.h"
#include "sensors/mpu9250.h"
#include "sensors/opt3001.h"

/* Tasks */
#define STACKSIZE 2048
Char sensorTaskStack[STACKSIZE];
Char displayTaskStack[STACKSIZE];

// RTOS variables for first button pin
static PIN_Handle button0Handle;
static PIN_State button0State;

// RTOS variables for second button pin
static PIN_Handle button1Handle;
static PIN_State button1State;

Display_Handle displayHandle; // Display handle
Display_Params displayParams; // Display parameters

// Config for first button pin
PIN_Config buttonConfig[] = {
    Board_BUTTON0 | PIN_INPUT_EN | PIN_PULLUP | PIN_IRQ_NEGEDGE, // Neljän vakion TAI-operaatio
    PIN_TERMINATE                                                // Taulukko lopetetaan aina tällä vakiolla
};

// Config for second button pin
PIN_Config button1Config[] = {
    Board_BUTTON1 | PIN_INPUT_EN | PIN_PULLUP | PIN_IRQ_NEGEDGE, // Neljän vakion TAI-operaatio
    PIN_TERMINATE                                                // Taulukko lopetetaan aina tällä vakiolla
};

// MPU global variables
static PIN_Handle hMpuPin;
static PIN_State MpuPinState;
static PIN_Config MpuPinConfig[] = {Board_MPU_POWER | PIN_GPIO_OUTPUT_EN | PIN_GPIO_HIGH | PIN_PUSHPULL |
                                        PIN_DRVSTR_MAX,
                                    PIN_TERMINATE};

// MPU I2C interface
static const I2CCC26XX_I2CPinCfg i2cMPUCfg = {.pinSDA = Board_I2C0_SDA1, .pinSCL = Board_I2C0_SCL1};

#define SENSOR_HISTORY_SIZE 4

float acclx[SENSOR_HISTORY_SIZE] = {0};
float accly[SENSOR_HISTORY_SIZE] = {0};
float acclz[SENSOR_HISTORY_SIZE] = {0};

unsigned int timeCounter = 0;

void updateSensorData(float ax, float ay, float az)
{
    int i;
    // Shift history
    for (i = SENSOR_HISTORY_SIZE - 1; i > 0; i--)
    {
        acclx[i] = acclx[i - 1];
        accly[i] = accly[i - 1];
        acclz[i] = acclz[i - 1];
    }
    // Update the latest values
    acclx[0] = ax;
    accly[0] = ay;
    acclz[0] = az;
}
#define DOT_THRESHOLD 0.5       // Higher threshold for dot detection
#define DASH_THRESHOLD 0.7      // Higher threshold for dash detection
#define DEAD_BAND 0.4           // Deadband to filter out noise
#define MAX_SAMPLES_TO_CHECK 20 // Number of samples to check for gesture confirmation
#define EPSILON 0.05            // Small epsilon for float comparisons
#define MAX_MORSE_LENGTH 10     // Maximum length of Morse code
#define IGNORE_SAMPLES 100       // Number of samples to ignore after detection
#define RECIEVE_BUFFER_SIZE 64
typedef enum
{
    RESTING,
    DETECTING_DOT,
    DETECTING_DASH
} GestureState;

GestureState currentState = RESTING;  // Current state for gesture detection
int samplesChecked = 0;               // Counter for samples checked after hitting the threshold
char morseCode[MAX_MORSE_LENGTH + 1]; // String to store Morse code
int morseIndex = 0;                   // Current index for storing Morse code
int ignoreCount = 0;                  // Counter to ignore samples after detection

#define BUZZER_PIN Board_BUZZER // Define the PIN ID for the buzzer (update according to your board)

#define DOT_BEEP_DURATION 200  // Duration of the beep for a dot
#define DASH_BEEP_DURATION 400 // Duration of the beep for a dash

static PIN_Handle buzzerHandle;
static PIN_State buzzerState;

static PIN_Config buzzerConfig[] = {BUZZER_PIN | PIN_GPIO_OUTPUT_EN | PIN_GPIO_LOW | PIN_PUSHPULL |
                                        PIN_DRVSTR_MAX, // Configure the buzzer pin as output
                                    PIN_TERMINATE};

Char uartTaskStack[STACKSIZE];
Char displayTaskStack[STACKSIZE];

static UART_Handle uartHandle;
static UART_Params uartParams;
double ambientLight = -1000.0;


char recievedMessageStr[RECIEVE_BUFFER_SIZE];

// Function to initialize the buzzer
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
    float ax, ay, az, gx, gy, gz; // Accelerometer and gyro data

    // Initialize
    I2C_Handle i2cMPU;
    I2C_Params i2cMPUParams;
    I2C_Params_init(&i2cMPUParams);
    i2cMPUParams.bitRate = I2C_400kHz;
    i2cMPUParams.custom = (uintptr_t)&i2cMPUCfg;

    // MPU open I2C
    i2cMPU = I2C_open(Board_I2C, &i2cMPUParams);
    if (i2cMPU == NULL)
    {
        System_abort("Error Initializing I2CMPU 1\n");
    }

    // MPU power on
    PIN_setOutputValue(hMpuPin, Board_MPU_POWER, Board_MPU_POWER_ON);

    // Wait for the sensor to power up
    Task_sleep(100000 / Clock_tickPeriod);

    // MPU9250 setup
    mpu9250_setup(&i2cMPU);


    while (1)
    {
        // Use the already open i2cMPU interface to get data
        mpu9250_get_data(&i2cMPU, &ax, &ay, &az, &gx, &gy, &gz);

        updateSensorData(ax, ay, az);
        // Detect Morse code based on moving average
        detectMorseCode(ay);
        timeCounter++;


        Task_sleep(1000 / Clock_tickPeriod); // Delay for readability
    }

    I2C_close(i2cMPU);
    PIN_setOutputValue(hMpuPin, Board_MPU_POWER, Board_MPU_POWER_OFF);
}

static bool buttonPressed = false;

// Add these globals at the top with other globals
static bool uartConnected = false;

// Morse code timing constants
#define SYMBOL_PAUSE 10000
#define WORD_PAUSE 30000
Display_Handle hDisplayLcd;
// Function to play received morse code
void playMorseCode(const char *morse)
{
    if (morse == NULL)
        return;
    int i;
    // memcpy(morse, recievedMessageStr, RECIEVE_BUFFER_SIZE);
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
            // Ignore invalid characters
            break;
        }
    }
}


void displayReceivedMorse(const char *receivedMorse)
{
    char decodedMessage[RECIEVE_BUFFER_SIZE] = {0};
    int decodedIndex = 0;
    char *currentChar = decodedMessage;
    const char *start = receivedMorse;

    while (*start != '\0')
    {
        // Find the end of the current Morse code character
        const char *end = start;
        while (*end != ' ' && *end != '\0')
        {
            end++;
        }

        // Translate the current Morse code sequence to a character
        *currentChar++ = morseToAscii(start);

        // Move to the next Morse code character
        start = (*end == ' ') ? end + 1 : end;
    }

    // Display the decoded message on the screen
    Display_print0(hDisplayLcd, 5, 3, decodedMessage);
}
// Modified UART task to handle both sending and receiving
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
            int bytesRead = UART_read(uartHandle, uartBuffer, RECIEVE_BUFFER_SIZE);
            if (bytesRead > 0)
            {
                // Process the received data
                System_printf("Message received: %s\n", uartBuffer);
                System_flush();
                displayReceivedMorse(uartBuffer);
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
                            morseCode[j] = '\0'; // Null-terminate the string
                        }
                    }
                    // Reset the Morse code for the next character
                    morseIndex = 0;
                }
            }
        }
        Task_sleep(10000 / Clock_tickPeriod);
    }
}
// Modified send function with better error handling
bool sendCharacterViaUART(const char character)
{
    if (!uartConnected || uartHandle == NULL)
    {
        System_printf("Cannot send character - UART not connected\n");
        System_flush();
        return false;
    }

    char buffer[4];
    int len = snprintf(buffer, sizeof(buffer), "%c\r\n", character);
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
    int beepDuration = duration / 2; // Duration for ON and OFF states
    int i;
    for (i = 0; i < duration / 10; i++)
    {                                                    // Repeat to create a longer beep
        PIN_setOutputValue(buzzerHandle, BUZZER_PIN, 1); // Turn the buzzer ON
        Task_sleep(beepDuration);                        // Keep it ON for half the duration
        PIN_setOutputValue(buzzerHandle, BUZZER_PIN, 0); // Turn the buzzer OFF
        Task_sleep(beepDuration);                        // Keep it OFF for half the duration
    }
}


void button0Fxn(PIN_Handle handle, PIN_Id pinId)
{
    buttonPressed = !buttonPressed;
    System_printf("Button state changed: %s\n", buttonPressed ? "pressed" : "released");
}

// Add new function for second button
void button1Fxn(PIN_Handle handle, PIN_Id pinId)
{
    if (pinId == Board_BUTTON1) // Check if it's the second button
    {
        System_printf("Button 1 pressed: Adding space\n");
        // beep(DOT_BEEP_DURATION / 2);
        // Add space to morse code array if there's room
        if (morseIndex < MAX_MORSE_LENGTH)
        {
            morseCode[morseIndex++] = ' '; // Add space
            morseCode[morseIndex] = '\0';  // Null-terminate

        }
    }
}


void detectMorseCode()
{
    float filteredAy = calculateFilteredAy(accly[0]); // Get the filtered acceleration on Y-axis

    switch (currentState)
    {
    case RESTING:
        // Check for dot gesture
        if (filteredAy > DOT_THRESHOLD && ignoreCount == 0)
        {
            currentState = DETECTING_DOT;              // Transition to detecting dot
            samplesChecked = 0;                        // Reset the samples checked counter
            System_printf("Entering DETECTING_DOT\n"); // Debug
        }
        // Check for dash gesture
        else if (filteredAy < -DASH_THRESHOLD && ignoreCount == 0)
        {
            currentState = DETECTING_DASH;              // Transition to detecting dash
            samplesChecked = 0;                         // Reset the samples checked counter
            System_printf("Entering DETECTING_DASH\n"); // Debug
        }
        break;

    case DETECTING_DOT:
        samplesChecked++;
        if (filteredAy < DEAD_BAND && filteredAy > -DEAD_BAND)
        {
            // Confirm dot gesture if we have seen the deadband
            System_printf("Detected: Dot\n");
            beep(DOT_BEEP_DURATION); // Beep for dot

            // Add dots based on button state
            if (morseIndex < MAX_MORSE_LENGTH)
            {
                morseCode[morseIndex++] = '.'; // First dot
                // Add second dot if button is pressed and there's space
                if (buttonPressed && morseIndex < MAX_MORSE_LENGTH)
                {
                    morseCode[morseIndex++] = '.';
                    beep(DOT_BEEP_DURATION); // Additional beep for second dot
                }
                morseCode[morseIndex] = '\0'; // Null-terminate the string
            }

            currentState = RESTING;       // Reset to resting state
            ignoreCount = IGNORE_SAMPLES; // Start ignoring samples
        }
        break;

    case DETECTING_DASH:
        samplesChecked++;
        if (filteredAy > -DEAD_BAND && filteredAy < DEAD_BAND)
        {
            // Confirm dash gesture if we have seen the deadband
            System_printf("Detected: Dash\n");
            beep(DASH_BEEP_DURATION); // Beep for dash

            // Add dashes based on button state
            if (morseIndex < MAX_MORSE_LENGTH)
            {
                morseCode[morseIndex++] = '-'; // First dash
                // Add second dash if button is pressed and there's space
                if (buttonPressed && morseIndex < MAX_MORSE_LENGTH)
                {
                    morseCode[morseIndex++] = '-';
                    beep(DASH_BEEP_DURATION); // Additional beep for second dash
                }
                morseCode[morseIndex] = '\0'; // Null-terminate the string
            }

            currentState = RESTING;       // Reset to resting state
            ignoreCount = IGNORE_SAMPLES; // Start ignoring samples
        }
        break;
    }

    // Handle sample ignoring after detection
    if (ignoreCount > 0)
    {
        ignoreCount--; // Decrement the ignore count
    }
}

Int main(void)
{
    // Initialize board
    Board_initGeneral();
    Board_initI2C();
    Board_initSPI();
    Board_initUART();
    // Open MPU power pin
    hMpuPin = PIN_open(&MpuPinState, MpuPinConfig);
    if (hMpuPin == NULL)
    {
        System_abort("Sensor Pin open failed!");
    }

    // Sensor task variables
    Task_Handle sensorTask;
    Task_Params sensorTaskParams;

    // Sensor task
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

    // Comm task variables
    initBuzzer();

    // Button pin initialization
    button0Handle = PIN_open(&button0State, buttonConfig);
    if (!button0Handle)
    {
        System_abort("Error initializing button pins\n");
    }

    // Button pin initialization for second button
    button1Handle = PIN_open(&button1State, button1Config);
    if (!button1Handle)
    {
        System_abort("Error initializing second button pins\n");
    }

    // Register interrupt for first button
    if (PIN_registerIntCb(button0Handle, &button0Fxn) != 0)
    {
        System_abort("Error registering button callback function");
    }

    // Register interrupt for second button
    if (PIN_registerIntCb(button1Handle, &button1Fxn) != 0)
    {
        System_abort("Error registering second button callback function");
    }
    // Initialize wireless transfer
    // Init6LoWPAN();

    // Start BIOS
    BIOS_start();

    return (0);
}
