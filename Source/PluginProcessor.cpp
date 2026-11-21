#include "PluginProcessor.h"
#include "PluginEditor.h"
#include <cstring>

#if JUCE_WINDOWS
 #ifndef NOMINMAX
  #define NOMINMAX
 #endif
 #include <windows.h>
#endif

NocturneAudioProcessor::NocturneAudioProcessor()
    : AudioProcessor(BusesProperties()
          .withInput("Input", juce::AudioChannelSet::stereo(), true)
          .withOutput("Output", juce::AudioChannelSet::stereo(), true)),
      parameters(*this, nullptr, "PARAMETERS", createParams())
{
}

void NocturneAudioProcessor::getStateInformation(juce::MemoryBlock& destData)
{
    auto state = parameters.copyState();
    state.setProperty("selectedPresetIndex", selectedPresetIndex.load(), nullptr);
    if (auto xml = state.createXml())
        copyXmlToBinary(*xml, destData);
}

void NocturneAudioProcessor::setStateInformation(const void* data, int sizeInBytes)
{
    if (auto xml = getXmlFromBinary(data, sizeInBytes))
    {
        const auto restored = juce::ValueTree::fromXml(*xml);
        if (restored.isValid() && restored.hasType(parameters.state.getType()))
        {
            parameters.replaceState(restored);
            selectedPresetIndex.store(juce::jlimit(0, 9,
                (int)restored.getProperty("selectedPresetIndex", 0)));
        }
    }
}

juce::AudioProcessorValueTreeState::ParameterLayout NocturneAudioProcessor::createParams()
{
    std::vector<std::unique_ptr<juce::RangedAudioParameter>> p;
    p.push_back(std::make_unique<juce::AudioParameterFloat>("speed", "Read speed", 10.0f, 100.0f, 45.0f));
    p.push_back(std::make_unique<juce::AudioParameterFloat>("accuracy", "Note accuracy", 0.50f, 0.93f, 0.75f));
    p.push_back(std::make_unique<juce::AudioParameterFloat>("glide", "Pitch glide", 0.0f, 100.0f, 35.0f));
    p.push_back(std::make_unique<juce::AudioParameterFloat>("vibrato", "Vibrato tolerance", 0.0f, 100.0f, 55.0f));
    p.push_back(std::make_unique<juce::AudioParameterFloat>("stability", "Transition stability", 0.0f, 100.0f, 35.0f));
    p.push_back(std::make_unique<juce::AudioParameterFloat>("inputGateDb", "Input gate (dB)", -60.0f, -6.0f, -42.0f));
    p.push_back(std::make_unique<juce::AudioParameterFloat>("confirmationDelayMs", "Note confirmation delay (ms)", 0.0f, 200.0f, 0.0f));
    p.push_back(std::make_unique<juce::AudioParameterChoice>(
        "analysisMode", "Analysis mode", juce::StringArray { "VOCAL", "POLYPHONIC" }, 0));
    p.push_back(std::make_unique<juce::AudioParameterChoice>(
        "chordMode", "Chord mode",
        juce::StringArray { "OFF", "MAJOR", "MINOR", "DIMINISHED", "AUGMENTED", "SUS2", "SUS4" }, 0));
    return { p.begin(), p.end() };
}

void NocturneAudioProcessor::prepareToPlay(double sr, int)
{
    sampleRate = juce::jmax(8000.0, sr);
    analysisBuffer.setSize(1, analysisWindowSize);
    analysisBuffer.clear();
    analysisFrame.setSize(1, analysisWindowSize);
    analysisFrame.clear();

    const int recordingCacheCapacity = (int)juce::jmin(120.0 * sampleRate, 12.0 * 1024.0 * 1024.0);
    lastRecordingAudio.setSize(1, recordingCacheCapacity);
    lastRecordingAudio.clear();
    lastRecordingSamples.store(0);
    lastRecordingReady.store(false);
    latestAudioIsRecording.store(false);

    writePosition = 0;
    samplesReceived = 0;
    samplesSinceAnalysis = 0;
    elapsedSeconds = 0.0;
    recordingStartTime = 0.0;
    wasRecording = false;
    liveTracker = {};
    currentPitchHz.store(0.0f);
    inputLevel.store(0.0f);
    recordingPositionSeconds.store(0.0);
    for (auto& value : liveWaveform)
        value.store(0.0f);
    liveWaveformWrite.store(0);
    liveWaveformAccumulator = 0.0f;
    liveWaveformSamples = 0;
}

float NocturneAudioProcessor::detectPitchYin(const float* x, int n, float& confidence, double analysisRate) const
{
    confidence = 0.0f;
    if (x == nullptr || n < 512 || analysisRate < 8000.0)
        return 0.0f;

    const int minLag = juce::jmax(2, (int)(analysisRate / 1000.0));
    const int maxLag = juce::jmin(n / 2 - 1, (int)(analysisRate / 65.0));
    if (maxLag <= minLag)
        return 0.0f;

    // Fixed-size scratch buffers avoid heap allocation in the audio callback.
    // Remove DC and normalise each frame before calculating YIN differences.
    // This keeps the detector numerically stable when the DAW input is very hot.
    std::array<float, analysisWindowSize + 1> difference {};
    std::array<float, analysisWindowSize + 1> cumulativeMean {};
    std::array<float, analysisWindowSize> normalised {};
    double mean = 0.0;
    for (int i = 0; i < n; ++i)
        mean += (double)x[i];
    mean /= (double)n;

    double rmsSquared = 0.0;
    for (int i = 0; i < n; ++i)
    {
        const float centred = x[i] - (float)mean;
        normalised[(size_t)i] = centred;
        rmsSquared += (double)centred * (double)centred;
    }
    const float rms = (float)std::sqrt(rmsSquared / (double)n);
    if (rms < 0.0007f)
        return 0.0f;

    const float normaliseGain = 0.20f / juce::jmax(1.0e-6f, rms);
    for (int i = 0; i < n; ++i)
        normalised[(size_t)i] = juce::jlimit(-2.5f, 2.5f, normalised[(size_t)i] * normaliseGain);

    const int compareCount = n - maxLag;
    for (int tau = 1; tau <= maxLag; ++tau)
    {
        float sum = 0.0f;
        for (int i = 0; i < compareCount; ++i)
        {
            const float d = normalised[(size_t)i] - normalised[(size_t)(i + tau)];
            sum += d * d;
        }
        difference[(size_t)tau] = sum;
    }

    double runningSum = 0.0;
    cumulativeMean[0] = 1.0f;
    for (int tau = 1; tau <= maxLag; ++tau)
    {
        runningSum += difference[(size_t)tau];
        cumulativeMean[(size_t)tau] = runningSum > 1.0e-12
            ? (float)((double)difference[(size_t)tau] * (double)tau / runningSum)
            : 1.0f;
    }

    constexpr float yinThreshold = 0.16f;
    int bestLag = -1;
    for (int tau = minLag; tau < maxLag; ++tau)
    {
        if (cumulativeMean[(size_t)tau] < yinThreshold)
        {
            while (tau + 1 < maxLag &&
                   cumulativeMean[(size_t)(tau + 1)] < cumulativeMean[(size_t)tau])
                ++tau;
            bestLag = tau;
            break;
        }
    }

    // If no early trough passes the threshold, accept only a strong global minimum.
    if (bestLag < 0)
    {
        bestLag = minLag;
        for (int tau = minLag + 1; tau <= maxLag; ++tau)
            if (cumulativeMean[(size_t)tau] < cumulativeMean[(size_t)bestLag])
                bestLag = tau;
        if (cumulativeMean[(size_t)bestLag] > 0.30f)
            return 0.0f;
    }

    confidence = juce::jlimit(0.0f, 1.0f, 1.0f - cumulativeMean[(size_t)bestLag]);
    if (confidence < 0.50f)
        return 0.0f;

    float subSampleOffset = 0.0f;
    if (bestLag > minLag && bestLag < maxLag)
    {
        const float a = cumulativeMean[(size_t)bestLag - 1];
        const float b = cumulativeMean[(size_t)bestLag];
        const float c = cumulativeMean[(size_t)bestLag + 1];
        const float denominator = a - 2.0f * b + c;
        if (std::abs(denominator) > 1.0e-6f)
            subSampleOffset = 0.5f * (a - c) / denominator;
    }

    const float hz = (float)(analysisRate / ((double)bestLag + (double)subSampleOffset));
    return (hz >= 45.0f && hz <= 1200.0f) ? hz : 0.0f;
}

