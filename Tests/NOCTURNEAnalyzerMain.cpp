#include "PluginProcessor.h"
#include <iostream>

int main(int argc, char* argv[])
{
    juce::ScopedJuceInitialiser_GUI juceInit;
    if (argc < 4)
    {
        std::cerr << "Usage: NOCTURNEAnalyzer <input-audio> <output-midi> <mode: mono|poly>\n";
        return 2;
    }

    const juce::File input(argv[1]);
    const juce::File output(argv[2]);
    const juce::String mode(argv[3]);

    if (!input.existsAsFile())
    {
        std::cerr << "Input audio does not exist: " << input.getFullPathName() << "\n";
        return 3;
    }
    if (mode != "mono" && mode != "poly")
    {
        std::cerr << "Mode must be mono or poly\n";
        return 4;
    }

    NocturneAudioProcessor processor;
    processor.parameters.getParameter("analysisMode")->setValueNotifyingHost(mode == "poly" ? 1.0f : 0.0f);
    processor.parameters.getParameter("inputGateDb")->setValueNotifyingHost(
        processor.parameters.getParameterRange("inputGateDb").convertTo0to1(-48.0f));
    processor.bpm.store(120.0f);
    processor.quantizeDivision.store(0);

    const int count = processor.analyseFile(input);
    if (count < 0 || !output.getParentDirectory().createDirectory().wasOk())
    {
        std::cerr << "Could not prepare output directory\n";
        return 5;
    }

    processor.exportMidi(output);
    if (!output.existsAsFile() || output.getSize() <= 14)
    {
        std::cerr << "Analysis produced no MIDI output (detected notes: " << count << ")\n";
        return 6;
    }

    std::cout << "Detected " << count << " notes; wrote " << output.getFullPathName() << "\n";
    return 0;
}
