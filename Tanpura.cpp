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
Limiter limiter;

GPIO calibrationReference;

float sampleRate;
float max_signal = 1.0;

// Notes
Note notes[NUM_STRINGS];
int current_note, last_current_note = -1;
int semitone, prev_semitone = 0;
int notes_offset[NUM_STRINGS];
const int base_semitone_offset = 24;
int note_dial, note_dial_prev;

// Default calibration values
float calibration_offset = 0.69;
float calibration_coef = -7.65;

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
float comb_buffer[NUM_STRINGS][15000];
int comb_npt = 15000;

//Formant Filters
FormantFilter formant_filters[NUM_FILTERS];
bool clockState, prevClockState;
float formant_duration = 2.0;

//Mixing
float string_mix = 0.3;
float comb_mix= 0.2;
float formant_mix = 0.3;
float formant_mix_amount = 0.5;
float now,then = 0;

//low pass filter
Svf lpf;

float stepR,stepG,stepB;

// Persistence
struct Settings {
    float tuningOffset;
    int firstNoteOffset;
    float tuningFactor;
    bool operator!=(const Settings& a) {
        return a.tuningOffset != tuningOffset;
    }
    float calibration_offset;
    float calibration_coef;
};

Settings& operator* (const Settings& settings) { return *settings; }
PersistentStorage<Settings> storage(hw.seed.qspi);


void saveData() {
    Settings &localSettings = storage.GetSettings();
    localSettings.tuningOffset = tuningOffset;
    localSettings.firstNoteOffset = notes_offset[0];
    localSettings.tuningFactor = tuningFactor;
    localSettings.calibration_coef = calibration_coef;
    localSettings.calibration_offset = calibration_offset;
    storage.Save();
}

void loadData() {
    Settings &localSettings = storage.GetSettings();
    tuningOffset = localSettings.tuningOffset;
    notes_offset[0] = localSettings.tuningOffset;
    tuningFactor = localSettings.tuningFactor;
    calibration_coef = localSettings.calibration_coef;
    calibration_offset = localSettings.calibration_offset;
    hw.seed.PrintLine("Loaded tuningFactor: %.2f Offset: %d",tuningFactor,notes_offset[0]);
}

void AutoCalibrate() {
    hw.seed.PrintLine("Autocalibrating...");
    float lowVal = 0.0;
    float highVal = 0.0;
    float epsilon = 0.01;
    calibrationReference.Write(true);
    for(int i=0; i<100; i++) {
        System::Delay(1);
        lowVal += hw.controls[DaisyLegio::CONTROL_PITCH].GetRawFloat();
        hw.seed.PrintLine("%s: " FLT_FMT(6), "LOWVAL:   ", FLT_VAR(6, hw.controls[DaisyLegio::CONTROL_PITCH].GetRawFloat()));
    }
    lowVal /= 100.0;

    calibrationReference.Write(false);
    for(int i=0; i<100; i++) {
        System::Delay(1);
        highVal += hw.controls[DaisyLegio::CONTROL_PITCH].GetRawFloat();
        hw.seed.PrintLine("%s: " FLT_FMT(6), "HIGHVAL:  ", FLT_VAR(6, hw.controls[DaisyLegio::CONTROL_PITCH].GetRawFloat()));
    }
    highVal /= 100.0;

    calibrationReference.Write(true);

    if(abs(lowVal - highVal) < epsilon) return; // error, something is plugged in

    calibration_coef = 2.5 / (highVal - lowVal);
    calibration_offset = lowVal;
    hw.seed.PrintLine("Highval %.2f Lowval %.2f Coef %.2f Offset %.2f",highVal,lowVal,calibration_coef,calibration_offset);
    saveData();
}


void doStep() {

    current_note = current_note + 1;
    if (current_note >= NUM_STRINGS) {
        current_note = 0;
    };
    string_trig[current_note] = 1.0f;

    formant_filters[current_note].Reset();

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
            formantWeighted = formant_filters[i].Process(stringVoltage) * string_weight;
            float mix = (stringWeighted * string_mix) + (combWeighted * comb_mix) + (formantWeighted * formant_mix * 20);
            currentVoltage += mix;
        };

        limiter.ProcessBlock(&currentVoltage,1,1);

        float sig_l, sig_r;
    
        lpf.Process(currentVoltage);
        sig_l = sig_r = lpf.Low();

        OUT_L[c] = sig_l; 
        OUT_R[c] = sig_r;
        
    }

    
};

