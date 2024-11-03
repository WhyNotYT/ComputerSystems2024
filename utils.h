#pragma once

#include <stdio.h>
#include <string.h>

/* XDCtools files */
#include <xdc/std.h>
#include <xdc/runtime/System.h>

void printFloat(float value)
{
    // Check for exact zero and print it directly
    if (value == 0.0f)
    {
        System_printf("0.00");
        return;
    }

    int intPart = (int)value;                          // Get the integer part
    float fracPart = value - intPart;                  // Get the fractional part
    int sign = (intPart < 0 || fracPart < 0) ? -1 : 1; // Determine the sign

    // Print the sign if negative
    if (sign == -1)
    {
        System_printf("-");
        intPart = -intPart;   // Make the integer part positive for printing
        fracPart = -fracPart; // Make the fractional part positive for printing
    }

    System_printf("%d.", intPart); // Print integer part

    // Multiply the fractional part to shift two decimal places
    int fracInt = (int)(fracPart * 100.0f); // Shift two places

    // If the fractional part is negative, make it positive
    if (fracInt < 0)
    {
        fracInt = -fracInt;
    }

    // Print fractional part with leading zeros if necessary
    System_printf("%02d", fracInt);
}

void displaySensorAvg(float *arx, float *ary, float *arz, const unsigned int arrSize)
{
    // Print the header
    System_printf("time,ax,ay,az,gx,gy,gz\n");
    int i;
    printFloat(calculateAvg(arx, arrSize));
    System_printf(",");
    printFloat(calculateAvg(ary, arrSize));
    System_printf(",");
    printFloat(calculateAvg(arz, arrSize));
    // System_printf(",");
    // printFloat(gx[i]); // You may also print gy and gz if you have them
    // System_printf(",");
    // printFloat(gy[i]);
    // System_printf(",");
    // printFloat(gz[i]);
    System_printf("\n");

    System_flush();
}

char morseToAscii(const char *morse)
{
    // Define a mapping from Morse code to ASCII characters
    if (strcmp(morse, ".-") == 0)
        return 'A';
    else if (strcmp(morse, "-...") == 0)
        return 'B';
    else if (strcmp(morse, "-.-.") == 0)
        return 'C';
    else if (strcmp(morse, "-..") == 0)
        return 'D';
    else if (strcmp(morse, ".") == 0)
        return 'E';
    else if (strcmp(morse, "..-.") == 0)
        return 'F';
    else if (strcmp(morse, "--.") == 0)
        return 'G';
    else if (strcmp(morse, "....") == 0)
        return 'H';
    else if (strcmp(morse, "..") == 0)
        return 'I';
    else if (strcmp(morse, ".---") == 0)
        return 'J';
    else if (strcmp(morse, "-.-") == 0)
        return 'K';
    else if (strcmp(morse, ".-..") == 0)
        return 'L';
    else if (strcmp(morse, "--") == 0)
        return 'M';
    else if (strcmp(morse, "-.") == 0)
        return 'N';
    else if (strcmp(morse, "---") == 0)
        return 'O';
    else if (strcmp(morse, ".--.") == 0)
        return 'P';
    else if (strcmp(morse, "--.-") == 0)
        return 'Q';
    else if (strcmp(morse, ".-.") == 0)
        return 'R';
    else if (strcmp(morse, "...") == 0)
        return 'S';
    else if (strcmp(morse, "-") == 0)
        return 'T';
    else if (strcmp(morse, "..-") == 0)
        return 'U';
    else if (strcmp(morse, "...-") == 0)
        return 'V';
    else if (strcmp(morse, ".--") == 0)
        return 'W';
    else if (strcmp(morse, "-..-") == 0)
        return 'X';
    else if (strcmp(morse, "-.--") == 0)
        return 'Y';
    else if (strcmp(morse, "--..") == 0)
        return 'Z';
    else if (strcmp(morse, "-----") == 0)
        return '0';
    else if (strcmp(morse, ".----") == 0)
        return '1';
    else if (strcmp(morse, "..---") == 0)
        return '2';
    else if (strcmp(morse, "...--") == 0)
        return '3';
    else if (strcmp(morse, "....-") == 0)
        return '4';
    else if (strcmp(morse, ".....") == 0)
        return '5';
    else if (strcmp(morse, "-....") == 0)
        return '6';
    else if (strcmp(morse, "--...") == 0)
        return '7';
    else if (strcmp(morse, "---..") == 0)
        return '8';
    else if (strcmp(morse, "----.") == 0)
        return '9';
    // Add more characters as needed
    else
        return '?'; // Unknown character
}

float previousAy = 0.0; // Store the previous ay reading for filtering

float calculateFilteredAy(const float ay)
{
    // Simple moving average filter or low-pass filter could be implemented here
    // For simplicity, let's average the current and previous readings
    float filteredAy = (ay + previousAy) / 2.0;
    previousAy = ay; // Update previous value for the next call
    return filteredAy;
}

float calculateAvg(const float *array, unsigned const int arrSize)
{
    float result = 0;
    int i;
    for (i = 0; i < arrSize; i++)
    {
        result = result + array[i];
    }
    result = result / arrSize;

    return result;
}
