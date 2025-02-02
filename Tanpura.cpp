#include "daisy_legio.h"
#include "daisysp.h"
#include "Note.hpp"
#include "formantfilter.cpp"
#include "Gate.cpp"

using namespace daisy;
using namespace daisysp;

#define NUM_STRINGS 4

//VERSION
int version_large = 1;
int version_small = 1;

// Patch.Init hardware objects
DaisyLegio hw;
Switch button, toggle;

float sampleRate;
float max_signal = 1.0;

// Notes
Note notes[NUM_STRINGS];
int current_note, last_current_note = -1;
int semitone, prev_semitone = 0;
int notes_orig_midinum[NUM_STRINGS];
int notes_offset[NUM_STRINGS];
const int base_semitone_offset = 24;

// Default calibration values
const float calib_base = 0.331;
const float calib_units_per_volt = 0.12573;

// Main strings (K-S Pluck)
Pluck strings[NUM_STRINGS];
float string_trig[NUM_STRINGS];
float string_buffer[NUM_STRINGS][1500];
int string_npt = 1500;  
float string_weight;
float tuningOffset = 0.0f;
float tuningOffsetPrev = 0.0f;
float tuningFactor = 0.0f;
float jawari, prevJawari = 0.0f;

//Comb filters
Comb combs[NUM_STRINGS];
float comb_buffer[NUM_STRINGS][1500];
int comb_npt = 1500;

//Formant Filters
FormantFilter formant_filters[NUM_FILTERS];
bool clockState, prevClockState, formant_on;
float formant_duration = 2.0;

//Mixing
float string_mix = 0.3;
float comb_mix= 0.2;
float formant_mix = 0.3;
float now,then = 0;

//low pass filter
bool lpf_on = false;
Svf lpf;

bool triggerState, prevTriggerState = false;

float stepR,stepG,stepB;

// Persistence
struct Settings {
    float tuningOffset;
    bool operator!=(const Settings& a) {
        return a.tuningOffset != tuningOffset;
    }
};

Settings& operator* (const Settings& settings) { return *settings; }
PersistentStorage<Settings> storage(hw.seed.qspi);

void saveData() {
    Settings &localSettings = storage.GetSettings();
    localSettings.tuningOffset = tuningOffset;
    storage.Save();
}

void loadData() {
    Settings &localSettings = storage.GetSettings();
    tuningOffset = localSettings.tuningOffset;
}

void doStep() {

    current_note = current_note + 1;
    if (current_note >= NUM_STRINGS) {
        current_note = 0;
        hw.seed.PrintLine("Looping.");
    };
    string_trig[current_note] = 1.0f;

    hw.seed.PrintLine("Doing step %d note: %s%d midi %d freq %.2f",current_note,notes[current_note].noteName.c_str(),notes[current_note].octave,notes[current_note].noteNumMIDI,notes[current_note].frequency);

    formant_filters[current_note].Reset();
    hw.seed.PrintLine("end step %d",current_note);

};

int processKnobValue(float value, int maxValue) {

    if (value > 1) {value = 1;}
    float voltsPerNum = 1.0/maxValue;
    float rawVal = value/voltsPerNum;
    return std::ceil(rawVal);

}


static void AudioCallback(AudioHandle::InputBuffer  in,
                          AudioHandle::OutputBuffer out,
                          size_t                    size)
{

    hw.ProcessAllControls();
    button.Debounce();
    toggle.Debounce();

    // loop through samples 
    for(size_t c = 0; c < size; c++)
    {

        float currentVoltage = 0;

        // update each string voice and add it to the mix
        for (int i = 0; i < NUM_STRINGS; i++ ) {

            float trig = string_trig[i];
            float stringVoltage = strings[i].Process(trig);
            float stringWeighted = stringVoltage * string_weight;
            if (string_trig[i]) {
                string_trig[i] = 0;
            }
            if (string_trig[0]) {string_trig[0] = 0;}

            float combVoltage = combs[i].Process(stringVoltage);
            float combWeighted = combVoltage * string_weight;
            float formantWeighted;
            if (formant_on) {
                formantWeighted = formant_filters[i].Process(stringVoltage) * string_weight;
            } else {
                formantWeighted = 0;
            }

            float mix = (stringWeighted * string_mix) + (combWeighted * comb_mix) + (formantWeighted * formant_mix * 20);

            currentVoltage += mix;
        };

        float sig_l, sig_r;

        if (lpf_on) {
            lpf.Process(currentVoltage);
            sig_l = sig_r = lpf.Low();
        } else {
            sig_l = sig_r = currentVoltage;
        }

        OUT_L[c] = sig_l; 
        OUT_R[c] = sig_r;
        
    }

    
};


