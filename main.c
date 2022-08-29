/*
 * main.c
 *
 *  Created on: 7 May 2022
 *      Author: matis
 */


#include "DE1SoC_WM8731/DE1SoC_WM8731.h"
#include "DE1SoC_LT24/DE1SoC_LT24.h"
#include "HPS_Watchdog/HPS_Watchdog.h"

//Array Length Helper
#define ARRAY_LEN(arr) (sizeof(arr)/sizeof(arr[0]))

//Include Floating Point Math Libraries
#include <math.h>

//Debugging Function (same as last lab)
#include <stdlib.h>
void exitOnFail(signed int status, signed int successStatus){
    if (status != successStatus) {
        exit((int)status); //Add breakpoint here to catch failure
    }
}

//Define some useful constants AUDIO
#define F_SAMPLE 48000.0        //Sampling rate of WM8731 Codec (Do not change)
#define PI2      6.28318530718  //2 x Pi      (Apple or Peach?)
#define PI       3.14159265359

// There are four HEX displays attached to the low (first) address.
#define SEVENSEG_N_DISPLAYS_LO 4

// There are two HEX displays attached to the high (second) address.
#define SEVENSEG_N_DISPLAYS_HI 2

//7-Segment displays
volatile unsigned char *sevenseg_base_lo_ptr = (unsigned char *) 0xFF200020;
volatile unsigned char *sevenseg_base_hi_ptr = (unsigned char *) 0xFF200030;


// KEY buttons base address BUTTONS
volatile int *KEY_ptr = (int *)0xFF200050;

// KEY switches base address SWITCH
volatile int *SW_ptr = (int *)0xFF200040;

// KEY LEDs base address RLED
volatile int *RLED_ptr = (int *)0xFF200000;

//Audio output registers
volatile unsigned char* fifospace_ptr;
volatile unsigned int*  audio_left_ptr;
volatile unsigned int*  audio_right_ptr;

//function used to generate a sine wave on the screen
//the rank is the frequency of the sine wave displayed on the screen ( 1 is 1 period on the screen, 2 is 2 periods displayed on the screen, etc...)
//amplitude is the amplitude multiplier
void WavGenDis(int rank, double amplitude) {
	int i;
	double phase = 0;
	//the increment is defined as a function of the rank and the width of the display
	double inc = rank * PI2 / LT24_HEIGHT;
	//Iterates through the display width pixels
	for (i = 0; i <= LT24_HEIGHT; i++) {

		//get the current sine value for the phase
		double sindisplay = (1/amplitude * (LT24_WIDTH/2) * sin(phase));
		//draws a pixel a the generated sine wave output divided by 1.5 to ensure it fits in the display
		LT24_drawPixel(LT24_BLACK, sindisplay/1.5 + 120, i);
		//increments the phase
		phase = phase + inc;

		while (phase >= PI2) {
			phase = phase - PI2;
		}
	 }
}

//Function used to write the numbers to the displays
//The two inputs are for the display number (0-5) and the number to be displayed (0-9)
void SevenSegWrite(unsigned int display, unsigned char value) {

	//The numbers are ordered in this array from 0-9 in the same position as the array index number
	//They are stored as their decimal values corresponding to the 8 bit value that controls the 7 segments
	unsigned char NumberArray[10] = {63, 6, 91, 79, 102, 109, 125, 7, 127, 103};

	//Sorting the display into either the high or low address that handles the displays
	if (display < SEVENSEG_N_DISPLAYS_LO) {
	        // If we are targeting a low address, use byte addressing to access
	        // directly.
	        sevenseg_base_lo_ptr[display] = NumberArray[value];
	    } else {
	        // If we are targeting a high address, shift down so byte addressing
	        // works.
	        display = display - SEVENSEG_N_DISPLAYS_LO;
	        sevenseg_base_hi_ptr[display] = NumberArray[value];
	    }
}