float GetVoctValue() {

    float raw = hw.controls[DaisyLegio::CONTROL_PITCH].GetRawFloat();
    float normalized = (raw - calibration_offset) * calibration_coef;
    return normalized;
}


int main(void) {

    // Initialize patch sm hardware and start audio, ADC
    hw.Init(true);
    hw.SetAudioSampleRate(daisy::SaiHandle::Config::SampleRate::SAI_32KHZ);
    hw.seed.StartLog(false);
    hw.StartAdc();

    Pin configPin = daisy::seed::D20;
    calibrationReference.Init(configPin,GPIO::Mode::OUTPUT, GPIO::Pull::NOPULL);
    
    AutoCalibrate();
    
    sampleRate = hw.AudioSampleRate();
    limiter.Init();

    //Indicate version by blinking lights
    for (int i=0; i < version_small; ++i) {
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
    defaults.firstNoteOffset = 7;
    defaults.tuningFactor = 0.0;
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
    notes[0].setNote("G",2);
    notes[1].setNote("C",3);
    notes[2].setNote("C",3);
    notes[3].setNote("C",2);

    notes_offset[0] = 7;
    notes_offset[1] = 12;
    notes_offset[2] = 12;
    notes_offset[3] = 0;

    // Clear buffers
    for (int n = 0; n < NUM_STRINGS; ++n) {
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
        float volts = GetVoctValue();

        semitone = round(volts * 12);
        if (semitone != prev_semitone) {
            hw.seed.PrintLine("Resetting semitone to %d",semitone);
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
        if (encInc) {
            tuningOffsetPrev = tuningOffset;
            tuningOffset = pow(2,tuningFactor);
            if (tuningOffset != tuningOffsetPrev) {
                saveData();
            }
        }

        jawari = hw.GetKnobValue(DaisyLegio::CONTROL_KNOB_TOP);

        // Effects mixing values
        comb_mix = jawari * 0.4;
        formant_mix = jawari * formant_mix_amount;
        string_mix = 1 - (comb_mix);

        if (hw.sw[DaisyLegio::SW_LEFT].Read() == hw.sw->POS_LEFT) {
            //Set semitone of first string
            note_dial_prev = note_dial;
            note_dial = floor(hw.GetKnobValue(DaisyLegio::CONTROL_KNOB_BOTTOM) * 12);
            if (note_dial != note_dial_prev) {
                notes_offset[0] = note_dial;
                notes[0].noteNumMIDI = base_semitone_offset + semitone + notes_offset[0] + 12;
                notes[0].noteName = notes[0].getNoteNameFromNum((notes[0].noteNumMIDI % 12) + 1);

                notes[0].setFreq();
                strings[0].SetFreq(notes[0].frequency * tuningOffset);
                combs[0].SetFreq(notes[0].frequency * tuningOffset);
                saveData();
            }

        } else if (hw.sw[DaisyLegio::SW_LEFT].Read() == hw.sw->POS_RIGHT) {
            //Set filter cutoff
            float freq = hw.GetKnobValue(DaisyLegio::CONTROL_KNOB_BOTTOM) * 4000;
            lpf.SetFreq(freq);
        }  else if (hw.sw[DaisyLegio::SW_LEFT].Read() == hw.sw->POS_CENTER) {
            hw.seed.PrintLine("Switch in center");
        }

        if (hw.sw[DaisyLegio::SW_RIGHT].Read() == hw.sw->POS_LEFT) {
            formant_mix_amount = 0.1;
            
        } else if (hw.sw[DaisyLegio::SW_RIGHT].Read() == hw.sw->POS_RIGHT) {
            formant_mix_amount = 1.0;
        } else if (hw.sw[DaisyLegio::SW_RIGHT].Read() == hw.sw->POS_CENTER) {
            formant_mix_amount = 0.5;
        }


        for (int i = 0; i < NUM_STRINGS; ++i) {
            strings[i].SetFreq(notes[i].frequency * tuningOffset);
            combs[i].SetFreq(notes[i].frequency * tuningOffset);
        }

        if (hw.gate.Trig()) {
            doStep();
        }

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