int main(void) {


    // Initialize patch sm hardware and start audio, ADC
    hw.Init(true);
    hw.SetAudioSampleRate(daisy::SaiHandle::Config::SampleRate::SAI_32KHZ);
    hw.seed.StartLog(false);
    hw.StartAdc();

    sampleRate = hw.AudioSampleRate();

    hw.seed.PrintLine("Logging enabled. Sample rate is %.2f",sampleRate);

    //Indicate version by blinking lights
    hw.seed.PrintLine("VERSION %d . %d",version_large,version_small);
    for (int i=0; i < version_small; ++i) {
        hw.seed.PrintLine("Blink %d",i);
        hw.SetLed(0,0,1,0);
        hw.SetLed(1,0,1,0);
        hw.UpdateLeds();
        // Wait 500ms
        System::Delay(500);
        hw.SetLed(0,0,0,0);
        hw.SetLed(1,0,0,0);
        hw.UpdateLeds();
        System::Delay(500);
    }

    //Set up and load persistent settings
    Settings defaults;
    defaults.tuningOffset = 0.0;
    storage.Init(defaults);
    loadData();

    //init lpf
    lpf.Init(sampleRate);
    lpf.SetRes(0.25);
    float freq = 10000;
    lpf.SetFreq(freq);

    //set up light colors
    stepR = 0;
    stepG = 0;
    stepB = 1;

    //Initial read of parameters for first sequence
    hw.ProcessAllControls(); 

    // Default tuning
    notes[0].setNote("D",2);
    notes[1].setNote("A",3);
    notes[2].setNote("A",3);
    notes[3].setNote("A",2);

    notes_orig_midinum[0] = 38;
    notes_orig_midinum[1] = 57;
    notes_orig_midinum[2] = 57;
    notes_orig_midinum[3] = 45;

    notes_offset[0] = 0;
    notes_offset[1] = 19;
    notes_offset[2] = 19;
    notes_offset[3] = 7;

    hw.seed.PrintLine("Clearing buffers....");
    // Clear buffers
    for (int n = 0; n < NUM_STRINGS; ++n) {
        hw.seed.PrintLine("Clearing buffer for string %d",n);
        for (int s = 0; s < string_npt; s++) {
            string_buffer[n][s] = 0;
        }
    }

    // Initialize oscillators
    for (int i = 0; i < NUM_STRINGS; ++i ) {
        strings[i].Init(sampleRate,string_buffer[i],string_npt,daisysp::PLUCK_MODE_RECURSIVE);
        strings[i].SetAmp(1.0f);
        strings[i].SetDecay(1.0f);
        strings[i].SetDamp(1.0f);
        strings[i].SetFreq(notes[i].frequency);
        string_trig[i] = 0.f;

        combs[i].Init(sampleRate,comb_buffer[i],comb_npt);
        combs[i].SetFreq(notes[i].frequency);

    
    };
    for (int i = 0; i < NUM_FILTERS; ++i) {
        formant_filters[i].Init(sampleRate,2,3);
        formant_filters[i].SetDuration(formant_duration);
    }

    string_weight = 1.0f/float(NUM_STRINGS + 1);

    hw.StartAudio(AudioCallback);

    while(1)
    {
        hw.ProcessAllControls(); 

        //process pitch
        prev_semitone = semitone;
        float control_pitch = hw.controls[DaisyLegio::CONTROL_PITCH].Value();
        float volts = (control_pitch - calib_base)/calib_units_per_volt;
        semitone = round(volts * 12);
        if (semitone != prev_semitone) {
            hw.seed.PrintLine("Semitone changed from %d to %d",prev_semitone,semitone);
            for (int i = 0; i < NUM_STRINGS; ++i) {
                notes[i].noteNumMIDI = base_semitone_offset + semitone + notes_offset[i];
                notes[i].noteName = notes[i].getNoteNameFromNum((notes[i].noteNumMIDI % 12) + 1);
                notes[i].setFreq();
                strings[i].SetFreq(notes[i].frequency * tuningOffset);
                combs[i].SetFreq(notes[i].frequency * tuningOffset);
            }
        }

        //change knob from 0-1 to -1 to 1 and raise 2 to that power to get factor
        float encInc = hw.encoder.Increment();
        tuningFactor += hw.encoder.Pressed() ? encInc / 12 : (encInc / 12) * 0.05;
        tuningOffsetPrev = tuningOffset;
        tuningOffset = pow(2,tuningFactor);

        if (tuningOffset != tuningOffsetPrev) {saveData();}

        float freq = hw.GetKnobValue(DaisyLegio::CONTROL_KNOB_BOTTOM) * 4000;
        lpf.SetFreq(freq);

        jawari = hw.GetKnobValue(DaisyLegio::CONTROL_KNOB_TOP);

        comb_mix = jawari * 0.4;
        formant_mix = jawari * 0.4;
        string_mix = 1 - (comb_mix);

        if (hw.sw[DaisyLegio::SW_LEFT].Read() == hw.sw->POS_LEFT) {
            lpf_on = false;
        } else if (hw.sw[DaisyLegio::SW_LEFT].Read() == hw.sw->POS_RIGHT) {
            lpf_on = true;
        } 

        if (hw.sw[DaisyLegio::SW_RIGHT].Read() == hw.sw->POS_LEFT) {
            formant_on = false;
        } else if (hw.sw[DaisyLegio::SW_RIGHT].Read() == hw.sw->POS_RIGHT) {
            formant_on = true;
        } 

        for (int i = 0; i < NUM_STRINGS; ++i) {
            strings[i].SetFreq(notes[i].frequency * tuningOffset);
            combs[i].SetFreq(notes[i].frequency * tuningOffset);
        }

        //check for trigger in
        triggerState = hw.Gate();
        if (triggerState && !prevTriggerState) {
            doStep();
        }
        prevTriggerState = triggerState;

        // Set leds
        stepR = jawari/2;
        stepB = 1 - jawari;
        int on_light = current_note % 2;
        int off_light = (current_note + 1)%2;
        hw.SetLed(on_light,stepR,stepG,stepB);
        hw.SetLed(off_light,0,0,0);
        hw.UpdateLeds();
        
    }
}    




