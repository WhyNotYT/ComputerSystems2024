#include <stdint.h>
#include <stdio.h>
#include <string.h>

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


char recievedMessageStr[RECIEVE_BUFFER_SIZE];

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