NocturneAudioProcessor::ChordNotes NocturneAudioProcessor::makeChordNotes(int rootMidi, int mode)
{
    static const int intervals[7][4] = {
        { 0, -1, -1, -1 }, // Off
        { 0, 4, 7, -1 },   // Major
        { 0, 3, 7, -1 },   // Minor
        { 0, 3, 6, -1 },   // Diminished
        { 0, 4, 8, -1 },   // Augmented
        { 0, 2, 7, -1 },   // Suspended second
        { 0, 5, 7, -1 }    // Suspended fourth
    };

    ChordNotes result;
    const int selected = juce::jlimit(0, 6, mode);
    for (int i = 0; i < 4 && intervals[selected][i] >= 0; ++i)
    {
        const int note = rootMidi + intervals[selected][i];
        if (note <= 127)
            result.midi[(size_t)result.count++] = note;
    }
    return result;
}

NocturneAudioProcessor::PitchTrackerEvents NocturneAudioProcessor::updatePitchTracker(
    PitchTrackerState& state, float hz, float confidence, double time,
    float accuracy, float glide, float vibrato, float stability, float confirmationDelayMs)
{
    PitchTrackerEvents events;

    const float requiredConfidence = juce::jlimit(0.50f, 0.96f, accuracy);
    const bool valid = hz > 0.0f && confidence >= requiredConfidence;

    if (!valid)
    {
        state.candidateMidi = -1;
        state.candidateFrames = 0;
        if (state.activeMidi >= 0)
        {
            if (!state.invalidPending)
            {
                state.invalidPending = true;
                state.invalidSince = time;
            }
            // A short dropout or consonant must not split a sustained vowel.
            const double releaseSeconds = 0.045 + (double)vibrato * 0.00045;
            if (time - state.invalidSince >= releaseSeconds)
            {
                events.closed = true;
                events.closedMidi = state.activeMidi;
                events.closeTime = state.invalidSince;
                state.activeMidi = -1;
                state.smoothedMidi = -1.0f;
                state.invalidPending = false;
            }
        }
        return events;
    }

    state.invalidPending = false;
    const float rawMidi = 69.0f + 12.0f * std::log2(hz / 440.0f);
    const float smoothing = juce::jlimit(0.0f, 0.75f, glide * 0.0030f + vibrato * 0.0020f);
    if (state.smoothedMidi < 0.0f)
        state.smoothedMidi = rawMidi;
    else
        state.smoothedMidi = state.smoothedMidi * smoothing + rawMidi * (1.0f - smoothing);

    // Use the unsmoothed pitch for transition decisions. Smoothing remains useful
    // for the display, but should not delay short syllables or quick melodic notes.
    const int candidate = juce::jlimit(0, 127, (int)std::round(rawMidi));
    const int confirmations = juce::jlimit(2, 6, 2 + (int)std::round(stability * 0.035f));
    const double minimumSwitchHold = 0.035 + (double)stability * 0.00065;

    if (state.activeMidi >= 0 && std::abs(rawMidi - (float)state.activeMidi) < 0.58f)
    {
        state.candidateMidi = -1;
        state.candidateFrames = 0;
        return events;
    }

    if (candidate != state.candidateMidi)
    {
        state.candidateMidi = candidate;
        state.candidateFrames = 1;
        state.candidateStart = time;
    }
    else
    {
        ++state.candidateFrames;
    }

    if (state.candidateFrames < confirmations ||
        time - state.candidateStart < (double)juce::jlimit(0.0f, 200.0f, confirmationDelayMs) * 0.001)
        return events;
    if (state.activeMidi >= 0 && time - state.noteStart < minimumSwitchHold)
        return events;

    if (state.activeMidi >= 0)
    {
        events.closed = true;
        events.closedMidi = state.activeMidi;
        events.closeTime = state.candidateStart;
    }

    state.activeMidi = candidate;
    state.noteStart = state.candidateStart;
    state.candidateMidi = -1;
    state.candidateFrames = 0;

    events.opened = true;
    events.openedMidi = candidate;
    events.openTime = state.noteStart;
    events.openConfidence = confidence;
    return events;
}

void NocturneAudioProcessor::setRecording(bool shouldRecord)
{
    recording.store(shouldRecord);
}

void NocturneAudioProcessor::finishLiveNote(double time, int midiSampleOffset, juce::MidiBuffer& midi)
{
    if (liveTracker.activeMidi >= 0)
    {
        const int count = liveTracker.activeChordCount > 0 ? liveTracker.activeChordCount : 1;
        juce::ScopedLock lock(notesLock);
        for (int i = 0; i < count; ++i)
        {
            const int noteMidi = liveTracker.activeChordCount > 0
                ? liveTracker.activeChordMidi[(size_t)i] : liveTracker.activeMidi;
            midi.addEvent(juce::MidiMessage::noteOff(1, noteMidi), midiSampleOffset);
            for (int n = notes.size() - 1; n >= 0; --n)
            {
                auto& note = notes.getReference(n);
                if (note.midi == noteMidi && note.start == liveTracker.noteStart)
                {
                    note.length = juce::jmax(0.04, time - note.start);
                    break;
                }
            }
        }
    }
    liveTracker = {};
}

