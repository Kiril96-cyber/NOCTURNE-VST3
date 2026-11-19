#include "PluginProcessor.h"
#include "PluginEditor.h"

NocturneAudioProcessor::NocturneAudioProcessor()
    : AudioProcessor(BusesProperties()
          .withInput("Input", juce::AudioChannelSet::stereo(), true)
          .withOutput("Output", juce::AudioChannelSet::stereo(), true)),
      parameters(*this, nullptr, "PARAMETERS", createParams())
{
}

juce::AudioProcessorValueTreeState::ParameterLayout NocturneAudioProcessor::createParams()
{
    std::vector<std::unique_ptr<juce::RangedAudioParameter>> p;
    p.push_back(std::make_unique<juce::AudioParameterFloat>("speed", "Read speed", 10.0f, 100.0f, 45.0f));
    p.push_back(std::make_unique<juce::AudioParameterFloat>("accuracy", "Note accuracy", 0.50f, 0.99f, 0.78f));
    p.push_back(std::make_unique<juce::AudioParameterFloat>("glide", "Pitch glide", 0.0f, 100.0f, 35.0f));
    p.push_back(std::make_unique<juce::AudioParameterFloat>("vibrato", "Vibrato tolerance", 0.0f, 100.0f, 55.0f));
    return { p.begin(), p.end() };
}

