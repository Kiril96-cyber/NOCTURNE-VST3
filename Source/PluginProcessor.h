#pragma once
#include <JuceHeader.h>

class NocturneAudioProcessor : public juce::AudioProcessor
{
public:
    NocturneAudioProcessor();
    ~NocturneAudioProcessor() override = default;
    void prepareToPlay(double, int) override;
    void releaseResources() override {}
    void processBlock(juce::AudioBuffer<float>&, juce::MidiBuffer&) override;
    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }
    const juce::String getName() const override { return "NOCTURNE"; }
    bool acceptsMidi() const override { return true; }
    bool producesMidi() const override { return true; }
    bool isMidiEffect() const override { return false; }
    double getTailLengthSeconds() const override { return 0.0; }
    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram(int) override {}
    const juce::String getProgramName(int) override { return {}; }
    void changeProgramName(int, const juce::String&) override {}
    void getStateInformation(juce::MemoryBlock&) override {}
    void setStateInformation(const void*, int) override {}

    juce::AudioProcessorValueTreeState parameters;
    struct NoteEvent { int midi; double start; double length; float confidence; };
    juce::Array<NoteEvent> getNotesCopy() const;
    void clearNotes();
    void analyseFile(const juce::File&);
    void exportMidi(const juce::File&);
    void moveNote(int index, int midi, double start);
    std::atomic<float> bpm { 120.0f };
    std::atomic<int> quantizeDivision { 0 };
    juce::String notesAsText() const;
    std::atomic<float> currentPitchHz { 0.0f };
    std::atomic<float> inputLevel { 0.0f };
    std::atomic<bool> recording { false };

private:
    static juce::AudioProcessorValueTreeState::ParameterLayout createParams();
    float detectPitchYin(const float*, int, float& confidence) const;
    void acceptPitch(float hz, float confidence, double time, int midiSampleOffset, juce::MidiBuffer& midi);
    juce::CriticalSection notesLock;
    juce::Array<NoteEvent> notes;
    juce::AudioBuffer<float> analysisBuffer;
    int filled = 0;
    double sampleRate = 44100.0, elapsed = 0.0, noteStart = 0.0;
    int activeMidi = -1, hopCounter = 0;
    float lastMidiFloat = -1.0f;
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(NocturneAudioProcessor)
};