void NocturneAudioProcessor::acceptPitch(float hz, float confidence, float frameRms,
                                          double time, int midiSampleOffset, juce::MidiBuffer& midi)
{
    const float accuracy = parameters.getRawParameterValue("accuracy")->load();
    const float glide = parameters.getRawParameterValue("glide")->load();
    const float vibrato = parameters.getRawParameterValue("vibrato")->load();
    const float stability = parameters.getRawParameterValue("stability")->load();
    const float confirmationDelayMs = parameters.getRawParameterValue("confirmationDelayMs")->load();
    const int chordMode = (int)std::round(parameters.getRawParameterValue("chordMode")->load());

    const auto previousChord = liveTracker.activeChordMidi;
    const int previousChordCount = liveTracker.activeChordCount;
    const double previousNoteStart = liveTracker.noteStart;
    const int previousRoot = liveTracker.activeMidi;

    const auto events = updatePitchTracker(liveTracker, hz, confidence, time,
                                            accuracy, glide, vibrato, stability, confirmationDelayMs);

    if (events.closed)
    {
        const int count = previousChordCount > 0 ? previousChordCount : 1;
        juce::ScopedLock lock(notesLock);
        for (int i = 0; i < count; ++i)
        {
            const int noteMidi = previousChordCount > 0
                ? previousChord[(size_t)i] : previousRoot;
            midi.addEvent(juce::MidiMessage::noteOff(1, noteMidi), midiSampleOffset);
            for (int n = notes.size() - 1; n >= 0; --n)
            {
                auto& note = notes.getReference(n);
                if (note.midi == noteMidi && note.start == previousNoteStart)
                {
                    note.length = juce::jmax(0.04, events.closeTime - note.start);
                    break;
                }
            }
        }
    }

    if (events.opened)
    {
        const auto chord = makeChordNotes(events.openedMidi, chordMode);
        liveTracker.activeChordMidi = chord.midi;
        liveTracker.activeChordCount = chord.count;
        juce::ScopedLock lock(notesLock);
        const float gateDb = parameters.getRawParameterValue("inputGateDb")->load();
        const int velocity = velocityFromRms(frameRms, gateDb);
        for (int i = 0; i < chord.count; ++i)
        {
            const int noteMidi = chord.midi[(size_t)i];
            midi.addEvent(juce::MidiMessage::noteOn(1, noteMidi, (juce::uint8)velocity), midiSampleOffset);
            notes.add({ noteMidi, events.openTime, 0.06, events.openConfidence, velocity });
        }
    }
    else if (events.closed)
    {
        liveTracker.activeChordCount = 0;
    }

    if (liveTracker.activeMidi >= 0)
    {
        juce::ScopedLock lock(notesLock);
        for (int chordIndex = 0; chordIndex < liveTracker.activeChordCount; ++chordIndex)
        {
            const int noteMidi = liveTracker.activeChordMidi[(size_t)chordIndex];
            for (int n = notes.size() - 1; n >= 0; --n)
            {
                auto& note = notes.getReference(n);
                if (note.midi == noteMidi && note.start == liveTracker.noteStart)
                {
                    note.length = juce::jmax(0.04, time - note.start);
                    break;
                }
            }
        }
    }
}

void NocturneAudioProcessor::processBlock(juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midi)
{
    juce::ScopedNoDenormals noDenormals;
    const int numSamples = buffer.getNumSamples();
    const int channels = buffer.getNumChannels();

    float blockPeak = 0.0f;
    for (int channel = 0; channel < channels; ++channel)
        blockPeak = juce::jmax(blockPeak, buffer.getMagnitude(channel, 0, numSamples));
    inputLevel.store(blockPeak);

    const bool isRecording = recording.load();
    if (isRecording && !wasRecording)
    {
        recordingStartTime = elapsedSeconds;
        liveTracker = {};
        recordingPositionSeconds.store(0.0);
        lastRecordingSamples.store(0);
        lastRecordingReady.store(false);
        latestAudioIsRecording.store(true);
    }
    else if (!isRecording && wasRecording)
    {
        finishLiveNote(elapsedSeconds - recordingStartTime, 0, midi);
        recordingPositionSeconds.store(juce::jmax(0.0, elapsedSeconds - recordingStartTime));
        lastRecordingReady.store(lastRecordingSamples.load() > 0);
    }
    wasRecording = isRecording;

    if (trackerResetRequested.exchange(false))
    {
        if (liveTracker.activeMidi >= 0)
        {
            const int count = liveTracker.activeChordCount > 0 ? liveTracker.activeChordCount : 1;
            for (int i = 0; i < count; ++i)
            {
                const int noteMidi = liveTracker.activeChordCount > 0
                    ? liveTracker.activeChordMidi[(size_t)i] : liveTracker.activeMidi;
                midi.addEvent(juce::MidiMessage::noteOff(1, noteMidi), 0);
            }
        }
        liveTracker = {};
    }

    const float speed = parameters.getRawParameterValue("speed")->load();
    const int hopSize = juce::jlimit(512, 1024,
        (int)std::round(1024.0 - (speed - 10.0f) * (512.0f / 90.0f)));

    if (channels > 0)
    {
        for (int i = 0; i < numSamples; ++i)
        {
            // A vocal may be routed to either side of a stereo mixer insert.
            // Analyse a mono fold-down instead of silently listening to channel 0 only.
            float monoSample = 0.0f;
            for (int channel = 0; channel < channels; ++channel)
                monoSample += buffer.getSample(channel, i);
            monoSample /= (float)channels;

            // Store signed samples in a short rolling buffer for a true live waveform.
            liveWaveformAccumulator += monoSample;
            if (++liveWaveformSamples >= 32)
            {
                const int waveformIndex = liveWaveformWrite.fetch_add(1) % (int)liveWaveform.size();
                liveWaveform[(size_t)waveformIndex].store(
                    juce::jlimit(-1.0f, 1.0f, liveWaveformAccumulator / (float)liveWaveformSamples));
                liveWaveformAccumulator = 0.0f;
                liveWaveformSamples = 0;
            }

            if (isRecording)
            {
                const int cachePosition = lastRecordingSamples.load();
                if (cachePosition < lastRecordingAudio.getNumSamples())
                {
                    lastRecordingAudio.setSample(0, cachePosition, monoSample);
                    lastRecordingSamples.store(cachePosition + 1);
                }
            }

            analysisBuffer.setSample(0, writePosition, monoSample);
            writePosition = (writePosition + 1) % analysisWindowSize;
            samplesReceived = juce::jmin(analysisWindowSize, samplesReceived + 1);
            ++samplesSinceAnalysis;

            elapsedSeconds += 1.0 / sampleRate;

            if (samplesReceived == analysisWindowSize && samplesSinceAnalysis >= hopSize)
            {
                samplesSinceAnalysis = 0;
                const float* source = analysisBuffer.getReadPointer(0);
                float* frame = analysisFrame.getWritePointer(0);
                for (int j = 0; j < analysisWindowSize; ++j)
                    frame[j] = source[(writePosition + j) % analysisWindowSize];

                const float frameRms = getFrameRms(frame, analysisWindowSize);
                const float gateDb = parameters.getRawParameterValue("inputGateDb")->load();
                const float gateRms = std::pow(10.0f, gateDb / 20.0f);
                float confidence = 0.0f;
                const float hz = frameRms >= gateRms
                    ? detectPitchYin(frame, analysisWindowSize, confidence, sampleRate) : 0.0f;
                currentPitchHz.store(hz);

                if (isRecording)
                {
                    const double position = elapsedSeconds - recordingStartTime;
                    recordingPositionSeconds.store(position);
                    acceptPitch(hz, confidence, frameRms, position, i, midi);
                }
            }
        }
    }
}

