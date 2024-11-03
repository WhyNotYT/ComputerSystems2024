
/* C libraries */
#include <stdio.h>
#include <string.h>
#include <stdint.h>
#include <ti/drivers/PIN.h> // Include this if using TI drivers, adjust according to your platform

/* XDCtools files */
#include <xdc/std.h>
#include <xdc/runtime/System.h>
/* Additional includes for display */
#include <ti/mw/display/Display.h>

/* BIOS Header files */
#include <ti/sysbios/BIOS.h>
#include <ti/sysbios/knl/Clock.h>
#include <ti/sysbios/knl/Task.h>
#include <ti/drivers/PIN.h>
#include <ti/drivers/pin/PINCC26XX.h>
#include <ti/drivers/i2c/I2CCC26XX.h>
#include <ti/drivers/I2C.h>
#include <ti/drivers/Power.h>
#include <ti/drivers/power/PowerCC26XX.h>
#include <ti/mw/display/Display.h>
#include <ti/mw/display/DisplayExt.h>
#include <ti/drivers/UART.h>
#include <ti/sysbios/BIOS.h> // For Task_sleep
/* Board Header files */
#include "Board.h"
#include "utils.h"

// #include "wireless/comm_lib.h"
#include "sensors/opt3001.h"
#include "sensors/mpu9250.h"

/* Tasks */
#define STACKSIZE 2048
Char sensorTaskStack[STACKSIZE];
Char displayTaskStack[STACKSIZE];

// RTOS variables for first button pin
static PIN_Handle buttonHandle;
static PIN_State buttonState;

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
static PIN_Config MpuPinConfig[] = {
    Board_MPU_POWER | PIN_GPIO_OUTPUT_EN | PIN_GPIO_HIGH | PIN_PUSHPULL | PIN_DRVSTR_MAX,
    PIN_TERMINATE};

// MPU I2C interface
static const I2CCC26XX_I2CPinCfg i2cMPUCfg = {
    .pinSDA = Board_I2C0_SDA1,
    .pinSCL = Board_I2C0_SCL1};

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
#define DOT_THRESHOLD 0.6       // Higher threshold for dot detection
#define DASH_THRESHOLD 0.6      // Higher threshold for dash detection
#define DEAD_BAND 0.3           // Deadband to filter out noise
#define MAX_SAMPLES_TO_CHECK 20 // Number of samples to check for gesture confirmation
#define EPSILON 0.05            // Small epsilon for float comparisons
#define MAX_MORSE_LENGTH 10     // Maximum length of Morse code
#define IGNORE_SAMPLES 10       // Number of samples to ignore after detection

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

static PIN_Config buzzerConfig[] = {
    BUZZER_PIN | PIN_GPIO_OUTPUT_EN | PIN_GPIO_LOW | PIN_PUSHPULL | PIN_DRVSTR_MAX, // Configure the buzzer pin as output
    PIN_TERMINATE};

// Function to initialize the buzzer
void initBuzzer()
{
    buzzerHandle = PIN_open(&buzzerState, buzzerConfig);
    if (!buzzerHandle)
    {
        System_abort("Error initializing buzzer pins\n");
    }
}

// Function to produce a beep sound
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
            if (morseIndex < MAX_MORSE_LENGTH)
            {
                morseCode[morseIndex++] = '.'; // Append dot to Morse code
                morseCode[morseIndex] = '\0';  // Null-terminate the string
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
            if (morseIndex < MAX_MORSE_LENGTH)
            {
                morseCode[morseIndex++] = '-'; // Append dash to Morse code
                morseCode[morseIndex] = '\0';  // Null-terminate the string
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

void buttonFxn(PIN_Handle handle, PIN_Id pinId)
{
    // When the button is pressed, lookup the Morse code and print the corresponding ASCII character
    char asciiChar = morseToAscii(morseCode);
    System_printf("Detected Character: %c\n", asciiChar);

    // Reset the Morse code for the next character
    morseIndex = 0;
    morseCode[morseIndex] = '\0'; // Null-terminate the string
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
        Task_sleep(500 / Clock_tickPeriod); // Delay for readability
    }

    I2C_close(i2cMPU);
    PIN_setOutputValue(hMpuPin, Board_MPU_POWER, Board_MPU_POWER_OFF);
}

void updateDisplay()
{
    // Function to update the display with the current Morse code sequence
    if (displayHandle)
    {
        Display_clear(displayHandle);                   // Clear the display
        Display_print0(displayHandle, 1, 1, morseCode); // Print the Morse code
    }
}

Void displayTaskFxn(UArg arg0, UArg arg1)
{

    /* Init Display */
    Display_Params displayParams;
    displayParams.lineClearMode = DISPLAY_CLEAR_BOTH;
    Display_Params_init(&displayParams);

    displayHandle = Display_open(Display_Type_LCD, &displayParams);
    tContext *pContext = DisplayExt_getGrlibContext(displayHandle);
    Display_clear(displayHandle);
    if (displayHandle == NULL)
    {
        System_abort("Error initializing Display\n");
    }

    // Loop to keep updating the display
    while (1)
    {
        Task_sleep(500000 / Clock_tickPeriod); // Sleep to reduce CPU usage
    }
}

Int main(void)
{
    // Initialize board
    Board_initGeneral();
    Board_initI2C();
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
    // Display task variables
    Task_Handle displayTask;
    Task_Params displayTaskParams;

    // Display task
    Task_Params_init(&displayTaskParams);
    displayTaskParams.stackSize = STACKSIZE;
    displayTaskParams.stack = &displayTaskStack;
    displayTaskParams.priority = 2;

    // displayTask = Task_create(displayTaskFxn, &displayTaskParams, NULL);
    // if (displayTask == NULL)
    // {
    //     System_abort("Task create failed!");
    // }
    // Comm task variables
    initBuzzer();

    // Task_Handle commTask;
    // Task_Params commTaskParams;

    // Comm task
    // Task_Params_init(&commTaskParams);
    // commTaskParams.stackSize = STACKSIZE;
    // commTaskParams.stack = &commTaskStack;
    // commTaskParams.priority = 1;

    // commTask = Task_create(commTaskFxn, &commTaskParams, NULL);
    // if (commTask == NULL)
    // {
    //     System_abort("Task create failed!");
    // }

    // Button pin initialization
    buttonHandle = PIN_open(&buttonState, buttonConfig);
    if (!buttonHandle)
    {
        System_abort("Error initializing button pins\n");
    }

    // Interrupt for button pin
    if (PIN_registerIntCb(buttonHandle, &buttonFxn) != 0)
    {
        System_abort("Error registering button callback function");
    }

    // Initialize wireless transfer
    // Init6LoWPAN();

    // Start BIOS
    BIOS_start();

    return (0);
}