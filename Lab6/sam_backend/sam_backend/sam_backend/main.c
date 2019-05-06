/*
 * sam_backend.c
 *
 * Created: 12/7/2018 5:18:33 PM
 * Author : Kaveh Pezeshki and Chris Ferrarin
 * 
 * MCU backend for Lab 6. Generates webpages as requested by the ESP8266, and interfaces with the onboard PIO and ADC peripherals to allow user control of an LED and to display the current time.
 * 
 */ 


#include "SAM4S4BDeviceDriver/SAM4S4B.h"
#include <string.h>
#include <stdlib.h>

#define LED_PIN PIO_PA18
#define CSB PIO_PA8
#define BUFF_LEN 32

/////////////////////////////////////////////////////////////////
// Measured Quantities
/////////////////////////////////////////////////////////////////
volatile double temp = 0; // Global variable in which the current temperature is stored
volatile double press = 0; // Global variable in which the current pressure is stored
volatile char id = 0; // Global variable in which the current BMP280 chip ID is stored
volatile float light = 0; // Global variable in which the current phototransistor voltage is stored

/////////////////////////////////////////////////////////////////
// Provided Constants and Functions
/////////////////////////////////////////////////////////////////
long signed int t_fine = 0; // Global variable which convertTemp() sets and convertPress() uses

//Defining the web page in two chunks: everything before the current time, and everything after the current time
//Please see the e155 website for a human-readable version of the file "webpage.html"
const char* webpageStart = "<!DOCTYPE html><html><head><title>E155 Web Server Demo Webpage</title><meta http-equiv=\"refresh\" content=\"5\"></head><body><h1>E155 Web Server Demo Webpage</h1>";
const char* ledStr = "<p>LED Control:</p><form action=\"ledon\"><input type=\"submit\" value=\"Turn the LED on!\" /></form> <form action=\"ledoff\"><input type=\"submit\" value=\"Turn the LED off!\" /></form>";
const char* webpageEnd   = "</body></html>";

void sendString(char* str) {
	char* ptr = str;
	while (*ptr) uartTx(*ptr++);
}

//determines whether a given character sequence is in a char array request, returning 1 if present, -1 if not present
int inString(char request[], char des[]) {
	if (strstr(request, des) != NULL) {return 1;}
	return -1;
}

//determines if tags "REQ:" and "/REQ" are in the input string
int requestInString(char request[]) {
	int tag1InString = inString(request, "\n");
	if(tag1InString > 0) {
		return 1;
	}
	return -1;
}

// Returns temperature in DegC, double precision. Output value of �51.23� equals 51.23 DegC.
// t_fine carries fine temperature as global value
volatile double convertTemp (volatile char msb, volatile char lsb, volatile char xlsb) {
	volatile signed long adc_T = (msb << 12) | (lsb << 4) | xlsb;
	volatile double var1, var2, T;
	var1 = (((double) adc_T)/16384.0 - ((double) 27504)/1024.0) * ((double) 26435);
	var2 = ((((double )adc_T)/131072.0 - ((double) 27504)/8192.0) *
	(((double) adc_T)/131072.0 - ((double) 27504)/8192.0)) * ((double) (-1000));
	t_fine = (long signed int) (var1 + var2);
	T = (var1 + var2) / 5120.0;
	return T;
}

// Returns pressure in Pa as double. Output value of �96386.2� equals 96386.2 Pa = 963.862 hPa
volatile double convertPress (volatile char msb, volatile char lsb, volatile char xlsb) {
	volatile signed long adc_P = (msb << 12) | (lsb << 4) | xlsb;
	volatile double var1, var2, p;
	var1 = ((double) t_fine/2.0) - 64000.0;
	var2 = var1 * var1 * ((double) (-7)) / 32768.0;
	var2 = var2 + var1 * ((double) 140) * 2.0;
	var2 = (var2/4.0)+(((double) 2855) * 65536.0);
	var1 = (((double) 3024) * var1 * var1 / 524288.0 + ((double) (-10685)) * var1) / 524288.0;
	var1 = (1.0 + var1 / 32768.0)*((double) 36477);
	if (var1 == 0.0) return 0; // Avoid exception caused by division by zero
	p = 1048576.0 - (double) adc_P;
	p = (p - (var2 / 4096.0)) * 6250.0 / var1;
	var1 = ((double) 6000) * p * p / 2147483648.0;
	var2 = p * ((double) (-14600)) / 32768.0;
	p = p + (var1 + var2 + ((double) 15500)) / 16.0;
	return p;
}