juce::Array<NocturneAudioProcessor::NoteEvent> NocturneAudioProcessor::getNotesCopy() const
{
    juce::ScopedLock lock(notesLock);
    return notes;
}

void NocturneAudioProcessor::clearNotes()
{
    juce::ScopedLock lock(notesLock);
    notes.clear();
    trackerResetRequested.store(true);
}


float NocturneAudioProcessor::getFrameRms(const float* samples, int numSamples)
{
    if (samples == nullptr || numSamples <= 0)
        return 0.0f;

    double sum = 0.0;
    for (int i = 0; i < numSamples; ++i)
        sum += (double)samples[i] * (double)samples[i];

    return (float)std::sqrt(sum / (double)numSamples);
}

int NocturneAudioProcessor::velocityFromRms(float rms, float gateDb)
{
    if (rms <= 0.0f)
        return 1;

    const float levelDb = 20.0f * std::log10(juce::jmax(1.0e-7f, rms));
    const float lowDb = juce::jlimit(-60.0f, -12.0f, gateDb);
    const float highDb = -3.0f;
    const float normalised = juce::jlimit(0.0f, 1.0f, (levelDb - lowDb) / (highDb - lowDb));
    const float curved = std::pow(normalised, 0.72f);
    return juce::jlimit(1, 127, 1 + (int)std::round(curved * 126.0f));
}

void NocturneAudioProcessor::estimateKey(const juce::AudioBuffer<float>& audio, double analysisRate)
{
    detectedKeyRoot.store(-1);
    detectedKeyMode.store(0);
    detectedKeyConfidence.store(0.0f);

    constexpr int fftOrder = 12;
    constexpr int fftSize = 1 << fftOrder;
    constexpr int firstMidi = 36;
    constexpr int lastMidi = 96;
    constexpr int keyHop = 1024;

    if (audio.getNumSamples() < fftSize || analysisRate < 8000.0)
        return;

    juce::dsp::FFT fft(fftOrder);
    std::vector<float> fftData((size_t)fftSize * 2, 0.0f);
    std::array<float, fftSize> window {};
    for (int i = 0; i < fftSize; ++i)
        window[(size_t)i] = 0.5f - 0.5f * std::cos(
            juce::MathConstants<float>::twoPi * (float)i / (float)(fftSize - 1));

    std::array<double, 12> chroma {};
    const float* samples = audio.getReadPointer(0);
    const int totalSamples = audio.getNumSamples();
    const float gateDb = parameters.getRawParameterValue("inputGateDb")->load();
    const float gateRms = std::pow(10.0f, gateDb / 20.0f);

    auto peakNear = [&](float frequency) -> float
    {
        if (frequency <= 0.0f || frequency >= (float)(analysisRate * 0.48))
            return 0.0f;
        const int centre = (int)std::round((double)frequency * (double)fftSize / analysisRate);
        float peak = 0.0f;
        for (int offset = -1; offset <= 1; ++offset)
        {
            const int bin = centre + offset;
            if (bin >= 1 && bin < fftSize / 2)
                peak = juce::jmax(peak, fftData[(size_t)bin]);
        }
        return peak;
    };

    for (int centreSample = 0; centreSample < totalSamples; centreSample += keyHop)
    {
        std::fill(fftData.begin(), fftData.end(), 0.0f);
        for (int i = 0; i < fftSize; ++i)
        {
            const int sourceIndex = centreSample + i - fftSize / 2;
            if (sourceIndex >= 0 && sourceIndex < totalSamples)
                fftData[(size_t)i] = samples[sourceIndex] * window[(size_t)i];
        }

        const int frameStart = juce::jlimit(0, juce::jmax(0, totalSamples - fftSize),
                                            centreSample - fftSize / 2);
        const float frameRms = getFrameRms(samples + frameStart, fftSize);
        if (frameRms < gateRms)
            continue;

        fft.performFrequencyOnlyForwardTransform(fftData.data());

        std::array<double, 12> frameChroma {};
        double frameTotal = 0.0;
        for (int midi = firstMidi; midi <= lastMidi; ++midi)
        {
            const float fundamental = 440.0f * std::pow(2.0f, (float)(midi - 69) / 12.0f);
            const double salience =
                (double)peakNear(fundamental) +
                0.45 * (double)peakNear(fundamental * 2.0f) +
                0.22 * (double)peakNear(fundamental * 3.0f) +
                0.12 * (double)peakNear(fundamental * 4.0f);
            frameChroma[(size_t)(midi % 12)] += salience;
            frameTotal += salience;
        }

        if (frameTotal > 1.0e-9)
            for (int pitchClass = 0; pitchClass < 12; ++pitchClass)
                chroma[(size_t)pitchClass] += frameChroma[(size_t)pitchClass] / frameTotal;
    }

    double chromaTotal = 0.0;
    for (const auto value : chroma)
        chromaTotal += value;
    if (chromaTotal <= 1.0e-9)
        return;

    static constexpr double majorProfile[12] =
        { 6.35, 2.23, 3.48, 2.33, 4.38, 4.09, 2.52, 5.19, 2.39, 3.66, 2.29, 2.88 };
    static constexpr double minorProfile[12] =
        { 6.33, 2.68, 3.52, 5.38, 2.60, 3.53, 2.54, 4.75, 3.98, 2.69, 3.34, 3.17 };

    double chromaMean = chromaTotal / 12.0;
    double chromaVariance = 0.0;
    for (const auto value : chroma)
        chromaVariance += (value - chromaMean) * (value - chromaMean);
    if (chromaVariance <= 1.0e-12)
        return;

    int bestRoot = -1;
    int bestMode = 0;
    double bestCorrelation = -2.0;
    double secondCorrelation = -2.0;

    for (int mode = 0; mode < 2; ++mode)
    {
        const double* profile = mode == 0 ? majorProfile : minorProfile;
        double profileMean = 0.0;
        for (int i = 0; i < 12; ++i)
            profileMean += profile[i];
        profileMean /= 12.0;

        double profileVariance = 0.0;
        for (int i = 0; i < 12; ++i)
        {
            const double delta = profile[i] - profileMean;
            profileVariance += delta * delta;
        }

        for (int root = 0; root < 12; ++root)
        {
            double covariance = 0.0;
            for (int pitchClass = 0; pitchClass < 12; ++pitchClass)
            {
                const int profileIndex = (pitchClass - root + 12) % 12;
                covariance += (chroma[(size_t)pitchClass] - chromaMean) *
                              (profile[profileIndex] - profileMean);
            }

            const double correlation = covariance /
                std::sqrt(chromaVariance * juce::jmax(1.0e-12, profileVariance));

            if (correlation > bestCorrelation)
            {
                secondCorrelation = bestCorrelation;
                bestCorrelation = correlation;
                bestRoot = root;
                bestMode = mode;
            }
            else if (correlation > secondCorrelation)
            {
                secondCorrelation = correlation;
            }
        }
    }

    if (bestRoot >= 0 && bestCorrelation > 0.12)
    {
        detectedKeyRoot.store(bestRoot);
        detectedKeyMode.store(bestMode);
        detectedKeyConfidence.store((float)juce::jlimit(
            0.0, 1.0, (bestCorrelation - juce::jmax(-1.0, secondCorrelation)) * 3.0));
    }
}

