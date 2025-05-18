#include <msp430.h> 
#include <Config_Common.h>

#include <stdbool.h>
#include <stdint.h>
#include <HAL.h>
#include <GUI_mpack.h>
#include <GUIComm.h>
#include <QmathLib.h>
#include <callbacks_mpack.h>
#include <math.h>
#include "eusci_b_spi.h"
#include "GPIO.h"
#include "CS.h"
#include "IO_functions.h"

// # defines for application code
#define STR_LEN_ONE      1

// Global variables
volatile uint8_t  command, adcChannel, adcA0Preload, adcA1Preload, adcA0Gain, adcA1Gain, adcReady;
volatile int32_t adcA0Data, adcA1Data;

// Power Measurement defines
#define MEASUREMENT_COUNT  1965  // number of samples per measurement 1965 = 0.5s
#define WINDOW_LENGTH  50         // 0: don't use a window
#define GRAPH_COUNT  100         // number of samples for graphic debug display
#define VSCALE 0.000061965373    // scaling factor to calibrate voltage measurements
#define ISCALE 0.000004220213    // scaling factor to calibrate current measurements
#define TXTLENGTH 80             // maximum number of characters allowed for the TXData buffer
#define ALPHA 1.00               // smoothing factor for Vdcmean and Idcmean
#define PI 3.14159265

int32_t graph1[GRAPH_COUNT];        // for debugging purposes, only
int32_t graph2[GRAPH_COUNT];        // for debugging purposes, only
int64_t  Vms, Ims, Pms, Vdc, Idc;     // variables to sum up sample values; ms: sum of squared or multiplied values
double Vdcmean, Idcmean;

double Vrms, Irms, Prms, VArms, tmpdouble;

uint16_t n;
int64_t  voltage, current;
int16_t  window[WINDOW_LENGTH];

char TXData[TXTLENGTH];
volatile uint8_t TXcount = 0;
volatile uint8_t* TXpointer;
uint8_t RXData = 0;


//! \brief RX Command structure.
//!         The corresponding callback will be called when the command is
//!         received from GUI.
//! Note: Shorter command names take less time to process
const tGUI_RxCmd GUI_RXCommands[] =
{
    {"3",  GUICallback_SetA0Preload},
    {"4",  GUICallback_SetA1Preload},
    {"5",  GUICallback_SetA0Gain},
    {"6",  GUICallback_SetA1Gain},
};

