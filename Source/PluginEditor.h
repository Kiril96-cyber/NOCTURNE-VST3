#pragma once
#include <JuceHeader.h>
#include "PluginProcessor.h"
#include <functional>

class NocturneMidiDragButton : public juce::Component, private juce::Timer
{
public:
    std::function<juce::File()> createMidiFile;
    std::function<bool(const juce::File&)> performNativeMidiDrag;
    std::function<void(bool)> dragResult;

    NocturneMidiDragButton()
    {
        startTimerHz(30);
    }

    void paint(juce::Graphics& g) override
    {
        const auto bounds = getLocalBounds().toFloat().reduced(0.8f);
        const float pulse = 0.5f + 0.5f * std::sin(animationPhase);
        const auto accent = juce::Colour(0xffbc8cff);

        g.setColour(juce::Colour(0xff17151f).withAlpha(0.96f));
        g.fillRoundedRectangle(bounds, 7.0f);
        g.setColour(accent.withAlpha(0.45f + pulse * 0.48f));
        g.drawRoundedRectangle(bounds, 7.0f, 1.2f + pulse * 0.8f);

        // A small document icon, followed by an animated dotted trail and arrow.
        const float cy = bounds.getCentreY();
        juce::Path fileIcon;
        fileIcon.addRoundedRectangle(11.0f, cy - 9.0f, 13.0f, 18.0f, 2.0f);
        g.setColour(juce::Colour(0xffd9caff).withAlpha(0.88f));
        g.strokePath(fileIcon, juce::PathStrokeType(1.2f));
        g.drawLine(14.0f, cy - 3.0f, 21.0f, cy - 3.0f, 0.9f);
        g.drawLine(14.0f, cy + 1.0f, 21.0f, cy + 1.0f, 0.9f);
        g.drawLine(14.0f, cy + 5.0f, 19.0f, cy + 5.0f, 0.9f);

        const float trailStart = 31.0f;
        const float trailEnd = 54.0f + 3.0f * std::sin(animationPhase * 0.9f);
        g.setColour(accent.withAlpha(0.35f));
        g.drawLine(trailStart, cy, trailEnd, cy, 1.2f);
        for (int i = 0; i < 4; ++i)
        {
            const float x = trailStart + std::fmod(animationPhase * 10.0f + i * 7.0f, 25.0f);
            g.setColour(accent.withAlpha(0.18f + 0.72f * pulse));
            g.fillEllipse(x - 1.6f, cy - 1.6f, 3.2f, 3.2f);
        }
        juce::Path arrow;
        arrow.startNewSubPath(trailEnd - 4.0f, cy - 4.0f);
        arrow.lineTo(trailEnd, cy);
        arrow.lineTo(trailEnd - 4.0f, cy + 4.0f);
        g.setColour(accent.withAlpha(0.75f + pulse * 0.25f));
        g.strokePath(arrow, juce::PathStrokeType(1.5f));

        g.setColour(juce::Colour(0xfff0eaff));
        g.setFont(juce::Font(12.5f, juce::Font::bold));
        g.drawText("DRAG MIDI", 61, 3, getWidth() - 68, 17, juce::Justification::centredLeft);
        g.setColour(juce::Colour(0xffb9accd));
        g.setFont(juce::Font(9.5f));
        g.drawText("HOLD  +  DRAG TO ROLL", 61, 18, getWidth() - 68, 13, juce::Justification::centredLeft);
    }

    void timerCallback() override
    {
        animationPhase += 0.11f;
        if (animationPhase > juce::MathConstants<float>::twoPi)
            animationPhase -= juce::MathConstants<float>::twoPi;
        repaint();
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
        // Prefer a native Shell IDataObject/DoDragDrop implementation when supplied.
        // This gives DAWs a real CF_HDROP file object instead of an internal JUCE drag.
        dragStarted = performNativeMidiDrag
            ? performNativeMidiDrag(midiFile)
            : juce::DragAndDropContainer::performExternalDragDropOfFiles(
                  files, false, this, {});

        if (dragResult)
            dragResult(dragStarted);
    }

private:
    bool dragStarted = false;
    float animationPhase = 0.0f;
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
    void mouseUp(const juce::MouseEvent&) override;
    void mouseWheelMove(const juce::MouseEvent&, const juce::MouseWheelDetails&) override;
    void mouseDoubleClick(const juce::MouseEvent&) override;
    bool keyPressed(const juce::KeyPress&) override;
    void undoEdit();
    void redoEdit();
    void pushUndoState();

private:
    void timerCallback() override;
    void chooseAudio();
    void loadWaveform(const juce::File&);
    void saveMidi();
    void applyPreset(int presetIndex);
    bool isPitchInSelectedScale(int midi) const;
    int snapMidiToSelectedScale(int midi) const;
    juce::Rectangle<float> pianoBounds() const;
    int pianoTopMidi() const;

    NocturneAudioProcessor& processor;
    juce::TextButton importButton { "IMPORT AUDIO" };
    juce::TextButton reanalyzeButton { "RE-ANALYZE" };
    juce::TextButton recordButton { "RECORD" };
    juce::TextButton exportButton { "EXPORT MIDI" };
    juce::TextButton clearButton { "CLEAR" };
    juce::TextButton undoButton { "UNDO" };
    juce::TextButton redoButton { "REDO" };
    NocturneMidiDragButton dragMidiButton;
    NocturnePianoLockButton pianoLockButton;

    juce::ComboBox presetSelector;
    juce::TextButton previousPresetButton, nextPresetButton;
    bool settingsOpen = false;
    float settingsReveal = 0.0f;
    float settingsHint = 0.0f;
    juce::ComboBox keySelector, scaleSelector;
    juce::Label keyLabel, scaleLabel;
    juce::ToggleButton scaleSnapButton { "SNAP" };
    juce::Label presetLabel;
    juce::Slider pianoScrollX, pianoScrollY;
    juce::Label pianoScrollXLabel, pianoScrollYLabel;
    juce::Slider speed, accuracy, glide, vibrato, stability, tempo, inputGate;
    juce::ComboBox quantize, chordMode, analysisMode;
    juce::Label speedLabel, accuracyLabel, glideLabel, vibratoLabel, stabilityLabel, tempoLabel, inputGateLabel;
    juce::Label quantizeLabel, chordModeLabel, analysisModeLabel, pitchLabel, statusLabel;

    std::vector<float> waveform;
    juce::String importedFileName;
    int draggedNote = -1;
    enum class NoteDragMode { none, move, resize, velocity };
    NoteDragMode noteDragMode = NoteDragMode::none;
    double dragOffsetSeconds = 0.0;
    double originalNoteLength = 0.1;
    int velocityNote = -1;
    std::vector<juce::Array<NocturneAudioProcessor::NoteEvent>> undoHistory, redoHistory;
    static constexpr int maxUndoStates = 64;
    float neonPhase = 0.0f;
    float pianoZoom = 1.0f;
    bool middlePanning = false;
    juce::Point<float> panStartPoint;
    double panStartX = 0.0;
    double panStartY = 84.0;

    std::unique_ptr<juce::FileChooser> midiSaveChooser;
    std::unique_ptr<juce::FileChooser> audioFileChooser;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(NocturneAudioProcessorEditor)
};