int NocturneAudioProcessor::analysePolyphonicFile(const juce::AudioBuffer<float>& audio, double analysisRate)
{
    constexpr int fftOrder = 12;
    constexpr int fftSize = 1 << fftOrder;
    constexpr int hopSize = 256;
    constexpr int firstMidi = 36; // C2
    constexpr int lastMidi = 96;  // C7
    constexpr int maxVoicesPerFrame = 5;

    if (audio.getNumSamples() < fftSize || analysisRate < 8000.0)
        return 0;

    struct Partial
    {
        int bin = 0;
        float weight = 0.0f;
    };
    struct NoteTemplate
    {
        std::vector<Partial> partials;
        float normSquared = 0.0f;
    };
    struct NoteTrack
    {
        bool active = false;
        int onFrames = 0;
        int offFrames = 0;
        double start = 0.0;
        float peak = 0.0f;
        int velocity = 100;
    };

    juce::dsp::FFT fft(fftOrder);
    std::vector<float> fftData((size_t)fftSize * 2, 0.0f);
    std::array<float, fftSize> window {};
    for (int i = 0; i < fftSize; ++i)
        window[(size_t)i] = 0.5f - 0.5f * std::cos(
            juce::MathConstants<float>::twoPi * (float)i / (float)(fftSize - 1));

    std::array<NoteTemplate, 128> templates;
    for (int midi = firstMidi; midi <= lastMidi; ++midi)
    {
        const float fundamental = 440.0f * std::pow(2.0f, (float)(midi - 69) / 12.0f);
        for (int harmonic = 1; harmonic <= 6; ++harmonic)
        {
            const float frequency = fundamental * (float)harmonic;
            if (frequency >= (float)(analysisRate * 0.48))
                break;

            const int centreBin = (int)std::round(
                (double)frequency * (double)fftSize / analysisRate);
            const float harmonicWeight = 1.0f / std::pow((float)harmonic, 0.85f);
            for (int offset = -1; offset <= 1; ++offset)
            {
                const int bin = centreBin + offset;
                if (bin < 1 || bin >= fftSize / 2)
                    continue;

                const float weight = harmonicWeight * (offset == 0 ? 1.0f : 0.30f);
                templates[(size_t)midi].partials.push_back({ bin, weight });
                templates[(size_t)midi].normSquared += weight * weight;
            }
        }
    }

    const float* samples = audio.getReadPointer(0);
    const int totalSamples = audio.getNumSamples();
    const double duration = (double)totalSamples / analysisRate;
    const float gateDb = parameters.getRawParameterValue("inputGateDb")->load();
    const float gateRms = std::pow(10.0f, gateDb / 20.0f);
    std::array<NoteTrack, 128> tracks;
    std::vector<NoteEvent> detected;
    detected.reserve(256);

    auto closeTrack = [&](int midi, double endTime)
    {
        auto& track = tracks[(size_t)midi];
        if (!track.active)
            return;

        const double length = juce::jmax(0.0, endTime - track.start);
        if (length >= 0.070)
            detected.push_back({ midi, track.start, length, track.peak, track.velocity });

        track = {};
    };

    std::array<float, fftSize / 2 + 1> residual {};
    std::array<float, 128> activation {};

    for (int centreSample = 0; centreSample < totalSamples; centreSample += hopSize)
    {
        std::fill(fftData.begin(), fftData.end(), 0.0f);
        for (int i = 0; i < fftSize; ++i)
        {
            const int sourceIndex = centreSample + i - fftSize / 2;
            if (sourceIndex >= 0 && sourceIndex < totalSamples)
                fftData[(size_t)i] = samples[sourceIndex] * window[(size_t)i];
        }

        const int frameStart = juce::jlimit(0, juce::jmax(0, totalSamples - fftSize),
                                            centreSample - fftSize / 2);
        const float frameRms = getFrameRms(samples + frameStart, fftSize);
        if (frameRms < gateRms)
        {
            for (int midi = firstMidi; midi <= lastMidi; ++midi)
            {
                auto& track = tracks[(size_t)midi];
                track.onFrames = 0;
                if (track.active && ++track.offFrames >= 5)
                    closeTrack(midi, (double)centreSample / analysisRate);
            }
            continue;
        }

        fft.performFrequencyOnlyForwardTransform(fftData.data());
        for (int bin = 0; bin <= fftSize / 2; ++bin)
            residual[(size_t)bin] = fftData[(size_t)bin];

        activation.fill(0.0f);
        float firstScore = 0.0f;
        std::array<bool, 128> selected {};

        for (int voice = 0; voice < maxVoicesPerFrame; ++voice)
        {
            int bestMidi = -1;
            float bestScore = 0.0f;
            float bestDot = 0.0f;

            for (int midi = firstMidi; midi <= lastMidi; ++midi)
            {
                if (selected[(size_t)midi])
                    continue;

                const auto& noteTemplate = templates[(size_t)midi];
                if (noteTemplate.normSquared <= 1.0e-12f)
                    continue;

                float dot = 0.0f;
                for (const auto& partial : noteTemplate.partials)
                    dot += residual[(size_t)partial.bin] * partial.weight;

                const float score = dot / std::sqrt(noteTemplate.normSquared);
                if (score > bestScore)
                {
                    bestScore = score;
                    bestDot = dot;
                    bestMidi = midi;
                }
            }

            if (bestMidi < 0 || bestScore <= 1.0e-6f)
                break;
            if (voice == 0)
                firstScore = bestScore;
            else if (bestScore < firstScore * 0.30f)
                break;

            selected[(size_t)bestMidi] = true;
            activation[(size_t)bestMidi] = bestScore / juce::jmax(1.0e-6f, firstScore);

            const auto& chosen = templates[(size_t)bestMidi];
            const float coefficient = bestDot / juce::jmax(1.0e-6f, chosen.normSquared);
            for (const auto& partial : chosen.partials)
            {
                float& binValue = residual[(size_t)partial.bin];
                binValue = juce::jmax(0.0f, binValue - coefficient * partial.weight);
            }
        }

        const double frameTime = (double)centreSample / analysisRate;
        for (int midi = firstMidi; midi <= lastMidi; ++midi)
        {
            auto& track = tracks[(size_t)midi];
            const float strength = activation[(size_t)midi];
            const bool present = strength >= 0.30f;

            if (present)
            {
                track.offFrames = 0;
                if (!track.active)
                {
                    ++track.onFrames;
                    if (track.onFrames >= 3)
                    {
                        track.active = true;
                        track.start = juce::jmax(0.0, frameTime - 2.0 * (double)hopSize / analysisRate);
                        track.peak = strength;
                        track.velocity = velocityFromRms(frameRms * std::sqrt(juce::jmax(0.15f, strength)), gateDb);
                    }
                }
                else
                {
                    track.peak = juce::jmax(track.peak, strength);
                }
            }
            else
            {
                track.onFrames = 0;
                if (track.active)
                {
                    ++track.offFrames;
                    if (track.offFrames >= 5)
                    {
                        const double endTime = frameTime -
                            (double)(track.offFrames - 1) * (double)hopSize / analysisRate;
                        closeTrack(midi, endTime);
                    }
                }
            }
        }
    }

    for (int midi = firstMidi; midi <= lastMidi; ++midi)
        if (tracks[(size_t)midi].active)
            closeTrack(midi, duration);

    std::stable_sort(detected.begin(), detected.end(),
        [](const NoteEvent& a, const NoteEvent& b)
        {
            if (a.start != b.start)
                return a.start < b.start;
            return a.midi < b.midi;
        });

    {
        juce::ScopedLock lock(notesLock);
        notes.clear();
        for (const auto& note : detected)
            notes.add(note);
    }

    return (int)detected.size();
}

