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
int thresholds[8];
int buffers[8][20];
int loop_ct;
bool rainbow;
int lastButtonState; 

void setup(){
  display = new HackathonDisplay();
  display->begin();
  pinMode(7, INPUT);
  rainbow = true;
  lastButtonState = 0;

  Serial.begin(9600);
  Serial.println("serial init");
  Serial.println(FFT_N);
  loop_ct = 0;

  initialize_fft();
  for (int i = 0; i < 8; i++){
    thresholds[i] = 0;
    for (int j = 0; j < 20; j++){
      buffers[i][j] = 0;
    }
  }
}


void loop(){

  capture_and_process_audio();

  int red, blue, green, black, purple, orange, yellow, pink, white;

  red = display->Color333(7, 0, 0);
  green = display->Color333(0, 7, 0);
  blue = display->Color333(0, 0, 7);
  white = display->Color333(7,7,7);
  purple = display->Color333(5,0,7);
  orange = display->Color333(7,3,0);
  yellow = display->Color333(7,6,0);
  pink = display->Color333(7, 2, 2);

  //int colors[] = {red, green, blue, white, purple, orange, yellow, pink};
  
  black = 0;


  int colors[] = {red, orange, yellow, green, blue, purple, pink, white};
  int colors2[] = {

    display->Color333(0,7,0),

    display->Color333(0,6,1),
    display->Color333(0,5,2),
    display->Color333(0,4,3),
    display->Color333(0,3,4),
    display->Color333(0,2,5),
    display->Color333(0,1,6),

    display->Color333(0,0,7)
  };

  //int buckets[8];
  //for (int i = 0; i < 8; i++){buckets[8] = 0;}
  //for (int i = 0; i < 8; i++){
  //  int offset = i*8;
  //  buckets[i] += spectrum[offset+i];
  //}

  //loop_ct++
  //int index = loop_ct % 20;
  //for (int i = 0; i < 8; i++){
  //  buffers[i][loop_ct] = bucket[i];
  //}

  //for (int i = 0; i < 8; i++){
  //  int color = display->Color333(3,i,8-i);
  //  int iMax, iMin;
  //  rain(i, color);
  //}
  int buttonState = digitalRead(7);
  if (buttonState != lastButtonState){

    if (buttonState == HIGH){
      rainbow = !rainbow;
      Serial.print("rainbow: ");
      Serial.println(rainbow);
    }
  }
  lastButtonState = buttonState;

  int bassAmp = spectrum[0] + spectrum[1];
  int bassThresh = 150;

  if(bassAmp > bassThresh){
    int bucketN = bassAmp % 8;
    int color = display->Color333(0,bucketN+1,9-bucketN);
    if(rainbow){
      rain(bucketN, colors2[bucketN]);
    } else {
      rain(bucketN, colors[bucketN]);
    }
  }
}


void rain(int bucket, int color){
  
  int x_start = bucket * 4;
  int x_end = x_start + 4;
  int length = 3;
  for (int y = 0; y < 15; y++){
    for (int x = x_start; x < x_end; x++){
      display->drawLine(x, 0, x, 15, 0);
      display->drawLine(x, y, x, y+length, color);
    }
  }
   
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