int main(void)
{
    //Initialize peripheral clocks
	samInit();
	pioInit();
	tcInit();
	tcDelayInit();
    adcInit(ADC_MR_LOWRES_BITS_10);
	adcChannelInit(ADC_CH4, ADC_CGR_GAIN_X1, ADC_COR_OFFSET_OFF); // Using ADC channel 4 (on pin PIO_PB0)

    //Initialize GPIO pin  for LED control
	pioPinMode(LED_PIN, PIO_OUTPUT);
	pioDigitalWrite(LED_PIN, PIO_HIGH);

    // Initialize CSB pin for SPI with the BMP280
	pioPinMode(CSB, PIO_OUTPUT);
	pioDigitalWrite(CSB, PIO_HIGH); // Either force the pin high initially or delay long enough for the pull-up resistor to respond
	
    //Initialize UART with no parity, 9600 baud
	//The peripheral clock is 4MHz, and we provide a clock divider factor CD such that baud rate = MCK/(16*CD)
	//Therefore we need a clock divide coefficient of 26, which provides a baud rate of 9615 baud. This 0.1% error is accounted for by the receiving UART
	uartInit(4, 13);

    // Initialize SPI. Divide the clock by 10 since the maximum frequency which the BMP280 supports
    // is 10 MHz. Use in (1, 1) mode, since this is the only mode the BMP280 supports. (The BMP280
    // should also support (0, 0) mode, but this did not work in testing.)
    spiInit(20, 1, 1);
	
    // Initialize BMP280
	pioDigitalWrite(CSB, PIO_LOW);
	spiSendReceive16(0x742F); // osrs_t = 001, osrs_p = 011, mode = 11
	spiSendReceive16(0x7510); // t_sb = 000, filter = 100, [0], spi3w_en = 0
	pioDigitalWrite(CSB, PIO_HIGH);
	
	// Confirm chip ID
	pioDigitalWrite(CSB, PIO_LOW);
	spiSendReceive(0xD0);
	id = spiSendReceive(0);
	pioDigitalWrite(CSB, PIO_HIGH);
	
    while (1) 
    {
		// Wait for the ESP8266 to send a request. Requests take the form 'REQ:<tag>/REQ', with TAG being <= 10 characters. Therefore the request[] array must be able to contain 18 characters
		pioDigitalWrite(CSB, PIO_LOW);
		spiSendReceive(0xF7);
		volatile char press_msb = spiSendReceive(0);
		volatile char press_lsb = spiSendReceive(0);
		volatile char press_xlsb = spiSendReceive(0);
		volatile char temp_msb = spiSendReceive(0);
		volatile char temp_lsb = spiSendReceive(0);
		volatile char temp_xlsb = spiSendReceive(0);
		pioDigitalWrite(CSB, PIO_HIGH);
		
		temp = convertTemp(temp_msb, temp_lsb, temp_xlsb);
		press = convertPress(press_msb, press_lsb, press_xlsb);
        light = adcRead(ADC_CH4);
		
		char request[BUFF_LEN] = "                  "; //initializing to a known value
		int  charIndex = 0;
		while (requestInString(request) == -1) {
			// Loop if we run out of space
			if (charIndex >= BUFF_LEN) charIndex = 0;
			
			//wait for a complete request to be transmitted before processing
			while (!uartRxReady());
			request[charIndex++] = uartRx();
		}
		
		//the request has been received. now process to determine whether to turn the LED on or off
		if (inString(request, "ledoff")==1) {
			pioDigitalWrite(LED_PIN, PIO_HIGH);
		}
		if (inString(request, "ledon")==1) {
			pioDigitalWrite(LED_PIN, PIO_LOW);
		}
		
		
		char temperature[20];
		char temperaturept2[20];
		char pressure[20];
		itoa(temp, temperature, 10);
		itoa((temp*10)-((int)temp)*10, temperaturept2, 10);
		itoa(press, pressure, 10);
		
		//finally, transmit the webpage over UART
		//transmitting the first section of the webpage
		sendString(webpageStart);
		
		sendString(ledStr);
		
		sendString("<p>Current Temperature:</p>");
		sendString(temperature);
		sendString(".");
		sendString(temperaturept2);
		sendString(" C");
		
		sendString("<p>Current pressure:</p>");
		sendString(pressure);
		sendString(" Pa");
		
		sendString(webpageEnd);		
    }
}
