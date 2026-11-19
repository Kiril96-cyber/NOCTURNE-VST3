#pragma once
#include <JuceHeader.h>
#include "PluginProcessor.h"

class NocturneAudioProcessorEditor : public juce::AudioProcessorEditor, private juce::Timer
{
public:
    explicit NocturneAudioProcessorEditor(NocturneAudioProcessor&);
    void paint(juce::Graphics&) override;
    void resized() override;
    void mouseDown(const juce::MouseEvent&) override;
    void mouseDrag(const juce::MouseEvent&) override;

private:
    void timerCallback() override;
    void chooseAudio();
    void loadWaveform(const juce::File&);
    void saveMidi();
    void copyNotes();
    juce::Rectangle<float> pianoBounds() const;

    NocturneAudioProcessor& processor;
    juce::TextButton importButton { "IMPORT AUDIO" };
    juce::TextButton recordButton { "RECORD" };
    juce::TextButton exportButton { "EXPORT MIDI" };
    juce::TextButton copyButton { "COPY NOTES" };
    juce::TextButton clearButton { "CLEAR" };

    juce::Slider speed, accuracy, glide, vibrato, tempo;
    juce::ComboBox quantize;
    juce::Label speedLabel, accuracyLabel, glideLabel, vibratoLabel, tempoLabel;
    juce::Label quantizeLabel, pitchLabel, statusLabel;

    std::vector<float> waveform;
    juce::String importedFileName;
    int draggedNote = -1;
    double dragOffsetSeconds = 0.0;

    std::unique_ptr<juce::FileChooser> midiSaveChooser;
    std::unique_ptr<juce::FileChooser> audioFileChooser;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(NocturneAudioProcessorEditor)
};
