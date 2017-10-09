/*
 * Teensy Sine
 * 
 * Drive a bipolar stepper motor synchronously from a teensy (plus amplifier).
 * Generates two sine waves with 90 degrees of phase separation suitable
 * for driving a stepper at the chosen rpm.
 */

// Settings
#define STEP_ANGLE 7.5 // step angle of your stepper motor
#define LED_PIN LED_BUILTIN // LED pin
#define DAC_PIN_A A21 // first DAC output pin
#define DAC_PIN_B A22 // second DAC output pin
#define ADC_PIN_FREQ A14 // analog input for controlling frequency
#define DAC_RESOLUTION 12 // resolution of the digital-to-analog converters on your board
#define ADC_RESOLUTION 13 // resolution of the analog-to-digital converters on your board
#define SAMPLES_PER_CYCLE 600 // number of discreet points in the generated sine waves; higher values are smoother but more processer intensive; should be a multiple of 4

// stable data structures
IntervalTimer timer; // timer object for the interrupt
bool useSlider = true; // TODO make runtime configurable
int vFreq;
int adcBitRange = pow(2, ADC_RESOLUTION);
float stepperRPM = 250.0; // desired rpm of the stepper shaft TODO make runtime configurable
float sineFrequency = ((360.0 / STEP_ANGLE) * (stepperRPM / 60.0)) / 4.0; // derived frequency (hz) of the sine wave to drive stepper at the requested RPMs TODO make runtime configurable
float interruptMicroseconds = 1000000.0 / (sineFrequency * SAMPLES_PER_CYCLE); // derived microseconds between interrupts to achieve the frequency
uint16_t sineTable[SAMPLES_PER_CYCLE]; // array of sine wave samples
uint32_t waveIndexA = SAMPLES_PER_CYCLE / 4; // index into the sine table for the first wave (90 degrees ahead of the second wave)
uint32_t waveIndexB = 0; // index into the sine table for the second wave

// create the table of individual samples for the discreet sine waves
void createSineTable() {
  float dacBitRange = pow(2.0, DAC_RESOLUTION) - 1;
  for(uint32_t i = 0; i < SAMPLES_PER_CYCLE; i++) {
    // sine sample normalised to the correct bit range
    sineTable[i] = (uint16_t)  (((1 + sin(((2.0*PI) / SAMPLES_PER_CYCLE) * i)) * dacBitRange) / 2);
  }
}

void sample() {
  waveIndexA++;
  waveIndexB++;
  if (waveIndexA > SAMPLES_PER_CYCLE) {
    waveIndexA = 0;
  }
  if (waveIndexB > SAMPLES_PER_CYCLE) {
    waveIndexB = 0;
  }
  analogWrite(DAC_PIN_A, sineTable[waveIndexA]);
  analogWrite(DAC_PIN_B, sineTable[waveIndexB]);
}

void setup() {
  createSineTable();
  pinMode(LED_PIN, OUTPUT);
  pinMode(DAC_PIN_A, OUTPUT);
  pinMode(DAC_PIN_B, OUTPUT);
  pinMode(ADC_PIN_FREQ, INPUT);
  analogWriteResolution(DAC_RESOLUTION);
  analogReadResolution(ADC_RESOLUTION);
  digitalWrite(LED_PIN, HIGH);
  interrupts();
  timer.begin(sample, interruptMicroseconds);
}

void loop() {
  // if in manual mode, read in from the freq pot and update vars
  if (useSlider) {
    vFreq = analogRead(ADC_PIN_FREQ);
    // scale stepper rpm based on vFreq; assuming audio taper pot
    if (vFreq == 0) {
      stepperRPM = 0;
      // TODO disable interrupt
      return;
    }
    stepperRPM = 500.0 * 1.0 / (1.0 - log10((float)vFreq/(float)adcBitRange));
    sineFrequency = ((360.0 / STEP_ANGLE) * (stepperRPM / 60.0)) / 4.0;
    interruptMicroseconds = 1000000.0 / (sineFrequency * SAMPLES_PER_CYCLE);
    timer.end();
    timer.begin(sample, interruptMicroseconds);
  }
}