// Main
void main(void)
{


    EUSCI_B_SPI_initMasterParam spiMasterConfig =
     {
         EUSCI_B_SPI_CLOCKSOURCE_ACLK,              // ACLK Clock Source
         32000,                                     // ACLK = 32kHz
         16000,                                     // SPICLK = 16kHz
         EUSCI_B_SPI_MSB_FIRST,                     // MSB First
         EUSCI_B_SPI_PHASE_DATA_CHANGED_ONFIRST_CAPTURED_ON_NEXT,    // Phase
         EUSCI_B_SPI_CLOCKPOLARITY_INACTIVITY_HIGH, // High polarity
         EUSCI_B_SPI_4PIN_UCxSTE_ACTIVE_LOW        // 4Wire SPI Mode
     };

    UCB0CTLW0 |= UCSTEM;

    WDTCTL = WDTPW | WDTHOLD;                       // Stop WDT

    // Initialize clocks, GPIOs
    HAL_System_Init();


    // Setting P1.5, P1.6 and P1.7 as SPI pins.
    GPIO_setAsPeripheralModuleFunctionInputPin(GPIO_PORT_P1,
            GPIO_PIN5 | GPIO_PIN6 | GPIO_PIN7,
            GPIO_PRIMARY_MODULE_FUNCTION);

    // SPI CS Pin
    P1DIR |= GPIO_PIN4;  // set P1.4 (GPIO3) as output
    P1OUT |= GPIO_PIN4; //  set P1.4 (GPIO3) to H

    // Setting the DCO to use the internal resistor. DCO will be at 16.384MHz
    // ACLK is at 32kHz
    CS_setupDCO(CS_INTERNAL_RESISTOR);

    // Configure and enable the SPI peripheral
    EUSCI_B_SPI_initMaster(EUSCI_B0_BASE, &spiMasterConfig);
    EUSCI_B_SPI_enable(EUSCI_B0_BASE);

    // initialize window
    for(n=0; n < WINDOW_LENGTH; n++)
    {
        tmpdouble = sin(PI/2.0*(float)(n+1)/(WINDOW_LENGTH+2));
        tmpdouble *= tmpdouble;
        window[n] = 32768.0 * tmpdouble;
    }


    // Put the welcome message in the transfer buffer
    TXData[TXcount++] = 'L';
    TXData[TXcount++] = 'i';
    TXData[TXcount++] = 'e';
    TXData[TXcount++] = 'b';
    TXData[TXcount++] = 'e';
    TXData[TXcount++] = 'r';
    TXData[TXcount++] = ' ';
    TXData[TXcount++] = 'S';
    TXData[TXcount++] = 'c';
    TXData[TXcount++] = 'h';
    TXData[TXcount++] = 'o';
    TXData[TXcount++] = 'l';
    TXData[TXcount++] = 'l';
    TXData[TXcount++] = 'i';
    TXData[TXcount++] = '!';
    TXData[TXcount++] = ' ';
    TXpointer = TXData;

//    P1OUT &= ~GPIO_PIN4; //  set P1.4 (GPIO3) to L to enable CS
    EUSCI_B_SPI_transmitData(EUSCI_B0_BASE, *TXpointer++);

    EUSCI_B_SPI_enableInterrupt(EUSCI_B0_BASE, EUSCI_B_SPI_RECEIVE_INTERRUPT);

//    __delay_cycles(36000);                           // Delay ~2ms
//    P1OUT |= GPIO_PIN4; //  set P1.4 (GPIO3) to H to disable CS


    // Initialize variables
    adcReady = 0;                                   // Clear ADC ready flag
    command = 0;                                    // Clear command

    // Configure SD24 reference
    SD24CTL = SD24REFS;                             // Internal reference
//    SD24CTL = 0;                                      // External reference
    __delay_cycles(3600);                           // Delay ~200us for 1.2V reference to settle

    // Configure SD24 ADC channels
    SD24INCTL0 |= SD24GAIN_1;                       // Initialize PGA gain for both channels
    SD24INCTL1 |= SD24GAIN_1;
//    SD24CCTL0  |= SD24OSR_256 | SD24GRP;   // OSR = 256, 2's complement
    SD24CCTL0  |= SD24OSR_256 | SD24DF | SD24GRP;   // OSR = 256, 2's complement
    SD24CCTL1  |= SD24OSR_256 | SD24DF | SD24IE;    // Group w/channel A0, enable ADC interrupts
    SD24PRE0 = 0;
    SD24PRE1 = 0;

    // Configure GUI communication
//    GUI_Init();                                                                              // Initialize GUI layer
//    GUI_InitRxCmd( &GUI_RXCommands[0],(sizeof(GUI_RXCommands)/sizeof(GUI_RXCommands[0])) );  // Initialize GUI receive

    // Start ADC conversions
    SD24CCTL1  |= SD24SC;

    // Enable interrupts
    __bis_SR_register(GIE);

    n = 0;
    Vms = 0;
    Ims = 0;
    Pms = 0;
    Vdc = 0;
    Idc = 0;
    Vdcmean = 0.0;
    Idcmean = 0.0;


    // Communication state machine
    while(1)
    {

        // Process ADC_A0_PRELOAD command
//        if(command == ADC_A0_PRELOAD)
        if(1==0)
        {

            // Set preload based on GUI inputs
            SD24PRE0 = adcA0Preload;                // Set ADC channel preload

            // Start ADC conversions
            SD24CCTL1 |= SD24IE;                    // Enable ADC interrupts
            SD24CCTL1 |= SD24SC;                    // Set bit to start ADC conversions
            command = 0;                            // Clear command
        }

        // Process ADC_A1_PRELOAD command
//        else if(command == ADC_A1_PRELOAD)
        else if (1==0)
        {

            // Set preload based on GUI inputs
            SD24PRE1 = adcA1Preload;                // Set ADC channel preload

            // Start ADC conversions
            SD24CCTL1 |= SD24IE;                    // Enable ADC interrupts
            SD24CCTL1 |= SD24SC;                    // Set bit to start ADC conversions
            command = 0;                            // Clear command
        }

        // Process ADC_A0_GAIN command
//        else if(command == ADC_A0_GAIN)
        else if (1==0)
        {

            // Reset PGA gain to 1
            SD24INCTL0 &= ~(SD24GAIN0 | SD24GAIN1 | SD24GAIN2);

            // Set PGA gain based on GUI inputs
            if(adcA0Gain == 1)
            {
                SD24INCTL0 |= SD24GAIN_1;           // Set PGA gain to 1
            }
            else if(adcA0Gain == 2)
            {
                SD24INCTL0 |= SD24GAIN_2;           // Set PGA gain to 2
            }
            else if(adcA0Gain == 4)
            {
                SD24INCTL0 |= SD24GAIN_4;           // Set PGA gain to 4
            }
            else if(adcA0Gain == 8)
            {
                SD24INCTL0 |= SD24GAIN_8;           // Set PGA gain to 8
            }
            else if(adcA0Gain == 16)
            {
                SD24INCTL0 |= SD24GAIN_16;          // Set PGA gain to 16
            }

            GUIComm_sendUInt8("5", STR_LEN_ONE, adcA0Gain);     // Send received gain to GUI for confirmation

            // Start ADC conversions
            SD24CCTL1 |= SD24IE;                    // Enable ADC interrupts
            SD24CCTL1 |= SD24SC;                    // Set bit to start ADC conversions
            command = 0;                            // Clear command
        }

        // Process ADC_A1_GAIN command
//        else if(command == ADC_A1_GAIN)
        if (1==0)
        {

            // Reset PGA gain to 1
            SD24INCTL1 &= ~(SD24GAIN0 | SD24GAIN1 | SD24GAIN2);

            // Set PGA gain based on GUI inputs
            if(adcA1Gain == 1)
            {
                SD24INCTL1 |= SD24GAIN_1;           // Set PGA gain to 1
            }
            else if(adcA1Gain == 2)
            {
                SD24INCTL1 |= SD24GAIN_2;           // Set PGA gain to 2
            }
            else if(adcA1Gain == 4)
            {
                SD24INCTL1 |= SD24GAIN_4;           // Set PGA gain to 4
            }
            else if(adcA1Gain == 8)
            {
                SD24INCTL1 |= SD24GAIN_8;           // Set PGA gain to 8
            }
            else if(adcA1Gain == 16)
            {
                SD24INCTL1 |= SD24GAIN_16;          // Set PGA gain to 16
            }

            GUIComm_sendUInt8("6", STR_LEN_ONE, adcA1Gain);     // Send received gain back to GUI

            // Start ADC conversions
            SD24CCTL1 |= SD24IE;                    // Enable ADC interrupts
            SD24CCTL1 |= SD24SC;                    // Set bit to start ADC conversions
            command = 0;                            // Clear command
        }

        // Send ADC data to GUI (when ready)
        else
        {
            // Update GUI graph with latest ADC measurements
            if(adcReady == 1)
            {
                voltage = adcA1Data; // voltage
                current = adcA0Data; // current

                // apply first part of window
                if (n < WINDOW_LENGTH)
                {
                    voltage = ((voltage - (int64_t)Vdcmean) * window[n])>>15 + (int64_t)Vdcmean;
                    current = ((current - (int64_t)Idcmean) * window[n])>>15 + (int64_t)Idcmean;
                }
                // apply second part of window
                if (n > (MEASUREMENT_COUNT - WINDOW_LENGTH))
                {
                    voltage = ((voltage - (int64_t)Vdcmean) * window[MEASUREMENT_COUNT - n])>>15 + (int64_t)Vdcmean;
                    current = ((current - (int64_t)Idcmean) * window[MEASUREMENT_COUNT - n])>>15 + (int64_t)Idcmean;
                }

                // integrate values
                Vdc += voltage;
                Idc += current;
                Vms += voltage * voltage;
                Ims += current * current;
                Pms += voltage * current;

                // update graphs
                if (n < GRAPH_COUNT)
                {
                    graph1[n] = voltage;
                    graph2[n] = current;
                }

                n++;

                if (n >= MEASUREMENT_COUNT)
                {
                    Vdcmean += ALPHA*((double)Vdc - Vdcmean);
                    Idcmean += ALPHA*((double)Idc - Idcmean);

                    tmpdouble = Vdcmean;
                    tmpdouble *= Vdcmean / MEASUREMENT_COUNT;
                    tmpdouble = (double)Vms - tmpdouble;
                    if (tmpdouble > 0.0)
                    {
                        Vrms = sqrt(tmpdouble / MEASUREMENT_COUNT) * VSCALE;
                    }
                    else
                    {
                        Vrms = 0.0;
                    }

                    tmpdouble = Idcmean;
                    tmpdouble *= Idcmean;
                    tmpdouble /= (double)MEASUREMENT_COUNT;
                    tmpdouble = (double)Ims - tmpdouble;
                    if (tmpdouble > 0.0)
                    {
                        Irms = sqrt(tmpdouble / (double)MEASUREMENT_COUNT) * ISCALE;
                    }
                    else
                    {
                        Irms = 0.0;
                    }

                    tmpdouble = Vdcmean;
                    tmpdouble *= Idcmean / MEASUREMENT_COUNT;
                    Prms = ((double)Pms  - tmpdouble) / MEASUREMENT_COUNT * VSCALE * ISCALE;

                    VArms = Vrms * Irms;

                    if (TXcount == 0)
                    {
                        if (1==1)
                        {
                        // output RMS Voltage
                        // output Vrms
                        TXData[TXcount++] = 'V';
                        TXData[TXcount++] = 'r';
                        TXData[TXcount++] = 'm';
                        TXData[TXcount++] = 's';
                        TXData[TXcount++] = ':';
                        TXData[TXcount++] = ' ';
                        TXcount += float2digits((float)Vrms, &TXData[TXcount], 5, 2);
                        TXData[TXcount++] = ';';
                        TXData[TXcount++] = ' ';

                        // output RMS Current
                        // output Arms
                        TXData[TXcount++] = 'A';
                        TXData[TXcount++] = 'r';
                        TXData[TXcount++] = 'm';
                        TXData[TXcount++] = 's';
                        TXData[TXcount++] = ':';
                        TXData[TXcount++] = ' ';
                        TXcount += float2digits((float)Irms, &TXData[TXcount], 5, 3);
                        TXData[TXcount++] = ';';
                        TXData[TXcount++] = ' ';

                        // output Wrms
                        TXData[TXcount++] = 'W';
                        TXData[TXcount++] = 'r';
                        TXData[TXcount++] = 'm';
                        TXData[TXcount++] = 's';
                        TXData[TXcount++] = ':';
                        TXData[TXcount++] = ' ';
                        // output RMS Power
                        TXcount += float2digits((float)Prms, &TXData[TXcount], 5, 1);
                        TXData[TXcount++] = ';';
                        TXData[TXcount++] = ' ';

                        // output VA Power
                        // output VArms
                        TXData[TXcount++] = 'V';
                        TXData[TXcount++] = 'A';
                        TXData[TXcount++] = 'r';
                        TXData[TXcount++] = 'm';
                        TXData[TXcount++] = 's';
                        TXData[TXcount++] = ':';
                        TXData[TXcount++] = ' ';
                        TXcount += float2digits((float)VArms, &TXData[TXcount], 5, 1);
                        TXData[TXcount++] = ';';
                        TXData[TXcount++] = ' ';
                        }
                        else
                        {
                            TXData[TXcount++] = '0';
                            TXData[TXcount++] = '1';
                            TXData[TXcount++] = '2';
                            TXData[TXcount++] = '3';
                            TXData[TXcount++] = '4';
                            TXData[TXcount++] = '5';
                            TXData[TXcount++] = '6';
                            TXData[TXcount++] = '7';
                            TXData[TXcount++] = '8';
                            TXData[TXcount++] = '9';
                            TXData[TXcount++] = 'T';
                            TXData[TXcount++] = 'E';
                            TXData[TXcount++] = 'S';
                            TXData[TXcount++] = 'T';
                            TXData[TXcount++] = ' ';
                        }
                        TXpointer = TXData;
                        P1OUT &= ~GPIO_PIN4; //  set P1.4 (GPIO3) to L to enable CS
                        __delay_cycles(36000);                           // Delay ~2ms
                        EUSCI_B_SPI_transmitData(EUSCI_B0_BASE, *TXpointer++);
                    }




                    n = 0;
                    Vms = 0;
                    Ims = 0;
                    Pms = 0;
                    Vdc = 0;
                    Idc = 0;
                }
//                GUIComm_sendInt16("0", STR_LEN_ONE, adcA0Data);     // Send A0 ADC data to PC
//                GUIComm_sendInt16("1", STR_LEN_ONE, adcA1Data);     // Send A1 ADC data to PC
                adcReady = 0;                                       // Clear data ready flag
            }
        }
    }
}

