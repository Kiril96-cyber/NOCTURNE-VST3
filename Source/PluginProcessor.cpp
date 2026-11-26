#include "PluginProcessor.h"
#include "PluginEditor.h"
#include "PolyphonicAnalyzer.h"

NocturneAudioProcessor::NocturneAudioProcessor()
 : AudioProcessor(BusesProperties().withInput("Input", juce::AudioChannelSet::stereo(), true)
                                  .withOutput("Output", juce::AudioChannelSet::stereo(), true)),
   parameters(*this, nullptr, "PARAMETERS", createParams())
{
}
juce::AudioProcessorValueTreeState::ParameterLayout NocturneAudioProcessor::createParams()
{
    std::vector<std::unique_ptr<juce::RangedAudioParameter>> p;
    p.push_back(std::make_unique<juce::AudioParameterFloat>("speed","Read speed",10.0f,100.0f,45.0f));
    p.push_back(std::make_unique<juce::AudioParameterFloat>("accuracy","Note accuracy",0.50f,0.99f,0.82f));
    p.push_back(std::make_unique<juce::AudioParameterFloat>("glide","Pitch glide",0.0f,100.0f,25.0f));
    p.push_back(std::make_unique<juce::AudioParameterFloat>("vibrato","Vibrato tolerance",0.0f,100.0f,35.0f));
    return { p.begin(), p.end() };
}
void NocturneAudioProcessor::prepareToPlay(double sr, int)
{
    sampleRate = sr; analysisBuffer.setSize(1, 4096); analysisBuffer.clear();
    filled = hopCounter = 0; elapsed = 0.0; activeMidi = -1;
}
float NocturneAudioProcessor::detectPitchYin(const float* x, int n, float& confidence) const
{
    const int minLag = juce::jmax(2, (int)(sampleRate / 1000.0));
    const int maxLag = juce::jmin(n / 2, (int)(sampleRate / 65.0));
    if (maxLag <= minLag) { confidence = 0; return 0; }
    std::vector<float> diff((size_t)maxLag + 1, 0.0f), cmnd((size_t)maxLag + 1, 1.0f);
    for (int tau=minLag; tau<=maxLag; ++tau)
        for (int i=0; i<n-maxLag; ++i) { float d=x[i]-x[i+tau]; diff[(size_t)tau] += d*d; }
    float running=0;
    for (int tau=minLag; tau<=maxLag; ++tau) { running += diff[(size_t)tau]; cmnd[(size_t)tau] = running > 0 ? diff[(size_t)tau]*tau/running : 1.0f; }
    const float threshold = 0.18f;
    int best=-1;
    for (int tau=minLag; tau<maxLag; ++tau)
        if (cmnd[(size_t)tau] < threshold) { while (tau+1<=maxLag && cmnd[(size_t)tau+1] < cmnd[(size_t)tau]) ++tau; best=tau; break; }
    if (best < 0) { best=minLag; for(int t=minLag+1;t<=maxLag;++t) if(cmnd[(size_t)t]<cmnd[(size_t)best]) best=t; }
    confidence = juce::jlimit(0.0f,1.0f,1.0f-cmnd[(size_t)best]);
    if (confidence < 0.55f) return 0;
    float shift=0;
    if(best>minLag && best<maxLag) {
        float a=cmnd[(size_t)best-1], b=cmnd[(size_t)best], c=cmnd[(size_t)best+1];
        float denom=a-2*b+c; if(std::abs(denom)>1.0e-6f) shift=0.5f*(a-c)/denom;
    }
    return (float)(sampleRate/(best+shift));
}
void NocturneAudioProcessor::acceptPitch(float hz, float confidence, double time, int midiSampleOffset, juce::MidiBuffer& midi)
{
    const float accuracy=*parameters.getRawParameterValue("accuracy");
    if(hz<=0 || confidence<accuracy) {
        if(activeMidi>=0) {
            midi.addEvent(juce::MidiMessage::noteOff(1, activeMidi), midiSampleOffset);
            juce::ScopedLock l(notesLock);
            if(!notes.isEmpty()) notes.getReference(notes.size()-1).length=juce::jmax(0.04,time-noteStart);
            activeMidi=-1;
        }
        return;
    }
    float mf=69.0f+12.0f*std::log2(hz/440.0f);
    const float glide=*parameters.getRawParameterValue("glide")/100.0f;
    const float vibrato=*parameters.getRawParameterValue("vibrato")/100.0f;
    if(lastMidiFloat<0) lastMidiFloat=mf;
    // Vibrato tolerance adds gentle pitch stabilization to avoid rapid semitone flips.
    const float smoothing=juce::jlimit(0.0f,0.92f,glide*0.72f+vibrato*0.20f);
    lastMidiFloat=lastMidiFloat*smoothing+(1.0f-smoothing)*mf;
    int noteNumber=juce::jlimit(0,127,(int)std::round(lastMidiFloat));
    if(noteNumber!=activeMidi) {
        if(activeMidi>=0) {
            midi.addEvent(juce::MidiMessage::noteOff(1, activeMidi), midiSampleOffset);
            juce::ScopedLock l(notesLock);
            if(!notes.isEmpty()) notes.getReference(notes.size()-1).length=juce::jmax(0.04,time-noteStart);
        }
        noteStart=time; activeMidi=noteNumber;
        midi.addEvent(juce::MidiMessage::noteOn(1, activeMidi, (juce::uint8)100), midiSampleOffset);
        juce::ScopedLock l(notesLock); notes.add({noteNumber,time,0.12,confidence});
    }
}
void NocturneAudioProcessor::processBlock(juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midi)
{
    juce::ScopedNoDenormals noDenormals;
    const int samples = buffer.getNumSamples();
    float peak = 0.0f;
    for (int channel = 0; channel < buffer.getNumChannels(); ++channel)
        peak = juce::jmax(peak, buffer.getMagnitude(channel, 0, samples));
    inputLevel.store(peak);

    constexpr int window = 2048;
    const float speed = parameters.getRawParameterValue("speed")->load();
    const int hop = juce::jlimit(128, 1024, (int)(sampleRate * (0.05f - speed * 0.0004f)));

    if (!recording.load() && activeMidi >= 0)
    {
        midi.addEvent(juce::MidiMessage::noteOff(1, activeMidi), 0);
        {
            juce::ScopedLock lock(notesLock);
            if (!notes.isEmpty())
                notes.getReference(notes.size() - 1).length = juce::jmax(0.04, elapsed - noteStart);
        }
        activeMidi = -1;
        lastMidiFloat = -1.0f;
    }

    if (buffer.getNumChannels() > 0)
    {
        const auto* input = buffer.getReadPointer(0);
        for (int i = 0; i < samples; ++i)
        {
            if (filled < window)
                analysisBuffer.setSample(0, filled++, input[i]);

            elapsed += 1.0 / sampleRate;
            ++hopCounter;
            if (filled >= window && hopCounter >= hop)
            {
                float confidence = 0.0f;
                const float hz = detectPitchYin(analysisBuffer.getReadPointer(0), window, confidence);
                currentPitchHz.store(hz);
                if (recording.load())
                    acceptPitch(hz, confidence, elapsed - (double)window / (2.0 * sampleRate), i, midi);

                analysisBuffer.copyFrom(0, 0, analysisBuffer, 0, hop, window - hop);
                filled = window - hop;
                hopCounter = 0;
            }
        }
    }
}

