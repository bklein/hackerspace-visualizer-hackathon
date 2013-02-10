#include <avr/pgmspace.h>
#include <ffft.h>
#include <math.h>
#include <Wire.h>
#include <Adafruit_GFX.h>     // Core graphics library
#include "HackathonDisplay.h"

HackathonDisplay *display;

// This stuff is all for the FFT, which transforms samples of audio input into
// buckets in a sort of spectrum analyzer (the spectrum[] variable), with each
// bucket representing the sound amplitude at a particular part of the
// frequency range. Each bucket represents about 150 Hz.
// 
// The "host" Arduino (the Arduino that your team brought) does all the audio
// calculation and then sends drawing commands to the "GPU" Arduino, which
// actually drives the real RGB matrix.
#define ADC_CHANNEL 0
int16_t       capture[FFT_N];    // Audio capture buffer
complex_t     bfly_buff[FFT_N];  // FFT "butterfly" buffer
uint16_t      spectrum[FFT_N/2]; // Spectrum output buffer
volatile byte samplePos = 0;     // Buffer position counter

// end FFT stuff

void setup(){
  display = new HackathonDisplay();
  display->begin();
  Serial.begin(9600);

  initialize_fft();
}


void loop(){

  capture_and_process_audio();

  int red, blue, green, black, white;
  red = display->Color333(7, 0, 0);
  green = display->Color333(0, 7, 0);
  blue = display->Color333(0, 0, 7);
  white = display->Color333(7,7,7);
  black = 0;

  int sum = 0;
  for (int i = 0; i < 2; i++){
    sum += spectrum[i];
  }
  sum %= 16;
  int bassAmp = spectrum[0]+spectrum[1];
  int trebleAmp = spectrum[2] + spectrum[3];
  //Serial.print(bassAmp);
  //Serial.print("\t");
  //Serial.println(trebleAmp);

  //Serial.println(bassAmp);
  int bassThresh, trebThresh;
  bassThresh = 150;

  if (bassAmp > bassThresh){
    // draw circle
    int row = bassAmp % 16;
    display->drawLine(0, row, 15, row, green);
    for (int i = 0; i < 15; i++){
      display->drawLine(0, i, 15, i, green);
    }
      
  } else {
    // erase circle
    int row = bassAmp % 16;
    display->drawLine(0, row, 15, row, blue);
  }
  //display->drawCircle(8,4,bassAmp % 5, red);
    
  //for (int row = 0; row < 16; row++){
  //  int color; 
  //  display->drawLine(0, 15, 15, 15, green);
  //  display->drawLine(16, 15, 31, 15, blue);

  //  color = bassAmp == row ? green : black;
  //  display->drawLine(0, row, 15, row, color);

  //  //color = trebleAmp == row ? blue : black;
  //  //display->drawLine(16, row, 31, row, color);
  //  delay(500);
  //}
  
  
  //display->drawCircle(0, 0, bassAmp, green);
  //display->drawCircle(0, 0, bassAmp+1, green);
  //display->drawCircle(0, 0, bassAmp, 0);
  //display->drawCircle(0, 0, bassAmp+1, 0);
  //display->drawCircle(0, 15, trebleAmp, blue);
  //display->drawCircle(0, 15, trebleAmp+1, blue);
  //display->drawCircle(0, 15, trebleAmp, 0);
  //display->drawCircle(0, 15, trebleAmp+1, 0);
  //

  //display->drawCircle(32, 0, bassAmp*2, red);
  //display->drawCircle(32, 0, bassAmp*2+1, red);
  //display->drawCircle(32, 0, bassAmp*2, 0);
  //display->drawCircle(32, 0, bassAmp*2+1, 0);
  //display->drawCircle(15, 7, sum, red);
  //if (sum > 8){
  //  // draw filled in and fade
  //  for (int r = 0; r < sum; r++){
  //    display->drawCircle(16, 7, r, green);
  //  }
  //  for (int r = sum; r < 32; r++){
  //    display->drawCircle(16, 7, r, 0);
  //  }
  //    
  //}
  //delay(1000);
  //display->drawCircle(15, 7, sum, black);
  
  // Reset the screen to black
  //for (int x = 0; x < 32; x++){
  //  display->drawLine(x, 0, x, 15, 0);
  //  //display->drawCircle(0, 0,i , 0);
  //}
  



}


/* ----------------------------------------------------------------------- */
/* --------------------------- ADVANCED STUFF ---------------------------- */
/* ----------------------------------------------------------------------- */
// You don't have to understand the code below this point, but it is pretty
// interesting to see how it all works!



// Tell the CPU to run the ISR(ADC_vect) function (below) until enough samples
// have been collected to fill up the whole capture[] array. Then, process the
// capture[] array into the spectrum[] array.
void capture_and_process_audio() {
  // Sample some audio
  ADCSRA |= _BV(ADIE);             // Resume sampling interrupt
  while(ADCSRA & _BV(ADIE));       // Wait for audio sampling to finish

  // Process the sample
  fft_input(capture, bfly_buff);   // Samples -> complex #s
  samplePos = 0;                   // Reset sample counter
  fft_execute(bfly_buff);          // Process complex data
  fft_output(bfly_buff, spectrum); // Complex -> spectrum

}

// Configure the CPU to automatically grab audio samples whenever we say so.
// After this function runs, the CPU will start collecting audio samples
// whenever the following is called:
// 
//     ADCSRA |= _BV(ADIE);             // Resume sampling interrupt
void initialize_fft() {
  // Init ADC free-run mode; f = ( 16MHz/prescaler ) / 13 cycles/conversion 
  ADMUX  = ADC_CHANNEL; // Channel sel, right-adj, use AREF pin
  ADCSRA = _BV(ADEN)  | // ADC enable
           _BV(ADSC)  | // ADC start
           _BV(ADATE) | // Auto trigger
           _BV(ADIE)  | // Interrupt enable
           _BV(ADPS2) | _BV(ADPS1); // | _BV(ADPS0); // 128:1 / 13 = 9615 Hz
  ADCSRB = 0;                // Free run mode, no high MUX bit
  DIDR0  = 1 << ADC_CHANNEL; // Turn off digital input for ADC pin
  TIMSK0 = 0;                // Timer0 off

  sei(); // Enable interrupts
}

// The CPU will run this little block of code very frequently to fill up the
// capture[] array. The capture[] array will later be processed into the
// spectrum[] array.
ISR(ADC_vect) {
  static const int16_t noiseThreshold = 4;
  int16_t              sample         = ADC; // 0-1023
  
  // This is a custom offset required by the breadboard circuit used at the
  // hackathon. This value is related to the uneven voltage divider with 110K
  // of resistance on one side and 100K on the other.
  sample += 45;

  capture[samplePos] =
    ((sample > (512-noiseThreshold)) &&
     (sample < (512+noiseThreshold))) ? 0 :
    sample - 512; // Sign-convert for FFT; -512 to +511

  if(++samplePos >= FFT_N) ADCSRA &= ~_BV(ADIE); // Buffer full, interrupt off
}

void drawFlag(){
  int red = display->Color333(255, 0, 0);
  int blue = display->Color333(0, 0, 255);
  int white  = display->Color333(255, 255, 255);

  display->drawCircle(5, 7, 7, white);
  //for (int row = 0; row < 16; row+=2){
  //  display->drawLine(0, row, 31, row, blue);
  //  display->drawLine(0, row+1, 31, row+1, red);
  //}




}
