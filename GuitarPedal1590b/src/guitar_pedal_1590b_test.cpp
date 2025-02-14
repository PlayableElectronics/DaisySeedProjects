#include <string.h>
#include "guitar_pedal_1590b.h"
#include "daisysp.h"

using namespace daisy;
using namespace daisysp;
using namespace bkshepherd;

GuitarPedal1590B hardware;

bool  effectOn = false;
bool relayBypassEnabled = true;
float led2Brightness = 0.0f;

uint32_t lastTimeStampUS;
int samplesSinceEnableToggled;
bool crossFading = false;
bool crossFadingToEffectOn = false;
float crossFadingWetFactor;
float crossFadingDryFactor;
float crossFadingTransitionTimeInSeconds = 0.25f;
int crossFadingTransitionTimeInSamples;

// Effect
Tremolo    treml, tremr;
Oscillator freq_osc;
int  waveform;
float osc_freq;
Parameter osc_freq_knob;

static void AudioCallback(AudioHandle::InputBuffer  in,
                     AudioHandle::OutputBuffer out,
                     size_t                    size)
{
    // Handle Inputs
    hardware.ProcessAnalogControls();
    hardware.ProcessDigitalControls();

    // Handle knobs Tremelo
    float tremFreqMin = 1.0f;
    float tremFreqMax = hardware.knobs[0].Process() * 20.f; //0 - 20 Hz
    treml.SetDepth(hardware.knobs[1].Process());
    tremr.SetDepth(hardware.knobs[1].Value());

    //float w = hardware.knobs[3].Process();
    //int numChoices = Oscillator::WAVE_LAST;
    //waveform = w * numChoices;
    freq_osc.SetWaveform(waveform);
    float knob2Value = osc_freq_knob.Process();
    float freq_osc_min = 0.01f;
    freq_osc.SetFreq(freq_osc_min + (knob2Value * 3.0f)); //0 - 20 Hz

    float mod = freq_osc.Process();

    if (knob2Value < 0.01) {
        mod = 1.0f;
    }

    treml.SetFreq(tremFreqMin + tremFreqMax * mod); 
    tremr.SetFreq(tremFreqMin + tremFreqMax * mod);

    //If the First Footswitch button is pressed, toggle the effect enabled
    bool oldEffectOn = effectOn;
    effectOn ^= hardware.switches[0].RisingEdge();

    if (relayBypassEnabled)
    { 
        hardware.SetAudioBypass(!effectOn);
    }
    else
    {
        hardware.SetAudioBypass(false);
    }
    
    // Handle Effect State being Toggled.
    if (effectOn != oldEffectOn)
    {
        // Setup Effect Crossfade to happen over a specified number of samples based on the time config
        samplesSinceEnableToggled = 0;
        crossFading = true;

        if (effectOn)
        {
            crossFadingToEffectOn = true;
            hardware.seed.PrintLine("Crossfade to EffectOn over %d samples", crossFadingTransitionTimeInSamples);
        }
        else
        {
            crossFadingToEffectOn = false;
            hardware.seed.PrintLine("Crossfade to EffectOff over %d samples", crossFadingTransitionTimeInSamples);
        }
    }

    // Process Audio
    for(size_t i = 0; i < size; i++)
    {
        if (crossFading)
        {
            if (samplesSinceEnableToggled < crossFadingTransitionTimeInSamples)
            {
                float fadeFactor = (float)samplesSinceEnableToggled / (float)crossFadingTransitionTimeInSamples;
                
                if (crossFadingToEffectOn)
                {
                    crossFadingDryFactor = 1.0f - fadeFactor;
                    crossFadingWetFactor = fadeFactor;
                }
                else
                {
                    crossFadingDryFactor = fadeFactor;
                    crossFadingWetFactor = 1.0f - fadeFactor;
                }
            }
            else
            {
                crossFading = false;
            }

            // Tremelo
            led2Brightness = treml.Process(1.0f);
            out[0][i] = (in[0][i] * crossFadingDryFactor) + (in[0][i] * led2Brightness * crossFadingWetFactor);
            out[1][i] = (in[1][i] * crossFadingDryFactor) + (tremr.Process(in[1][i]) * crossFadingWetFactor);

            if (!effectOn)
            {
                led2Brightness = 0.0f;
            }

            // Increment the Sample Count
            samplesSinceEnableToggled += 1;
        }
        else
        {
            out[0][i] = in[0][i];
            out[1][i] = in[1][i];
            led2Brightness = 0.0f;

            if(effectOn)
            {
                // Tremelo
                led2Brightness = treml.Process(1.0f);
                out[0][i] = in[0][i] * led2Brightness;
                out[1][i] = tremr.Process(in[1][i]);
            }
        }
    }

    //LED stuff
    hardware.SetLed((GuitarPedal1590B::LedIndex)0, effectOn);
    hardware.SetLed((GuitarPedal1590B::LedIndex)1, led2Brightness);
    hardware.UpdateLeds();
}

