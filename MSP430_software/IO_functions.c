/*
 * IO_functions.c
 *
 *  Created on: 18.04.2025
 *      Author: ihma
 */

#include "IO_functions.h"

uint8_t float2digits(float value, char* txt, int8_t digits, int8_t decimals)
{
    unsigned int digit;
    int n = 0;
    int p = 0;
    unsigned int dot = 0;

    if (value < 0.0)
    {
        value *= -1.0;
        txt[p++] = '-';     // indicate negative value with a '-'
        dot++;              // needed if a character is displayed
    }
    else
    {
        txt[p++] = ' ';     // use a space character to keep length for positive and negative values constant
//        txt[p++] = '+';  // indicate positive value with a '+'
        dot++;              // needed if a character is displayed
    }

    // adjust for processing the first digit
    for (n=0; n < (digits - decimals); n++)
    {
        value *= 0.1;
    }
    for (n=0; n < (decimals - digits + 1); n++)
    {
        value *= 10;
    }

    for (n=0; n< (digits-decimals); n++) // process all digits before decimal point
    {
        value *= 10;
        digit = value;
        txt[p++] = digit +48;
        value -= digit;
    }
    if (decimals > 0)
    {
        dot++;
        txt[p++] = (unsigned int)'.';
        for (n=n; n < digits+1; n++) // process all digits behind decimal point
        {
            value *= 10;
            digit = value;
            txt[p++] = digit +48;
            value -= digit;
        }
    }
    return(digits+dot);
}