juce::Array<NocturneAudioProcessor::NoteEvent> NocturneAudioProcessor::getNotesCopy() const
{ juce::ScopedLock l(notesLock); return notes; }
void NocturneAudioProcessor::clearNotes()
{ juce::ScopedLock l(notesLock); notes.clear(); activeMidi=-1; lastMidiFloat=-1; elapsed=0; }
void NocturneAudioProcessor::analyseFile(const juce::File& file)
{
    juce::AudioFormatManager fm;
    fm.registerBasicFormats();
    std::unique_ptr<juce::AudioFormatReader> reader(fm.createReaderFor(file));
    if (!reader || reader->lengthInSamples <= 0 || reader->lengthInSamples > (int64)reader->sampleRate * 60 * 15)
        return;

    const int channels = juce::jlimit(1, 2, (int)reader->numChannels);
    juce::AudioBuffer<float> audio(channels, (int)reader->lengthInSamples);
    if (!reader->read(&audio, 0, audio.getNumSamples(), 0, true, true))
        return;

    clearNotes();
    NocturnePolyphonicAnalyzer analyzer;
    const auto detected = analyzer.analyse(audio, reader->sampleRate, 0.22f, 5);
    {
        juce::ScopedLock lock(notesLock);
        for (const auto& event : detected)
            notes.add({ event.midi, event.start, event.length, event.confidence });
    }
}
void NocturneAudioProcessor::moveNote(int index, int midi, double start)
{
    juce::ScopedLock l(notesLock);
    if (juce::isPositiveAndBelow(index, notes.size()))
    {
        auto& n = notes.getReference(index);
        n.midi = juce::jlimit(0, 127, midi);
        n.start = juce::jmax(0.0, start);
    }
}
void NocturneAudioProcessor::exportMidi(const juce::File& file)
{
    juce::MidiFile mf; mf.setTicksPerQuarterNote(960);
    juce::MidiMessageSequence seq; auto copy=getNotesCopy();
    const double tempo = juce::jmax(20.0f, bpm.load());
    const int division = quantizeDivision.load();
    const double gridSeconds = division > 0 ? (60.0 / tempo) * (4.0 / (double) division) : 0.0;
    for(const auto& n:copy) {
        const double quantizedStart = gridSeconds > 0 ? std::round(n.start / gridSeconds) * gridSeconds : n.start;
        const double quantizedEnd = gridSeconds > 0 ? std::round((n.start + n.length) / gridSeconds) * gridSeconds : (n.start + n.length);
        const double start=quantizedStart*960.0*tempo/60.0, end=juce::jmax(quantizedStart+0.03,quantizedEnd)*960.0*tempo/60.0;
        seq.addEvent(juce::MidiMessage::noteOn(1,n.midi,(juce::uint8)100),start);
        seq.addEvent(juce::MidiMessage::noteOff(1,n.midi),end);
    }
    seq.updateMatchedPairs(); mf.addTrack(seq);
    file.deleteFile(); juce::FileOutputStream out(file);
    if(out.openedOk()) mf.writeTo(out);
}
juce::String NocturneAudioProcessor::notesAsText() const
{
    juce::String s; auto copy=getNotesCopy();
    for(const auto& n:copy) s << juce::MidiMessage::getMidiNoteName(n.midi,true,true,3) << "  " << juce::String(n.start,3) << "s\n";
    return s;
}
juce::AudioProcessorEditor* NocturneAudioProcessor::createEditor() { return new NocturneAudioProcessorEditor(*this); }
juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter() { return new NocturneAudioProcessor(); }