int GetNumberOfSamplesForTime(float time)
{
    // Calculates the number of samples for a specified amount of time in seconds.
    return (int)(hardware.AudioSampleRate() * time);
}

// Typical Switch case for Message Type.
void HandleMidiMessage(MidiEvent m)
{
    switch(m.type)
    {
        case NoteOn:
        {
            NoteOnEvent p = m.AsNoteOn();
            char        buff[512];
            sprintf(buff,
                    "Note Received:\t%d\t%d\t%d\r\n",
                    m.channel,
                    m.data[0],
                    m.data[1]);
            //hareware.seed.usb_handle.TransmitInternal((uint8_t *)buff, strlen(buff));
            // This is to avoid Max/MSP Note outs for now..
            if(m.data[1] != 0)
            {
                p = m.AsNoteOn();
                //led2Brightness = ((float)p.velocity / 127.0f);
                //osc.SetFreq(mtof(p.note));
                //osc.SetAmp((p.velocity / 127.0f));
            }
        }
        break;
        case ControlChange:
        {
            ControlChangeEvent p = m.AsControlChange();
            switch(p.control_number)
            {
                case 1:
                    // CC 1 for cutoff.
                    //filt.SetFreq(mtof((float)p.value));
                    break;
                case 2:
                    // CC 2 for res.
                    //filt.SetRes(((float)p.value / 127.0f));
                    break;
                default: 
                    //led2Brightness = ((float)p.value / 127.0f);
                break;
            }
            break;
        }
        default: break;
    }
}

int main(void)
{
    // Initialize the Hardware
    hardware.Init();
    hardware.SetAudioBlockSize(4);
    hardware.SetAudioSampleRate(SaiHandle::Config::SampleRate::SAI_48KHZ);

    // Set the number of samples to use for the crossfade based on the hardware sample rate
    crossFadingTransitionTimeInSamples = GetNumberOfSamplesForTime(crossFadingTransitionTimeInSeconds);

    // Setup the Tremolo Effect
    float sample_rate = hardware.AudioSampleRate();
    treml.Init(sample_rate);
    tremr.Init(sample_rate);
    treml.SetWaveform(Oscillator::WAVE_SIN);
    tremr.SetWaveform(Oscillator::WAVE_SIN);
    waveform = 0;
    osc_freq = 0.0f;
    freq_osc.Init(sample_rate);
    freq_osc.SetWaveform(waveform);
    freq_osc.SetAmp(1.0f);
    freq_osc.SetFreq(osc_freq);
    osc_freq_knob.Init(hardware.knobs[2], 0.0, 1.0f, Parameter::Curve::EXPONENTIAL);
 
    // Start the Audio Callback
    hardware.StartAdc();
    hardware.StartAudio(AudioCallback);

    // Setup Midi Receiving
    hardware.midi.StartReceive();

    // Send Test Midi Message
    uint8_t midiData[3];
    midiData[0] = 0b10110000;
    midiData[1] = 0b00001010;
    midiData[2] = 0b01111111;
    hardware.midi.SendMessage(midiData, sizeof(uint8_t) * 3);

    // Setup Logging
    hardware.seed.StartLog();
    lastTimeStampUS = System::GetUs();

    while(1)
    {
        // Handle Time
        uint32_t currentTimeStampUS = System::GetUs();
        //uint32_t elapsedTimeStampUS = currentTimeStampUS - lastTimeStampUS;
        lastTimeStampUS = currentTimeStampUS;
        
        // Handle MIDI Events
        hardware.midi.Listen();

        while(hardware.midi.HasEvents())
        {
            HandleMidiMessage(hardware.midi.PopEvent());
        }
    }
}