//Function used to set the frequency to the displays
void SetSegments(unsigned int value) {
	char digit1;
	char digit2;
	//changing the unit number to the actual number of the first displays in the pairs
	int unitdisplay = 0;

	//individual values to be displayed are extracted using the  division operator to get the left-most digit and the modulo operator to get the rest of the digits for each digit
	//The display and values are then sent to the SevenSegWrite() function to be displayed
	if (value >= 10000) {
		//straight division gives the left most digit
		char digit5 = value / 10000;
		//modulus operator outputs the rest of the number
		value = value % 10000;
		SevenSegWrite(unitdisplay + 4, digit5);
	} else {
		//the screen needs to be set to 0 when there isn't a nth digit
		char digit5 = 0;
		SevenSegWrite(unitdisplay + 4, digit5);
	}
	if (value >= 1000) {
		char digit4 = value / 1000;
		value = value % 1000;
		SevenSegWrite(unitdisplay + 3, digit4);
	} else {
		char digit4 = 0;
		SevenSegWrite(unitdisplay + 3, digit4);
	}
	if (value >= 100) {
		char digit3 = value / 100;
		value = value % 100;
		SevenSegWrite(unitdisplay + 2, digit3);
	} else {
		char digit3 = 0;
		SevenSegWrite(unitdisplay + 2, digit3);
	}
	digit1 = value %  10;
	digit2 = value / 10;
	SevenSegWrite(unitdisplay, digit1);
	SevenSegWrite((unitdisplay + 1), digit2);

}

//Sine approximation function that returns an approximation of the sin() function
//Bhaskara I approximation
double Bhaskara(double input) {
	double output = (16 * input * (PI - input))/ ((5*(PI * PI)) - (4 * input * (PI - input)));
	return output;
}