int NocturneAudioProcessor::analyseFile(const juce::File& file)
{
    juce::AudioFormatManager formats;
    formats.registerBasicFormats();
    std::unique_ptr<juce::AudioFormatReader> reader(formats.createReaderFor(file));
    if (!reader || reader->lengthInSamples <= 0 || reader->sampleRate < 8000.0)
        return 0;

    const int totalSamples = (int)juce::jmin<int64>(reader->lengthInSamples, (int64)std::numeric_limits<int>::max());
    const int sourceChannels = juce::jlimit(1, 2, (int)reader->numChannels);
    juce::AudioBuffer<float> source(sourceChannels, totalSamples);
    if (!reader->read(&source, 0, totalSamples, 0, true, true))
        return 0;

    juce::AudioBuffer<float> audio(1, totalSamples);
    float* mono = audio.getWritePointer(0);
    for (int sample = 0; sample < totalSamples; ++sample)
    {
        float sum = 0.0f;
        for (int channel = 0; channel < sourceChannels; ++channel)
            sum += source.getSample(channel, sample);
        mono[sample] = sum / (float)sourceChannels;
    }

    lastImportedAudio.makeCopyOf(audio);
    lastImportedSampleRate = reader->sampleRate;
    latestAudioIsRecording.store(false);
    return analyseAudioBuffer(lastImportedAudio, lastImportedSampleRate);
}

int NocturneAudioProcessor::analyseLastAudio()
{
    if (latestAudioIsRecording.load())
    {
        if (!lastRecordingReady.load())
            return 0;

        const int cachedSamples = juce::jlimit(0, lastRecordingAudio.getNumSamples(),
                                               lastRecordingSamples.load());
        if (cachedSamples <= 0 || sampleRate < 8000.0)
            return 0;

        juce::AudioBuffer<float> recordedAudio(1, cachedSamples);
        recordedAudio.copyFrom(0, 0, lastRecordingAudio, 0, 0, cachedSamples);
        return analyseAudioBuffer(recordedAudio, sampleRate);
    }

    if (lastImportedAudio.getNumSamples() <= 0 || lastImportedSampleRate < 8000.0)
        return 0;
    return analyseAudioBuffer(lastImportedAudio, lastImportedSampleRate);
}