void NocturneAudioProcessor::prepareToPlay(double sr, int)
{
    sampleRate = juce::jmax(8000.0, sr);
    analysisBuffer.setSize(1, analysisWindowSize);
    analysisBuffer.clear();
    analysisFrame.setSize(1, analysisWindowSize);
    analysisFrame.clear();

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
    std::array<float, analysisWindowSize + 1> difference {};
    std::array<float, analysisWindowSize + 1> cumulativeMean {};
    double rms = 0.0;
    for (int i = 0; i < n; ++i)
        rms += (double)x[i] * (double)x[i];
    rms = std::sqrt(rms / (double)n);
    if (rms < 0.0012)
        return 0.0f;

    const int compareCount = n - maxLag;
    for (int tau = 1; tau <= maxLag; ++tau)
    {
        float sum = 0.0f;
        for (int i = 0; i < compareCount; ++i)
        {
            const float d = x[i] - x[i + tau];
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

NocturneAudioProcessor::PitchTrackerEvents NocturneAudioProcessor::updatePitchTracker(
    PitchTrackerState& state, float hz, float confidence, double time,
    float accuracy, float glide, float vibrato, float speed)
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
            const double releaseSeconds = 0.075 + (double)vibrato * 0.0010;
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
    const float smoothing = juce::jlimit(0.0f, 0.94f, glide * 0.0065f + vibrato * 0.0035f);
    if (state.smoothedMidi < 0.0f)
        state.smoothedMidi = rawMidi;
    else
        state.smoothedMidi = state.smoothedMidi * smoothing + rawMidi * (1.0f - smoothing);

    const int candidate = juce::jlimit(0, 127, (int)std::round(state.smoothedMidi));
    const int confirmations = juce::jlimit(2, 7, 2 + (int)std::round((100.0f - speed) * 0.045f));

    if (state.activeMidi >= 0 && std::abs(state.smoothedMidi - (float)state.activeMidi) < 0.72f)
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

    if (state.candidateFrames < confirmations)
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
        midi.addEvent(juce::MidiMessage::noteOff(1, liveTracker.activeMidi), midiSampleOffset);
        juce::ScopedLock lock(notesLock);
        if (!notes.isEmpty())
        {
            auto& last = notes.getReference(notes.size() - 1);
            if (last.midi == liveTracker.activeMidi)
                last.length = juce::jmax(0.04, time - last.start);
        }
    }
    liveTracker = {};
}

void NocturneAudioProcessor::acceptPitch(float hz, float confidence, double time,
                                          int midiSampleOffset, juce::MidiBuffer& midi)
{
    const float accuracy = parameters.getRawParameterValue("accuracy")->load();
    const float glide = parameters.getRawParameterValue("glide")->load();
    const float vibrato = parameters.getRawParameterValue("vibrato")->load();
    const float speed = parameters.getRawParameterValue("speed")->load();

    const auto events = updatePitchTracker(liveTracker, hz, confidence, time,
                                            accuracy, glide, vibrato, speed);

    if (events.closed)
    {
        midi.addEvent(juce::MidiMessage::noteOff(1, events.closedMidi), midiSampleOffset);
        juce::ScopedLock lock(notesLock);
        for (int i = notes.size() - 1; i >= 0; --i)
        {
            auto& n = notes.getReference(i);
            if (n.midi == events.closedMidi && n.start <= events.closeTime &&
                n.start + n.length >= events.closeTime - 0.01)
            {
                n.length = juce::jmax(0.04, events.closeTime - n.start);
                break;
            }
        }
    }

    if (events.opened)
    {
        midi.addEvent(juce::MidiMessage::noteOn(1, events.openedMidi, (juce::uint8)100), midiSampleOffset);
        juce::ScopedLock lock(notesLock);
        notes.add({ events.openedMidi, events.openTime, 0.06, events.openConfidence });
    }

    if (liveTracker.activeMidi >= 0)
    {
        juce::ScopedLock lock(notesLock);
        for (int i = notes.size() - 1; i >= 0; --i)
        {
            auto& n = notes.getReference(i);
            if (n.midi == liveTracker.activeMidi && n.start == liveTracker.noteStart)
            {
                n.length = juce::jmax(0.04, time - n.start);
                break;
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

    const int waveformIndex = liveWaveformWrite.fetch_add(1) % (int)liveWaveform.size();
    liveWaveform[(size_t)waveformIndex].store(blockPeak);

    const bool isRecording = recording.load();
    if (isRecording && !wasRecording)
    {
        recordingStartTime = elapsedSeconds;
        liveTracker = {};
        recordingPositionSeconds.store(0.0);
    }
    else if (!isRecording && wasRecording)
    {
        finishLiveNote(elapsedSeconds - recordingStartTime, 0, midi);
        recordingPositionSeconds.store(0.0);
    }
    wasRecording = isRecording;

    const float speed = parameters.getRawParameterValue("speed")->load();
    const int hopSize = juce::jlimit(256, 1024,
        (int)std::round(1024.0 - (speed - 10.0f) * (768.0f / 90.0f)));

    if (channels > 0)
    {
        const float* input = buffer.getReadPointer(0);
        for (int i = 0; i < numSamples; ++i)
        {
            analysisBuffer.setSample(0, writePosition, input[i]);
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

                float confidence = 0.0f;
                const float hz = detectPitchYin(frame, analysisWindowSize, confidence, sampleRate);
                currentPitchHz.store(hz);

                if (isRecording)
                {
                    const double position = elapsedSeconds - recordingStartTime;
                    recordingPositionSeconds.store(position);
                    acceptPitch(hz, confidence, position, i, midi);
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
    liveTracker = {};
}

int NocturneAudioProcessor::analyseFile(const juce::File& file)
{
    juce::AudioFormatManager formats;
    formats.registerBasicFormats();
    std::unique_ptr<juce::AudioFormatReader> reader(formats.createReaderFor(file));
    if (!reader || reader->lengthInSamples <= 0 || reader->sampleRate < 8000.0)
        return 0;

    const int totalSamples = (int)juce::jmin<int64>(reader->lengthInSamples, (int64)std::numeric_limits<int>::max());
    juce::AudioBuffer<float> audio(1, totalSamples);
    if (!reader->read(&audio, 0, totalSamples, 0, true, true))
        return 0;

    clearNotes();
    PitchTrackerState tracker;
    const double fileRate = reader->sampleRate;
    const int hop = 512;
    const float accuracy = parameters.getRawParameterValue("accuracy")->load();
    const float glide = parameters.getRawParameterValue("glide")->load();
    const float vibrato = parameters.getRawParameterValue("vibrato")->load();
    const float speed = parameters.getRawParameterValue("speed")->load();
    int detectedCount = 0;

    for (int position = 0; position + analysisWindowSize <= totalSamples; position += hop)
    {
        float confidence = 0.0f;
        const float hz = detectPitchYin(audio.getReadPointer(0) + position,
                                        analysisWindowSize, confidence, fileRate);
        const double time = (double)position / fileRate;
        const auto events = updatePitchTracker(tracker, hz, confidence, time,
                                                accuracy, glide, vibrato, speed);

        juce::ScopedLock lock(notesLock);
        if (events.closed)
        {
            for (int i = notes.size() - 1; i >= 0; --i)
            {
                auto& n = notes.getReference(i);
                if (n.midi == events.closedMidi && n.start <= events.closeTime &&
                    n.start + n.length >= events.closeTime - 0.01)
                {
                    n.length = juce::jmax(0.04, events.closeTime - n.start);
                    break;
                }
            }
        }
        if (events.opened)
        {
            notes.add({ events.openedMidi, events.openTime, 0.06, events.openConfidence });
            ++detectedCount;
        }
        if (tracker.activeMidi >= 0 && !notes.isEmpty())
        {
            auto& last = notes.getReference(notes.size() - 1);
            if (last.midi == tracker.activeMidi && last.start == tracker.noteStart)
                last.length = juce::jmax(0.04, time - last.start);
        }
    }

    if (tracker.activeMidi >= 0)
    {
        juce::ScopedLock lock(notesLock);
        for (int i = notes.size() - 1; i >= 0; --i)
        {
            auto& n = notes.getReference(i);
            if (n.midi == tracker.activeMidi && n.start == tracker.noteStart)
            {
                n.length = juce::jmax(0.05, (double)totalSamples / fileRate - n.start);
                break;
            }
        }
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
        sequence.addEvent(juce::MidiMessage::noteOn(1, note.midi, (juce::uint8)100), startTicks);
        sequence.addEvent(juce::MidiMessage::noteOff(1, note.midi), endTicks);
    }

    sequence.updateMatchedPairs();
    midiFile.addTrack(sequence);
    file.deleteFile();
    juce::FileOutputStream output(file);
    if (output.openedOk())
        midiFile.writeTo(output);
}

juce::String NocturneAudioProcessor::notesAsText() const
{
    juce::String output;
    const auto copy = getNotesCopy();
    for (const auto& note : copy)
        output << juce::MidiMessage::getMidiNoteName(note.midi, true, true, 3)
               << "  " << juce::String(note.start, 3) << "s\n";
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
