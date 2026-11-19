#pragma once
#include <JuceHeader.h>
#include "PluginProcessor.h"
#include <functional>

class NocturneMidiDragButton : public juce::Component
{
public:
    std::function<juce::File()> createMidiFile;
    std::function<void(bool)> dragResult;

    void paint(juce::Graphics& g) override
    {
        const auto bounds = getLocalBounds().toFloat().reduced(0.5f);
        g.setColour(isMouseOver() ? juce::Colour(0xff30283e) : juce::Colour(0xff191923));
        g.fillRoundedRectangle(bounds, 6.0f);
        g.setColour(juce::Colour(0xff9c83c7));
        g.drawRoundedRectangle(bounds, 6.0f, 1.0f);
        g.setColour(juce::Colour(0xffe8e3ef));
        g.setFont(juce::Font(13.0f));
        g.drawText("DRAG MIDI", getLocalBounds(), juce::Justification::centred);
    }

    void mouseDown(const juce::MouseEvent&) override
    {
        dragStarted = false;
    }

    void mouseDrag(const juce::MouseEvent& event) override
    {
        if (dragStarted || event.getDistanceFromDragStart() < 6 || !createMidiFile)
            return;

        const auto midiFile = createMidiFile();
        if (!midiFile.existsAsFile())
        {
            if (dragResult)
                dragResult(false);
            return;
        }

        juce::StringArray files;
        files.add(midiFile.getFullPathName());
        dragStarted = juce::DragAndDropContainer::performExternalDragDropOfFiles(
            files, false, this, [midiFile] { midiFile.deleteFile(); });

        if (!dragStarted)
            midiFile.deleteFile();
        if (dragResult)
            dragResult(dragStarted);
    }

private:
    bool dragStarted = false;
};

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
    int pianoTopMidi() const;

    NocturneAudioProcessor& processor;
    juce::TextButton importButton { "IMPORT AUDIO" };
    juce::TextButton recordButton { "RECORD" };
    juce::TextButton exportButton { "EXPORT MIDI" };
    juce::TextButton copyButton { "COPY NOTES" };
    juce::TextButton clearButton { "CLEAR" };
    NocturneMidiDragButton dragMidiButton;

    juce::Slider speed, accuracy, glide, vibrato, stability, tempo, inputGate;
    juce::ComboBox quantize, chordMode, analysisMode;
    juce::Label speedLabel, accuracyLabel, glideLabel, vibratoLabel, stabilityLabel, tempoLabel, inputGateLabel;
    juce::Label quantizeLabel, chordModeLabel, analysisModeLabel, pitchLabel, statusLabel;

    std::vector<float> waveform;
    juce::String importedFileName;
    int draggedNote = -1;
    double dragOffsetSeconds = 0.0;

    std::unique_ptr<juce::FileChooser> midiSaveChooser;
    std::unique_ptr<juce::FileChooser> audioFileChooser;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(NocturneAudioProcessorEditor)
};
