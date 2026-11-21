#pragma once
#include <JuceHeader.h>
#include <array>
#include <limits>

class NocturneAudioProcessor : public juce::AudioProcessor
{
public:
    NocturneAudioProcessor();
    ~NocturneAudioProcessor() override = default;

    void prepareToPlay(double sampleRate, int samplesPerBlock) override;
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

    struct NoteEvent
    {
        int midi = 60;
        double start = 0.0;
        double length = 0.1;
        float confidence = 0.0f;
        int velocity = 100;
    };

    juce::Array<NoteEvent> getNotesCopy() const;
    void clearNotes();
    int analyseFile(const juce::File&);
    int analyseLastAudio();
    void exportMidi(const juce::File&);
    bool copyMidiToClipboard(void* clipboardOwnerWindow);
    juce::File createTemporaryMidiFile();
    void moveNote(int index, int midi, double start);
    void setNoteLength(int index, double length);
    void setNoteVelocity(int index, int velocity);
    void addNote(int midi, double start, double length = 0.25, int velocity = 100);
    void removeNote(int index);
    void replaceNotes(const juce::Array<NoteEvent>& replacement);
    void setRecording(bool shouldRecord);
    std::array<float, 256> getLiveWaveformSnapshot() const;

    std::atomic<float> bpm { 120.0f };
    std::atomic<int> quantizeDivision { 0 };
    std::atomic<float> currentPitchHz { 0.0f };
    std::atomic<float> inputLevel { 0.0f };
    std::atomic<bool> recording { false };
    std::atomic<double> recordingPositionSeconds { 0.0 };
    std::atomic<bool> trackerResetRequested { false };
    std::atomic<int> detectedKeyRoot { -1 };
    std::atomic<int> detectedKeyMode { 0 }; // 0 = major, 1 = minor
    std::atomic<float> detectedKeyConfidence { 0.0f };

    juce::String notesAsText() const;

private:
    struct PitchTrackerState
    {
        int activeMidi = -1;
        int candidateMidi = -1;
        int candidateFrames = 0;
        float smoothedMidi = -1.0f;
        double noteStart = 0.0;
        double candidateStart = 0.0;
        double invalidSince = 0.0;
        bool invalidPending = false;
        std::array<int, 4> activeChordMidi {};
        int activeChordCount = 0;
    };

    struct ChordNotes
    {
        std::array<int, 4> midi {};
        int count = 0;
    };

    struct PitchTrackerEvents
    {
        bool closed = false;
        bool opened = false;
        int closedMidi = -1;
        int openedMidi = -1;
        double closeTime = 0.0;
        double openTime = 0.0;
        float openConfidence = 0.0f;
    };

    static juce::AudioProcessorValueTreeState::ParameterLayout createParams();
    static ChordNotes makeChordNotes(int rootMidi, int mode);
    static PitchTrackerEvents updatePitchTracker(PitchTrackerState&,
                                                   float hz,
                                                   float confidence,
                                                   double time,
                                                   float accuracy,
                                                   float glide,
                                                   float vibrato,
                                                   float stability);
    float detectPitchYin(const float*, int, float& confidence, double analysisRate) const;
    void acceptPitch(float hz, float confidence, float frameRms, double time,
                     int midiSampleOffset, juce::MidiBuffer& midi);
    void finishLiveNote(double time, int midiSampleOffset, juce::MidiBuffer& midi);
    void estimateKey(const juce::AudioBuffer<float>& audio, double analysisRate);
    int analysePolyphonicFile(const juce::AudioBuffer<float>& audio, double analysisRate);
    static float getFrameRms(const float* samples, int numSamples);
    static int velocityFromRms(float rms, float gateDb);

    juce::CriticalSection notesLock;
    juce::Array<NoteEvent> notes;
    juce::AudioBuffer<float> lastImportedAudio;
    double lastImportedSampleRate = 0.0;
    int analyseAudioBuffer(const juce::AudioBuffer<float>& audio, double analysisRate);
    juce::AudioBuffer<float> analysisBuffer;
    juce::AudioBuffer<float> analysisFrame;
    PitchTrackerState liveTracker;

    static constexpr int analysisWindowSize = 2048;
    int writePosition = 0;
    int samplesReceived = 0;
    int samplesSinceAnalysis = 0;
    double sampleRate = 44100.0;
    double elapsedSeconds = 0.0;
    double recordingStartTime = 0.0;
    bool wasRecording = false;

    std::array<std::atomic<float>, 256> liveWaveform {};
    std::atomic<int> liveWaveformWrite { 0 };
    float liveWaveformAccumulator = 0.0f;
    int liveWaveformSamples = 0;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(NocturneAudioProcessor)
};