#if defined(__TI_COMPILER_VERSION__) || defined(__IAR_SYSTEMS_ICC__)
#pragma vector=SD24_VECTOR
__interrupt void SD24_ISR(void)
#elif defined(__GNUC__)
void __attribute__ ((interrupt(SD24_VECTOR))) SD24_ISR (void)
#else
#error Compiler not supported!
#endif
{
     int16_t a0, a1;
     uint16_t b0, b1;

    switch (__even_in_range(SD24IV,SD24IV_SD24MEM3)) {
        case SD24IV_NONE: break;
        case SD24IV_SD24OVIFG: break;
        case SD24IV_SD24MEM0: break;
        case SD24IV_SD24MEM1:
            SD24CCTL0 &= ~SD24LSBACC;
            SD24CCTL1 &= ~SD24LSBACC;
            a0 = SD24MEM0; // higher word
            a1 = SD24MEM1; // higher word
            // switch to LSB access
            SD24CCTL0 |= SD24LSBACC;
            SD24CCTL1 |= SD24LSBACC;
            b0 = SD24MEM0; // lower word
            b1 = SD24MEM1; // lower word

            adcA0Data = (uint32_t)a0 << 8;
            adcA0Data += b0 & 0x00ff;       // Save CH0 results

            adcA1Data = (uint32_t)a1 << 8;
            adcA1Data += b1 & 0x00ff;       // Save CH1 results (clears IFG)

            adcReady = 1;                           // Set data ready flag
            break;
        case SD24IV_SD24MEM2: break;
        case SD24IV_SD24MEM3: break;
        default: break;
    }
}

#if defined(__TI_COMPILER_VERSION__) || defined(__IAR_SYSTEMS_ICC__)
#pragma vector=USCI_B0_VECTOR
__interrupt
#elif defined(__GNUC__)
__attribute__((interrupt(USCI_B0_VECTOR)))
#endif
void USCI_B0_ISR(void)
{
    switch(__even_in_range(UCB0IV, USCI_SPI_UCTXIFG))
    {
        case USCI_NONE: break;
        case USCI_SPI_UCRXIFG:
                        // Read what slave sent. Should be same as transferred. If there
                        // is a mismatch turn on the LED
                        RXData = EUSCI_B_SPI_receiveData(EUSCI_B0_BASE);

                        if (--TXcount > 0)
                        {
                            EUSCI_B_SPI_transmitData(EUSCI_B0_BASE, *TXpointer++);
                        }
                        else
                        {
                            P1OUT |= GPIO_PIN4; //  set P1.4 (GPIO3) to H to disable CS
                        }
                        break;
        case USCI_SPI_UCTXIFG:
            break;
        default: break;
    }
}