int NocturneAudioProcessor::analyseAudioBuffer(const juce::AudioBuffer<float>& audio, double fileRate)
{
    if (audio.getNumSamples() <= 0 || fileRate < 8000.0)
        return 0;

    const int totalSamples = audio.getNumSamples();
    estimateKey(audio, fileRate);

    const int analysisMode = (int)std::round(parameters.getRawParameterValue("analysisMode")->load());
    if (analysisMode == 1)
    {
        clearNotes();
        return analysePolyphonicFile(audio, fileRate);
    }

    clearNotes();
    PitchTrackerState tracker;
    const int hop = 256; // denser offline frames preserve short vocal articulations
    const float accuracy = juce::jmin(0.86f, parameters.getRawParameterValue("accuracy")->load());
    const float glide = parameters.getRawParameterValue("glide")->load() * 0.35f;
    const float vibrato = parameters.getRawParameterValue("vibrato")->load() * 0.65f;
    const float stability = parameters.getRawParameterValue("stability")->load() * 0.35f;
    const float confirmationDelayMs = parameters.getRawParameterValue("confirmationDelayMs")->load();
    const int chordMode = (int)std::round(parameters.getRawParameterValue("chordMode")->load());
    const float gateDb = parameters.getRawParameterValue("inputGateDb")->load();
    const float gateRms = std::pow(10.0f, gateDb / 20.0f);
    int detectedCount = 0;

    for (int position = 0; position + analysisWindowSize <= totalSamples; position += hop)
    {
        const float* frameSamples = audio.getReadPointer(0) + position;
        const float frameRms = getFrameRms(frameSamples, analysisWindowSize);
        float confidence = 0.0f;
        const float hz = frameRms >= gateRms
            ? detectPitchYin(frameSamples, analysisWindowSize, confidence, fileRate) : 0.0f;
        const int frameVelocity = velocityFromRms(frameRms, gateDb);
        const double time = (double)position / fileRate;

        const auto previousChord = tracker.activeChordMidi;
        const int previousChordCount = tracker.activeChordCount;
        const double previousNoteStart = tracker.noteStart;
        const int previousRoot = tracker.activeMidi;

        const auto events = updatePitchTracker(tracker, hz, confidence, time,
                                                accuracy, glide, vibrato, stability, confirmationDelayMs);

        juce::ScopedLock lock(notesLock);
        if (events.closed)
        {
            const int count = previousChordCount > 0 ? previousChordCount : 1;
            for (int i = 0; i < count; ++i)
            {
                const int noteMidi = previousChordCount > 0
                    ? previousChord[(size_t)i] : previousRoot;
                for (int n = notes.size() - 1; n >= 0; --n)
                {
                    auto& note = notes.getReference(n);
                    if (note.midi == noteMidi && note.start == previousNoteStart)
                    {
                        note.length = juce::jmax(0.04, events.closeTime - note.start);
                        break;
                    }
                }
            }
        }

        if (events.opened)
        {
            const auto chord = makeChordNotes(events.openedMidi, chordMode);
            tracker.activeChordMidi = chord.midi;
            tracker.activeChordCount = chord.count;
            for (int i = 0; i < chord.count; ++i)
            {
                notes.add({ chord.midi[(size_t)i], events.openTime, 0.06, events.openConfidence, frameVelocity });
                ++detectedCount;
            }
        }
        else if (events.closed)
        {
            tracker.activeChordCount = 0;
        }

        if (tracker.activeMidi >= 0)
        {
            for (int chordIndex = 0; chordIndex < tracker.activeChordCount; ++chordIndex)
            {
                const int noteMidi = tracker.activeChordMidi[(size_t)chordIndex];
                for (int n = notes.size() - 1; n >= 0; --n)
                {
                    auto& note = notes.getReference(n);
                    if (note.midi == noteMidi && note.start == tracker.noteStart)
                    {
                        note.length = juce::jmax(0.04, time - note.start);
                        break;
                    }
                }
            }
        }
    }

    if (tracker.activeMidi >= 0)
    {
        juce::ScopedLock lock(notesLock);
        for (int chordIndex = 0; chordIndex < tracker.activeChordCount; ++chordIndex)
        {
            const int noteMidi = tracker.activeChordMidi[(size_t)chordIndex];
            for (int n = notes.size() - 1; n >= 0; --n)
            {
                auto& note = notes.getReference(n);
                if (note.midi == noteMidi && note.start == tracker.noteStart)
                {
                    note.length = juce::jmax(0.05, (double)totalSamples / fileRate - note.start);
                    break;
                }
            }
        }
    }

    // Post-capture cleanup removes tiny tracking ghosts and merges split sustains.
    std::vector<NoteEvent> ordered;
    {
        const auto captured = getNotesCopy();
        ordered.reserve((size_t)captured.size());
        for (const auto& note : captured)
        {
            // Reject very short, low-confidence pitch ghosts while preserving articulated notes.
            if (note.length >= 0.065 && note.confidence >= 0.54f)
                ordered.push_back(note);
        }
    }
    std::stable_sort(ordered.begin(), ordered.end(),
        [](const NoteEvent& a, const NoteEvent& b) { return a.start < b.start; });

    juce::Array<NoteEvent> cleaned;
    for (const auto& note : ordered)
    {
        if (!cleaned.isEmpty())
        {
            auto& previous = cleaned.getReference(cleaned.size() - 1);
            const double gap = note.start - (previous.start + previous.length);
            if (previous.midi == note.midi && gap >= -0.02 && gap <= 0.10)
            {
                previous.length = juce::jmax(previous.start + previous.length,
                                             note.start + note.length) - previous.start;
                previous.confidence = juce::jmax(previous.confidence, note.confidence);
                continue;
            }
        }
        cleaned.add(note);
    }

    {
        juce::ScopedLock lock(notesLock);
        notes.clear();
        notes.addArray(cleaned);
        detectedCount = notes.size();
    }
    return detectedCount;
}

void NocturneAudioProcessor::moveNote(int index, int midi, double start)
{
    juce::ScopedLock lock(notesLock);
    if (juce::isPositiveAndBelow(index, notes.size()))
    {
        auto& n = notes.getReference(index);
        n.midi = juce::jlimit(0, 127, midi);
        n.start = juce::jmax(0.0, start);
    }
}

void NocturneAudioProcessor::setNoteLength(int index, double length)
{
    juce::ScopedLock lock(notesLock);
    if (juce::isPositiveAndBelow(index, notes.size()))
        notes.getReference(index).length = juce::jlimit(0.03, 32.0, length);
}

void NocturneAudioProcessor::setNoteVelocity(int index, int velocity)
{
    juce::ScopedLock lock(notesLock);
    if (juce::isPositiveAndBelow(index, notes.size()))
        notes.getReference(index).velocity = juce::jlimit(1, 127, velocity);
}

void NocturneAudioProcessor::addNote(int midi, double start, double length, int velocity)
{
    juce::ScopedLock lock(notesLock);
    NoteEvent note;
    note.midi = juce::jlimit(0, 127, midi);
    note.start = juce::jmax(0.0, start);
    note.length = juce::jlimit(0.03, 32.0, length);
    note.velocity = juce::jlimit(1, 127, velocity);
    note.confidence = 1.0f;
    notes.add(note);
}

void NocturneAudioProcessor::removeNote(int index)
{
    juce::ScopedLock lock(notesLock);
    if (juce::isPositiveAndBelow(index, notes.size()))
        notes.remove(index);
}

void NocturneAudioProcessor::replaceNotes(const juce::Array<NoteEvent>& replacement)
{
    juce::ScopedLock lock(notesLock);
    notes = replacement;
}

juce::File NocturneAudioProcessor::createTemporaryMidiFile()
{
    const auto directory = juce::File::getSpecialLocation(juce::File::tempDirectory)
        .getChildFile("NOCTURNE_MIDI_Drag");
    const auto directoryResult = directory.createDirectory();
    if (directoryResult.failed())
        return {};

    const auto file = directory.getChildFile("NOCTURNE_" + juce::Uuid().toString() + ".mid");
    exportMidi(file);
    return file.existsAsFile() && file.getSize() > 14 ? file : juce::File{};
}

