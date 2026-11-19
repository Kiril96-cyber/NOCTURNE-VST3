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
        // Use JUCE's native OS file drag with automatic source-window detection.
        // Keep the temporary MIDI file on disk after the drop: some hosts read it
        // asynchronously after the OLE drop callback has returned.
        dragStarted = juce::DragAndDropContainer::performExternalDragDropOfFiles(
            files, false, nullptr, {});

        if (dragResult)
            dragResult(dragStarted);
    }

private:
    bool dragStarted = false;
};

class NocturnePianoLockButton : public juce::Component
{
public:
    std::function<void(bool)> onChange;

    bool isLocked() const noexcept { return locked; }

    void paint(juce::Graphics& g) override
    {
        const float cx = getWidth() * 0.5f;
        const float top = 2.0f;

        juce::Path shackle;
        if (locked)
        {
            shackle.startNewSubPath(cx - 5.0f, top + 9.0f);
            shackle.lineTo(cx - 5.0f, top + 6.5f);
            shackle.cubicTo(cx - 5.0f, top - 0.5f, cx + 5.0f, top - 0.5f, cx + 5.0f, top + 6.5f);
            shackle.lineTo(cx + 5.0f, top + 9.0f);
        }
        else
        {
            shackle.startNewSubPath(cx - 5.0f, top + 9.0f);
            shackle.lineTo(cx - 5.0f, top + 6.5f);
            shackle.cubicTo(cx - 5.0f, top - 0.5f, cx + 5.0f, top - 0.5f, cx + 5.0f, top + 5.0f);
        }

        g.setColour(juce::Colour(0xffb5a2d6));
        g.strokePath(shackle, juce::PathStrokeType(1.8f,
            juce::PathStrokeType::curved, juce::PathStrokeType::rounded));

        const juce::Rectangle<float> body(cx - 7.0f, top + 8.0f, 14.0f, 11.0f);
        g.setColour(locked ? juce::Colour(0xff9c83c7) : juce::Colour(0xff292532));
        g.fillRoundedRectangle(body, 2.3f);
        g.setColour(juce::Colour(0xffe8e3ef));
        g.drawRoundedRectangle(body, 2.3f, 1.0f);
        g.fillEllipse(cx - 1.2f, top + 11.0f, 2.4f, 2.4f);
        g.drawLine(cx, top + 13.0f, cx, top + 16.0f, 1.2f);
    }

    void mouseDown(const juce::MouseEvent&) override
    {
        mousePressed = true;
    }

    void mouseUp(const juce::MouseEvent& event) override
    {
        if (mousePressed && getLocalBounds().toFloat().contains(event.position))
        {
            locked = !locked;
            repaint();
            if (onChange)
                onChange(locked);
        }
        mousePressed = false;
    }

private:
    bool locked = true;
    bool mousePressed = false;
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
    void applyPreset(int presetIndex);
    juce::Rectangle<float> pianoBounds() const;
    int pianoTopMidi() const;

    NocturneAudioProcessor& processor;
    juce::TextButton importButton { "IMPORT AUDIO" };
    juce::TextButton recordButton { "RECORD" };
    juce::TextButton exportButton { "EXPORT MIDI" };
    juce::TextButton copyButton { "COPY NOTES" };
    juce::TextButton clearButton { "CLEAR" };
    NocturneMidiDragButton dragMidiButton;
    NocturnePianoLockButton pianoLockButton;

    juce::ComboBox presetSelector;
    juce::Label presetLabel;
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
