#pragma once
#include <JuceHeader.h>
#include <array>
#include <vector>
#include <algorithm>
#include <cmath>

// Offline polyphonic transcription front-end. Uses harmonic salience and
// iterative spectral masking; intentionally independent from the live YIN path.
class NocturnePolyphonicAnalyzer
{
public:
    struct Event { int midi = 60; double start = 0.0; double length = 0.1; float confidence = 0.0f; };

    std::vector<Event> analyse(const juce::AudioBuffer<float>& audio, double sampleRate,
                               float sensitivity = 0.22f, int maxVoices = 5) const
    {
        constexpr int fftSize = 8192;
        constexpr int hop = 1024;
        constexpr int minMidi = 36, maxMidi = 96;
        constexpr int maxHarmonics = 8;
        juce::dsp::FFT fft(13);
        std::vector<float> frame((size_t)fftSize * 2, 0.0f);
        std::array<float, fftSize / 2> spectrum {};
        std::array<float, fftSize / 2> residual {};
        std::array<float, maxMidi + 1> salience {};
        std::array<bool, 128> present {};
        std::array<int, 128> missing {};
        std::array<double, 128> starts {};
        std::array<float, 128> confidence {};
        std::vector<Event> events;
        const int length = audio.getNumSamples();
        if (length < fftSize || sampleRate <= 0.0) return events;
        const int voices = juce::jlimit(1, 8, maxVoices);
        const float minHz = 65.4064f;
        const float maxHz = 2093.0045f;
        juce::ignoreUnused(minHz, maxHz);

        for (int pos = 0; pos + fftSize <= length; pos += hop)
        {
            std::fill(frame.begin(), frame.end(), 0.0f);
            for (int i = 0; i < fftSize; ++i)
            {
                float mono = 0.0f;
                for (int ch = 0; ch < audio.getNumChannels(); ++ch)
                    mono += audio.getSample(ch, pos + i);
                mono /= (float)juce::jmax(1, audio.getNumChannels());
                const float window = 0.5f - 0.5f * std::cos(juce::MathConstants<float>::twoPi * (float)i / (float)(fftSize - 1));
                frame[(size_t)i] = mono * window;
            }
            fft.performFrequencyOnlyForwardTransform(frame.data());
            float total = 0.0f;
            for (int k = 1; k < fftSize / 2; ++k)
            {
                spectrum[(size_t)k] = frame[(size_t)k];
                residual[(size_t)k] = frame[(size_t)k];
                total += frame[(size_t)k];
            }


            std::array<bool, 128> detected {};
            std::array<float, 128> frameConfidence {};
            for (int voice = 0; voice < voices; ++voice)
            {
                int bestMidi = -1;
                float bestScore = 0.0f;
                for (int note = minMidi; note <= maxMidi; ++note)
                {
                    const float hz = 440.0f * std::pow(2.0f, ((float)note - 69.0f) / 12.0f);
                    float score = 0.0f;
                    for (int h = 1; h <= maxHarmonics; ++h)
                    {
                        const int bin = (int)std::round(hz * (float)fftSize / (float)sampleRate * (float)h);
                        if (bin < 2 || bin >= fftSize / 2 - 2) break;
                        float peak = 0.0f;
                        for (int d = -1; d <= 1; ++d)
                            peak = juce::jmax(peak, residual[(size_t)(bin + d)]);
                        score += peak / std::pow((float)h, 0.85f);
                    }
                    // Prefer fundamentals with coherent harmonic support.
                    if (score > bestScore) { bestScore = score; bestMidi = note; }
                }
                if (bestMidi < 0 || total < 1.0e-5f || bestScore / total < sensitivity) break;
                if (detected[(size_t)bestMidi]) break;
                detected[(size_t)bestMidi] = true;
                frameConfidence[(size_t)bestMidi] = juce::jlimit(0.0f, 1.0f, bestScore / (total * 0.65f));
                const float hz = 440.0f * std::pow(2.0f, ((float)bestMidi - 69.0f) / 12.0f);
                for (int h = 1; h <= maxHarmonics; ++h)
                {
                    const int bin = (int)std::round(hz * (float)fftSize / (float)sampleRate * (float)h);
                    if (bin < 2 || bin >= fftSize / 2 - 2) break;
                    for (int d = -2; d <= 2; ++d)
                        residual[(size_t)(bin + d)] *= 0.18f;
                }
            }

            const double time = (double)pos / sampleRate;
            for (int note = minMidi; note <= maxMidi; ++note)
            {
                if (detected[(size_t)note])
                {
                    if (!present[(size_t)note]) starts[(size_t)note] = time;
                    present[(size_t)note] = true;
                    missing[(size_t)note] = 0;
                    confidence[(size_t)note] = juce::jmax(confidence[(size_t)note], frameConfidence[(size_t)note]);
                }
                else if (present[(size_t)note] && ++missing[(size_t)note] >= 2)
                {
                    const double end = time - (double)missing[(size_t)note] * hop / sampleRate;
                    const double duration = end - starts[(size_t)note];
                    if (duration >= 0.055)
                        events.push_back({ note, starts[(size_t)note], duration, confidence[(size_t)note] });
                    present[(size_t)note] = false;
                    missing[(size_t)note] = 0;
                    confidence[(size_t)note] = 0.0f;
                }
            }
        }

        const double endTime = (double)length / sampleRate;
        for (int note = minMidi; note <= maxMidi; ++note)
            if (present[(size_t)note] && endTime - starts[(size_t)note] >= 0.055)
                events.push_back({ note, starts[(size_t)note], endTime - starts[(size_t)note], confidence[(size_t)note] });

        std::sort(events.begin(), events.end(), [](const Event& a, const Event& b)
        {
            return a.start == b.start ? a.midi < b.midi : a.start < b.start;
        });
        return events;
    }
};