//
// Main Function
// =============
int main(void) {

	//INITIALIZE THE VARIABLES
	//sine parameter variables
    double freq = 440.0;
    double ampl = 8888608000.0;
    double phase[10] = {0.0, 0.0, 0.0, 0.0, 0.0, 0.0, 0.0, 0.0, 0.0, 0.0};
    double waveheight = LT24_WIDTH/2;
    double incr[10] = {0.0, 0.0, 0.0, 0.0, 0.0, 0.0, 0.0, 0.0, 0.0, 0.0};
    double SWAmp[10] = {1,2,3,4,5,6,7,8,9,10};

    //counter variables
    int i;
    int j;
    int sum;

    //output variables
    signed int audio_sample;
    signed int sindisplay;

    //flags
    int flag = 1;
    int prev_SW = -1;
    int SWFlag[10];



    //Initialise the Audio Codec.
    exitOnFail(
            WM8731_initialise(0xFF203040),  //Initialise Audio Codec
            WM8731_SUCCESS);                //Exit if not successful


    //Clear both FIFOs
    WM8731_clearFIFO(true,true);


    //Grab the FIFO Space and Audio Channel Pointers
    fifospace_ptr = WM8731_getFIFOSpacePtr();
    audio_left_ptr = WM8731_getLeftFIFOPtr();
    audio_right_ptr = WM8731_getRightFIFOPtr();


    //Initialise the LCD Display.
	exitOnFail(
			LT24_initialise(0xFF200060,0xFF200080), //Initialise LCD
			LT24_SUCCESS);                          //Exit if not successful
	HPS_ResetWatchdog();
	//Display Internal Test Pattern
	exitOnFail(
			LT24_testPattern(), //Generate test pattern
			LT24_SUCCESS);      //Exit if not successful
	HPS_ResetWatchdog();
	//Wait a moment
	usleep(500000);
	HPS_ResetWatchdog();

	//reset 7 segment displays
	SetSegments(freq);

    // Primary function while loop
    while (1) {

    	//FREQUENCY MODIFICATION
    	//change audio signal flag
    	if (*KEY_ptr & 0x8) {
    		//clear the FIFO to ensure none of the previous inputs interfere
    		WM8731_clearFIFO(true,true);
    		//change the frequency
    		freq = freq * 1.1;
    		//update the 7 segment display
    		SetSegments(freq);
    		//set the audio signal flag to 1 to indicate frequency change
    		flag = 1;

    	//same as above but for frequency reduction
    	} else if (*KEY_ptr & 0x4) {
    		WM8731_clearFIFO(true,true);
    		freq = freq / 1.1;
    		SetSegments(freq);
    		flag = 1;
    	}



    	// AUDIO GENERATION
    	//ensure the switch was not moved recently
    	//ensures the sound only plays when thereisn't going to be interferance
		if (*SW_ptr == prev_SW) {

			//check that the FIFO isn't full
			if ((fifospace_ptr[2] > 0) && (fifospace_ptr[3] > 0)) {

				//calculate the increment based off the frequency if the frequency change flag is up
				if (flag == 1) {
					//calculate new increment for all sine waves
					for (i = 0; i < ARRAY_LEN(SWFlag); i++) {
						incr[i] = ((i + 1) * freq) * PI2 / F_SAMPLE;
					}

					// reset the phase for all sine waves
					for (i = 0; i < ARRAY_LEN(SWFlag); i++) {
						phase[i] = 0.0;
					}
					flag = 0;
				}

				//generate sine waves
				//amplitude is controller using the same amp modifier as the screen wave generator to ensure it's coherent
				//use of sine approximation function for speed
				audio_sample = ((ampl * 1/SWAmp[0]) * Bhaskara(phase[0]));

				// iterate through the waves and add them to the audio signal
				for (j = 1; j < ARRAY_LEN(SWFlag); j++) {
					//SWFlag[] used as a multiplier to make sure the off waves are not added to the sound output
					audio_sample += (SWFlag[j] * (ampl * 1/SWAmp[j]) * Bhaskara(phase[j]));
				}

				//increment the phase of each wave
				for (j = 0; j < ARRAY_LEN(SWFlag); j++) {
					phase[j] = phase[j] + incr[j];
					//Ensure phase is wrapped to range 0 to 2Pi (range of sin function)
					while (phase[j] >= PI2) {
						phase[j] = phase[j] - PI2;
					}
				}
					//add the audio data to the output registers
					*audio_left_ptr = audio_sample;
					*audio_right_ptr = audio_sample;
			}
		}

    	//DISPLAY OUTPUT + SWITCH LED control
		//display output is tied to the switch change detection flag and the button register to only refresh the display when it needs to be changed
    	if (*SW_ptr != prev_SW | *KEY_ptr != 0) {
    		//clear the display
    		LT24_clearDisplay(LT24_WHITE);
    		flag = 1;
    		//for each switch turn the flag corresponding to the wave/switch if it is on
    		//turn on/off the correct LED based on switch state
    		if (*SW_ptr & 0x200) {
    			*RLED_ptr = *RLED_ptr | (1 << 9);
    			SWFlag[0] = 1;
    		} else {
    			*RLED_ptr = *RLED_ptr & ~(1 << 9);
    			SWFlag[0] = 0;
    		}

    		if (*SW_ptr & 0x100) {
    			*RLED_ptr = *RLED_ptr | (1 << 8);
    			SWFlag[1] = 1;
    		} else {
    			*RLED_ptr = *RLED_ptr & ~(1 << 8);
    			SWFlag[1] = 0;
    		}

    		if (*SW_ptr & 0x80) {
    			*RLED_ptr = *RLED_ptr | (1 << 7);
    			SWFlag[2] = 1;
    		} else {
    			*RLED_ptr = *RLED_ptr & ~(1 << 7);
    			SWFlag[2] = 0;
    		}

    		if (*SW_ptr & 0x40) {
    			*RLED_ptr = *RLED_ptr | (1 << 6);
    			SWFlag[3] = 1;
    		} else {
    			*RLED_ptr = *RLED_ptr & ~(1 << 6);
    			SWFlag[3] = 0;
    		}

    		if (*SW_ptr & 0x20) {
    			*RLED_ptr = *RLED_ptr | (1 << 5);
    			SWFlag[4] = 1;
    		} else {
    			*RLED_ptr = *RLED_ptr & ~(1 << 5);
    			SWFlag[4] = 0;
    		}

    		if (*SW_ptr & 0x10) {
    			*RLED_ptr = *RLED_ptr | (1 << 4);
    			SWFlag[5] = 1;
    		} else {
    			*RLED_ptr = *RLED_ptr & ~(1 << 4);
    			SWFlag[5] = 0;
    		}

    		if (*SW_ptr & 0x8) {
    			*RLED_ptr = *RLED_ptr | (1 << 3);
    			SWFlag[6] = 1;
    		} else {
    			*RLED_ptr = *RLED_ptr & ~(1 << 3);
    			SWFlag[6] = 0;
    		}

    		if (*SW_ptr & 0x4) {
    			*RLED_ptr = *RLED_ptr | (1 << 2);
    			SWFlag[7] = 1;
    		} else {
    			*RLED_ptr = *RLED_ptr & ~(1 << 2);
    			SWFlag[7] = 0;
    		}

    		if (*SW_ptr & 0x2) {
    			*RLED_ptr = *RLED_ptr | (1 << 1);
    			SWFlag[8] = 1;
    		} else {
    			*RLED_ptr = *RLED_ptr & ~(1 << 1);
    			SWFlag[8] = 0;
    		}

    		if (*SW_ptr & 0x1) {
    			*RLED_ptr = *RLED_ptr | (1 << 0);
    			SWFlag[9] = 1;
    		} else {
    			*RLED_ptr = *RLED_ptr & ~(1 << 0);
    			SWFlag[9] = 0;
    		}


    		//BUTTON LOGIC
    		// increase or decrease the amplitude multiplier if buttons are pressed
    		if (*KEY_ptr & 0x2) {
    			//the amplitude for all the sines need to be changed
    			for (i = 1; i < ARRAY_LEN(SWFlag); i++) {
    				if (SWFlag[i] == 1) {
    					//check if the amplitude multiplier is already at 0
    					if (SWAmp[i] != 0) {
        					SWAmp[i]--;
    					}
    				}
    			}
    		} else if (*KEY_ptr & 0x1) {
    			for (i = 1; i < ARRAY_LEN(SWFlag); i++) {
					if (SWFlag[i] == 1) {
						SWAmp[i]++;

					}
				}
    		}


    		//SINE WAVE DISPLAY ON SCREEN
    		//check if the 1st switch is up because it indicates whether the display is set to seperate sine waves or joined view
    		//1st switch flag =1 means seperate sine waves
    		if (SWFlag[0] == 1) {
    			//Always generates 1st Sine because it can't be disabled
    			WavGenDis(1, SWAmp[0]);
    			//iterates through the rest of the sine waves and prints them to the screen
    			for (i = 1; i < ARRAY_LEN(SWFlag); i++) {
    				// if flag is up print the wave
    				if (SWFlag[i] == 1) {
    					//rank has +1 because array is counted from 0
    					WavGenDis(i+1, SWAmp[i]);
    				}
    			}

    		} else {
    			//similar code to the audio sine generation using the display width/height as paramters instead of the reference frequency and the amplitude
    			sindisplay = 0;
    			//get increments for all wave
    			for (i = 0; i < ARRAY_LEN(SWFlag); i++) {
    				incr[i] = (i + 1) * PI2 / LT24_HEIGHT;
    			}


    			//reset the phase counters
    			for (i = 0; i < ARRAY_LEN(SWFlag); i++) {
    				phase[i] = 0.0;
    			}

    			//iterate through x axis
    			for (i = 0; i <= LT24_HEIGHT; i++) {
    				//get y value for each wave and add them
    				sum = 0;
    				sindisplay = ((waveheight * 1/SWAmp[0]) * sin(phase[0]));
    				for (j = 0; j < ARRAY_LEN(SWFlag); j++) {
    					sindisplay = sindisplay + (SWFlag[j] * (waveheight * 1/SWAmp[j]) * sin(phase[j]));
    					//find the amount of sine waves blended together
    					sum = sum + (SWFlag[j] * (1/SWAmp[j]));
    				}
    				//normalize the output using the sum so it fits on the screen
    				sindisplay = sindisplay / (sum + 1);

    				//draw the points for each x
    				LT24_drawPixel(LT24_BLACK, sindisplay/2 + 120, i);

    				//increment phase
    				for (j = 0; j < ARRAY_LEN(SWFlag); j++) {
    					phase[j] = phase[j] + incr[j];
    		            //Ensure phase is wrapped to range 0 to 2Pi (range of sin function)
    					while (phase[j] >= PI2) {
    						phase[j] = phase[j] - PI2;
    					}
    				}
    			}
    		}
    		//reset the switch flag
    		prev_SW = *SW_ptr;
    	}
        //Finally reset the watchdog.
        HPS_ResetWatchdog();
    }
}