void NocturneAudioProcessor::exportMidi(const juce::File& file)
{
    juce::MidiFile midiFile;
    midiFile.setTicksPerQuarterNote(960);
    juce::MidiMessageSequence sequence;
    const auto copy = getNotesCopy();
    const double tempo = juce::jmax(20.0f, bpm.load());
    const int division = quantizeDivision.load();
    const double gridSeconds = division > 0 ? (60.0 / tempo) * (4.0 / (double)division) : 0.0;

    for (const auto& note : copy)
    {
        const double quantizedStart = gridSeconds > 0.0
            ? std::round(note.start / gridSeconds) * gridSeconds : note.start;
        const double quantizedEnd = gridSeconds > 0.0
            ? std::round((note.start + note.length) / gridSeconds) * gridSeconds
            : (note.start + note.length);
        const double startTicks = quantizedStart * 960.0 * tempo / 60.0;
        const double endTicks = juce::jmax(quantizedStart + 0.03, quantizedEnd) * 960.0 * tempo / 60.0;
        sequence.addEvent(juce::MidiMessage::noteOn(1, note.midi,
            (juce::uint8)juce::jlimit(1, 127, note.velocity)), startTicks);
        sequence.addEvent(juce::MidiMessage::noteOff(1, note.midi), endTicks);
    }

    // Write a self-contained format-0 MIDI file. FL Studio's import and
    // external file-drop paths need the source tempo to interpret tick positions.
    sequence.addEvent(juce::MidiMessage::tempoMetaEvent(
        (int)std::round(60000000.0 / tempo)), 0.0);
    sequence.addEvent(juce::MidiMessage::timeSignatureMetaEvent(4, 4), 0.0);
    sequence.updateMatchedPairs();
    midiFile.addTrack(sequence);
    file.deleteFile();
    juce::FileOutputStream output(file);
    if (output.openedOk())
    {
        const bool written = midiFile.writeTo(output, 0);
        output.flush();
        if (!written)
            file.deleteFile();
    }
}

bool NocturneAudioProcessor::copyMidiToClipboard(void* clipboardOwnerWindow)
{
    juce::MidiFile midiFile;
    midiFile.setTicksPerQuarterNote(960);
    juce::MidiMessageSequence sequence;
    const auto copy = getNotesCopy();
    if (copy.isEmpty())
        return false;

    const double tempo = juce::jmax(20.0f, bpm.load());
    const int division = quantizeDivision.load();
    const double gridSeconds = division > 0
        ? (60.0 / tempo) * (4.0 / (double)division) : 0.0;

    for (const auto& note : copy)
    {
        const double quantizedStart = gridSeconds > 0.0
            ? std::round(note.start / gridSeconds) * gridSeconds : note.start;
        const double quantizedEnd = gridSeconds > 0.0
            ? std::round((note.start + note.length) / gridSeconds) * gridSeconds
            : (note.start + note.length);
        const double startTicks = quantizedStart * 960.0 * tempo / 60.0;
        const double endTicks = juce::jmax(quantizedStart + 0.03, quantizedEnd)
                              * 960.0 * tempo / 60.0;

        sequence.addEvent(juce::MidiMessage::noteOn(
            1, note.midi, (juce::uint8)juce::jlimit(1, 127, note.velocity)), startTicks);
        sequence.addEvent(juce::MidiMessage::noteOff(1, note.midi), endTicks);
    }

    sequence.updateMatchedPairs();
    // FL Studio's clipboard parser accepts a single-track Standard MIDI File.
    // Embed the actual project tempo so tick positions preserve their timing.
    sequence.addEvent(juce::MidiMessage::tempoMetaEvent(
        (int)std::round(60000000.0 / tempo)), 0.0);
    sequence.addEvent(juce::MidiMessage::timeSignatureMetaEvent(4, 4), 0.0);
    sequence.updateMatchedPairs();
    midiFile.addTrack(sequence);

    juce::MemoryOutputStream midiBytes;
    if (!midiFile.writeTo(midiBytes, 0) || midiBytes.getDataSize() == 0)
        return false;

   #if JUCE_WINDOWS
    // FL Studio's "Paste from MIDI Clipboard" accepts the registered
    // "Standard MIDI File" clipboard format (not plain text).
    HWND owner = static_cast<HWND>(clipboardOwnerWindow);
    if (owner == nullptr || !::IsWindow(owner))
    {
        owner = ::GetActiveWindow();
        if (owner == nullptr || !::IsWindow(owner))
            owner = ::GetForegroundWindow();
    }

    const UINT midiClipboardFormat = ::RegisterClipboardFormatW(L"Standard MIDI File");
    // EmptyClipboard requires a real owner HWND. Passing NULL makes SetClipboardData fail.
    if (owner == nullptr || midiClipboardFormat == 0 || !::OpenClipboard(owner))
        return false;

    if (!::EmptyClipboard())
    {
        ::CloseClipboard();
        return false;
    }
    const SIZE_T byteCount = (SIZE_T)midiBytes.getDataSize();
    HGLOBAL memory = ::GlobalAlloc(GMEM_MOVEABLE, byteCount);
    if (memory == nullptr)
    {
        ::CloseClipboard();
        return false;
    }

    void* destination = ::GlobalLock(memory);
    if (destination == nullptr)
    {
        ::GlobalFree(memory);
        ::CloseClipboard();
        return false;
    }

    std::memcpy(destination, midiBytes.getData(), (size_t)byteCount);
    ::GlobalUnlock(memory);

    if (::SetClipboardData(midiClipboardFormat, memory) == nullptr)
    {
        ::GlobalFree(memory);
        ::CloseClipboard();
        return false;
    }

    // Windows owns the HGLOBAL after SetClipboardData succeeds.
    ::CloseClipboard();
    return true;
   #else
    return false;
   #endif
}

juce::String NocturneAudioProcessor::notesAsText() const
{
    juce::String output;
    const auto copy = getNotesCopy();
    for (const auto& note : copy)
        output << juce::MidiMessage::getMidiNoteName(note.midi, true, true, 3)
               << "  " << juce::String(note.start, 3) << "s"
               << "  VEL " << juce::String(note.velocity) << "\n";
    return output;
}

std::array<float, 256> NocturneAudioProcessor::getLiveWaveformSnapshot() const
{
    std::array<float, 256> snapshot {};
    const int write = liveWaveformWrite.load();
    for (int i = 0; i < (int)snapshot.size(); ++i)
    {
        const int source = (write + i) % (int)liveWaveform.size();
        snapshot[(size_t)i] = liveWaveform[(size_t)source].load();
    }
    return snapshot;
}

juce::AudioProcessorEditor* NocturneAudioProcessor::createEditor()
{
    return new NocturneAudioProcessorEditor(*this);
}

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new NocturneAudioProcessor();
}
