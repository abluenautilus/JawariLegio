#include "formantfilter.hpp"

#define NUM_FILTERS 4

// We keep interpolated filter coefficients in SDRAM 
float DSY_SDRAM_BSS fc_interp_[5][192000];
float DSY_SDRAM_BSS bw_interp_[5][192000];
float DSY_SDRAM_BSS gain_interp_[5][192000];

float lerp(float a, float b, float f)
{
    // simple linear interpolation
    return a * (1.0 - f) + (b * f);
}

static float Flip(float x)
{
    return 1 - x;
}

static float Square(float x) {

    return x * x;
}

float EaseIn(float t)
{
    return Flip(Square(Flip(t)));
}

void FormantFilter::Init(float samplerate, int start, int end) {

    res_ = 0.7;
    drive_ = 0.02;
    samplerate_ = samplerate;
    dur_ = 1.0f;
    ttotal_ = samplerate * dur_;
    t_ = 0;
    start_vowel_ = start;
    end_vowel_ = end;
    current_section_ = 0;
    tick_ = 0;
    div_factor_ = 5;

    for (int i = 0; i < NUM_FILTERS; ++i) {

        filterbank[i].Init(samplerate);
        filterbank[i].SetCenterFreq(formant_fc_[i][start_vowel_]);
        filterbank[i].SetFilterBandwidth(formant_bw_[i][start_vowel_]); 

    }
    current_vowel_ = start_vowel_;

    this->SetDuration(1.0);
};


void FormantFilter::SetVowel(int vowel) {
    current_vowel_ = vowel;
    for (int i = 0; i < NUM_FILTERS; ++i) {
        filterbank[i].SetCenterFreq(formant_fc_[i][vowel]);
        filterbank[i].SetFilterBandwidth(formant_bw_[i][start_vowel_]);  
    }
};

float FormantFilter::Process(float input) {

    float output = 0;

    for (int i = 0; i < NUM_FILTERS; ++i) {
        
        float currentpoint = t_/ttotal_;
        if (currentpoint > 1.0) {currentpoint = 1.0;}

        // interpolate between start and end

        //reduce processing load by interpolating less frequently
        float gain = 0.0;
        if (tick_== div_factor_) {
            int val = (int)t_;
            if (t_ > 47999) {val = 47999;}
            float freq = fc_interp_[i][val];
            float bw = bw_interp_[i][val];
            gain = gain_interp_[i][val];
            filterbank[i].SetCenterFreq(freq);
            filterbank[i].SetFilterBandwidth(bw);
        }
        ++tick_;
        if (tick_ > div_factor_) {tick_ = 0;}

        // process filter
        gain = exp(gain/10);
        filterbank[i].Process(input);
        output += filterbank[i].Bandpass() * gain;

    };

    ++t_; 

    return output;

};

void FormantFilter::SetStartEndVowels(int vowela, int vowelb) {

    start_vowel_ = vowela;
    end_vowel_ = vowelb;

}

void FormantFilter::SetDuration(float dur) {
    dur_ = dur;
    ttotal_ = samplerate_ * dur_;

    //interpolate values
    for (int i = 0; i < ttotal_; ++i) {
        float currentpoint = i/ttotal_;
        for (int a = 0; a < NUM_FILTERS; ++a) {
            fc_interp_[a][i] = lerp(formant_fc_[a][start_vowel_], formant_fc_[a][end_vowel_], EaseIn(currentpoint));
            bw_interp_[a][i] = lerp(formant_bw_[a][start_vowel_], formant_bw_[a][end_vowel_], EaseIn(currentpoint));
            gain_interp_[a][i] = lerp(formant_gain_[a][start_vowel_], formant_gain_[a][end_vowel_], EaseIn(currentpoint));
        }
    }

}

void FormantFilter::Reset() {

    t_ = 0;
    current_section_ = 0;

}