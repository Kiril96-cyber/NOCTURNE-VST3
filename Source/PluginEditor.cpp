#include "PluginEditor.h"
#include <BinaryData.h>

#if JUCE_WINDOWS
 #ifndef NOMINMAX
  #define NOMINMAX
 #endif
 #include <windows.h>
 #include <ole2.h>
 #include <shlobj.h>
 #include <cstring>
 #include <cwchar>
 #include <atomic>

namespace
{
class NocturneFileDropSource final : public IDropSource
{
public:
    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID iid, void** object) override
    {
        if (object == nullptr)
            return E_POINTER;
        *object = nullptr;
        if (IsEqualIID(iid, IID_IUnknown) || IsEqualIID(iid, IID_IDropSource))
        {
            *object = static_cast<IDropSource*>(this);
            AddRef();
            return S_OK;
        }
        return E_NOINTERFACE;
    }

    ULONG STDMETHODCALLTYPE AddRef() override { return ++references; }

    ULONG STDMETHODCALLTYPE Release() override
    {
        const ULONG remaining = --references;
        if (remaining == 0)
            delete this;
        return remaining;
    }

    HRESULT STDMETHODCALLTYPE QueryContinueDrag(BOOL escapePressed, DWORD keyState) override
    {
        if (escapePressed != FALSE)
            return DRAGDROP_S_CANCEL;
        if ((keyState & MK_LBUTTON) == 0)
            return DRAGDROP_S_DROP;
        return S_OK;
    }

    HRESULT STDMETHODCALLTYPE GiveFeedback(DWORD) override
    {
        return DRAGDROP_S_USEDEFAULTCURSORS;
    }

private:
    std::atomic<ULONG> references { 1 };
};

bool performNativeMidiFileDrag(const juce::File& file)
{
    if (!file.existsAsFile())
        return false;

    const HRESULT oleInit = ::OleInitialize(nullptr);
    if (oleInit == RPC_E_CHANGED_MODE)
    {
        juce::StringArray files;
        files.add(file.getFullPathName());
        return juce::DragAndDropContainer::performExternalDragDropOfFiles(files, false);
    }
    if (FAILED(oleInit))
        return false;

    const juce::String fullPath = file.getFullPathName();
    PIDLIST_ABSOLUTE absoluteItem = nullptr;
    HRESULT hr = ::SHParseDisplayName(fullPath.toWideCharPointer(),
                                      nullptr, &absoluteItem, 0, nullptr);
    if (FAILED(hr) || absoluteItem == nullptr)
    {
        ::OleUninitialize();
        return false;
    }

    PIDLIST_ABSOLUTE parentFolder = ::ILCloneFull(absoluteItem);
    PCUITEMID_CHILD childItem = ::ILFindLastID(absoluteItem);
    if (parentFolder == nullptr || childItem == nullptr || !::ILRemoveLastID(parentFolder))
    {
        if (parentFolder != nullptr)
            ::ILFree(parentFolder);
        ::ILFree(absoluteItem);
        ::OleUninitialize();
        return false;
    }

    PCUITEMID_CHILD childItems[1] = { childItem };
    IDataObject* dataObject = nullptr;
    hr = ::SHCreateDataObject(parentFolder, 1, childItems, nullptr,
                              IID_IDataObject, reinterpret_cast<void**>(&dataObject));

    ::ILFree(parentFolder);
    ::ILFree(absoluteItem);

    if (FAILED(hr) || dataObject == nullptr)
    {
        ::OleUninitialize();
        return false;
    }

    // Explicitly expose CF_HDROP so FL Studio receives the same shell file
    // descriptor type used when a .mid is dragged from Windows Explorer.
    const auto* widePath = fullPath.toWideCharPointer();
    const size_t pathChars = std::wcslen(widePath);
    const SIZE_T allocationSize = sizeof(DROPFILES) + (pathChars + 2) * sizeof(wchar_t);
    HGLOBAL memory = ::GlobalAlloc(GMEM_MOVEABLE | GMEM_ZEROINIT, allocationSize);

    bool hasHDrop = false;
    if (memory != nullptr)
    {
        auto* dropFiles = static_cast<DROPFILES*>(::GlobalLock(memory));
        if (dropFiles != nullptr)
        {
            dropFiles->pFiles = sizeof(DROPFILES);
            dropFiles->fWide = TRUE;
            auto* pathBuffer = reinterpret_cast<wchar_t*>(
                reinterpret_cast<BYTE*>(dropFiles) + dropFiles->pFiles);
            std::memcpy(pathBuffer, widePath, (pathChars + 1) * sizeof(wchar_t));
            pathBuffer[pathChars + 1] = L'\0';
            ::GlobalUnlock(memory);

            FORMATETC format {};
            format.cfFormat = CF_HDROP;
            format.dwAspect = DVASPECT_CONTENT;
            format.lindex = -1;
            format.tymed = TYMED_HGLOBAL;

            STGMEDIUM medium {};
            medium.tymed = TYMED_HGLOBAL;
            medium.hGlobal = memory;

            if (SUCCEEDED(dataObject->SetData(&format, &medium, TRUE)))
                hasHDrop = true;
        }

        if (!hasHDrop)
            ::GlobalFree(memory);
    }

    if (!hasHDrop)
    {
        dataObject->Release();
        ::OleUninitialize();
        return false;
    }

    auto* dropSource = new NocturneFileDropSource();
    DWORD effect = DROPEFFECT_NONE;
    const HRESULT dragResult = ::DoDragDrop(dataObject, dropSource,
                                            DROPEFFECT_COPY, &effect);

    dropSource->Release();
    dataObject->Release();
    ::OleUninitialize();

    return dragResult == DRAGDROP_S_DROP && (effect & DROPEFFECT_COPY) != 0;
}
}
#endif

static const juce::Colour bg(0xff101016);
static const juce::Colour panel(0xff191923);
static const juce::Colour line(0xff343044);
static const juce::Colour violet(0xff9c83c7);
static const juce::Colour text(0xffe8e3ef);
static const juce::Colour muted(0xff938ba3);
static const juce::Colour waveformColour(0xff6c5b88);
static const juce::Colour liveColour(0xffb39adf);

static juce::Array<int> scaleSemitones(int scaleId)
{
    switch (scaleId)
    {
        case 1:  return { 0, 2, 4, 5, 7, 9, 11 };      // Major
        case 2:  return { 0, 2, 3, 5, 7, 8, 10 };       // Natural minor
        case 3:  return { 0, 2, 3, 5, 7, 8, 11 };       // Harmonic minor
        case 4:  return { 0, 2, 3, 5, 7, 9, 11 };       // Melodic minor
        case 5:  return { 0, 2, 4, 7, 9 };             // Major pentatonic
        case 6:  return { 0, 3, 5, 7, 10 };            // Minor pentatonic
        case 7:  return { 0, 3, 5, 6, 7, 10 };         // Blues
        case 8:  return { 0, 2, 3, 5, 7, 9, 10 };       // Dorian
        case 9:  return { 0, 1, 3, 5, 7, 8, 10 };       // Phrygian
        case 10: return { 0, 2, 4, 6, 7, 9, 11 };      // Lydian
        case 11: return { 0, 2, 4, 5, 7, 9, 10 };      // Mixolydian
        case 12: return { 0, 1, 3, 5, 6, 8, 10 };      // Locrian
        case 13: return { 0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11 }; // Chromatic
        default: return { 0, 2, 4, 5, 7, 9, 11 };      // Major
    }
}

static juce::String detectedKeyText(const NocturneAudioProcessor& processor)
{
    static const juce::StringArray pitchNames {
        "C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B"
    };
    const int root = processor.detectedKeyRoot.load();
    if (!juce::isPositiveAndBelow(root, 12))
        return "KEY  --";

    const juce::String mode = processor.detectedKeyMode.load() == 1 ? "MINOR" : "MAJOR";
    const int confidence = juce::jlimit(0, 100,
        (int)std::round(processor.detectedKeyConfidence.load() * 100.0f));
    return "KEY  " + pitchNames[root] + " " + mode + "  " + juce::String(confidence) + "%";
}

NocturneAudioProcessorEditor::NocturneAudioProcessorEditor(NocturneAudioProcessor& p)
    : AudioProcessorEditor(&p), processor(p)
{
    setSize(1040, 740);

    addAndMakeVisible(presetLabel);
    presetLabel.setText("PRESET", juce::dontSendNotification);
    presetLabel.setJustificationType(juce::Justification::centred);
    presetLabel.setColour(juce::Label::textColourId, muted);
    presetSelector.addItem("VOCAL  /  NATURAL", 1);
    presetSelector.addItem("VOCAL  /  SOFT", 2);
    presetSelector.addItem("VOCAL  /  BELTING", 3);
    presetSelector.addItem("VOCAL  /  RAP", 4);
    presetSelector.addItem("VOCAL  /  LOW", 5);
    presetSelector.addItem("GUITAR  /  CLEAN", 6);
    presetSelector.addItem("GUITAR  /  DISTORTED", 7);
    presetSelector.addItem("PIANO  /  POLYPHONIC", 8);
    presetSelector.addItem("E-PIANO  /  POLYPHONIC", 9);
    presetSelector.addItem("STRINGS  /  PAD", 10);
    presetSelector.setSelectedId(1, juce::dontSendNotification);
    presetSelector.setJustificationType(juce::Justification::centred);
    presetSelector.setColour(juce::ComboBox::backgroundColourId, panel);
    presetSelector.setColour(juce::ComboBox::textColourId, text);
    presetSelector.onChange = [this]
    {
        applyPreset(presetSelector.getSelectedId() - 1);
        settingsHint = 1.0f;
        settingsOpen = true;
    };
    addAndMakeVisible(presetSelector);

    auto setupPresetArrow = [&](juce::TextButton& button, const juce::String& glyph)
    {
        button.setButtonText(glyph);
        button.setColour(juce::TextButton::buttonColourId, panel);
        button.setColour(juce::TextButton::buttonOnColourId, violet.withAlpha(0.35f));
        button.setColour(juce::TextButton::textColourOffId, juce::Colour(0xffd9caff));
        button.setColour(juce::TextButton::textColourOnId, text);
        button.setTooltip(glyph == juce::String("‹") ? "Previous preset" : "Next preset");
        addAndMakeVisible(button);
    };
    setupPresetArrow(previousPresetButton, "‹");
    setupPresetArrow(nextPresetButton, "›");
    previousPresetButton.onClick = [this]
    {
        const int next = presetSelector.getSelectedId() <= 1 ? presetSelector.getNumItems()
                                                              : presetSelector.getSelectedId() - 1;
        presetSelector.setSelectedId(next, juce::sendNotification);
    };
    nextPresetButton.onClick = [this]
    {
        const int next = presetSelector.getSelectedId() >= presetSelector.getNumItems() ? 1
                                                                                         : presetSelector.getSelectedId() + 1;
        presetSelector.setSelectedId(next, juce::sendNotification);
    };

    // Musical key and scale controls for Piano Roll guidance and optional note snapping.
    keyLabel.setText("KEY", juce::dontSendNotification);
    scaleLabel.setText("SCALE", juce::dontSendNotification);
    for (auto* label : { &keyLabel, &scaleLabel })
    {
        addAndMakeVisible(*label);
        label->setColour(juce::Label::textColourId, muted);
        label->setFont(juce::Font(8.5f, juce::Font::bold));
    }
    static const char* keyNames[] = { "C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B" };
    for (int i = 0; i < 12; ++i)
        keySelector.addItem(keyNames[i], i + 1);
    keySelector.setSelectedId(1, juce::dontSendNotification);
    scaleSelector.addItem("Major", 1);
    scaleSelector.addItem("Natural Minor", 2);
    scaleSelector.addItem("Harmonic Minor", 3);
    scaleSelector.addItem("Melodic Minor", 4);
    scaleSelector.addItem("Major Pentatonic", 5);
    scaleSelector.addItem("Minor Pentatonic", 6);
    scaleSelector.addItem("Blues", 7);
    scaleSelector.addItem("Dorian", 8);
    scaleSelector.addItem("Phrygian", 9);
    scaleSelector.addItem("Lydian", 10);
    scaleSelector.addItem("Mixolydian", 11);
    scaleSelector.addItem("Locrian", 12);
    scaleSelector.addItem("Chromatic", 13);
    scaleSelector.setSelectedId(1, juce::dontSendNotification);
    for (auto* combo : { &keySelector, &scaleSelector })
    {
        addAndMakeVisible(*combo);
        combo->setColour(juce::ComboBox::backgroundColourId, panel);
        combo->setColour(juce::ComboBox::textColourId, text);
        combo->setColour(juce::ComboBox::outlineColourId, line);
        combo->onChange = [this] { repaint(); };
    }
    scaleSnapButton.setColour(juce::ToggleButton::textColourId, text);
    scaleSnapButton.setColour(juce::ToggleButton::tickColourId, violet);
    scaleSnapButton.setTooltip("When enabled, newly created and moved notes snap to the selected scale");
    addAndMakeVisible(scaleSnapButton);

    addAndMakeVisible(pianoLockButton);
    pianoLockButton.onChange = [this](bool locked)
    {
        statusLabel.setText(locked ? "PIANO ROLL LOCKED" : "PIANO ROLL EDITING ENABLED",
                            juce::dontSendNotification);
        repaint();
    };

    pianoScrollXLabel.setText("X  /  TIME", juce::dontSendNotification);
    pianoScrollYLabel.setText("Y", juce::dontSendNotification);
    for (auto* label : { &pianoScrollXLabel, &pianoScrollYLabel })
    {
        addAndMakeVisible(*label);
        label->setColour(juce::Label::textColourId, muted);
        label->setFont(juce::Font(9.0f, juce::Font::bold));
    }

    addAndMakeVisible(pianoScrollX);
    pianoScrollX.setSliderStyle(juce::Slider::LinearHorizontal);
    pianoScrollX.setTextBoxStyle(juce::Slider::NoTextBox, false, 0, 0);
    pianoScrollX.setRange(0.0, 1.0, 0.001);
    pianoScrollX.setValue(0.0, juce::dontSendNotification);
    pianoScrollX.setColour(juce::Slider::trackColourId, violet);
    pianoScrollX.setColour(juce::Slider::thumbColourId, juce::Colour(0xffc9a8ff));
    pianoScrollX.setTooltip("Scroll Piano Roll horizontally through the timeline");
    pianoScrollX.onValueChange = [this] { repaint(); };

    addAndMakeVisible(pianoScrollY);
    pianoScrollY.setSliderStyle(juce::Slider::LinearVertical);
    pianoScrollY.setTextBoxStyle(juce::Slider::NoTextBox, false, 0, 0);
    pianoScrollY.setRange(23.0, 127.0, 1.0);
    pianoScrollY.setValue(84.0, juce::dontSendNotification);
    pianoScrollY.setColour(juce::Slider::trackColourId, violet);
    pianoScrollY.setColour(juce::Slider::thumbColourId, juce::Colour(0xffc9a8ff));
    pianoScrollY.setTooltip("Scroll Piano Roll vertically through pitches");
    pianoScrollY.onValueChange = [this] { repaint(); };

    auto setupButton = [&](juce::TextButton& button)
    {
        addAndMakeVisible(button);
        button.setColour(juce::TextButton::buttonColourId, panel);
        button.setColour(juce::TextButton::textColourOffId, text);
        button.setColour(juce::TextButton::buttonOnColourId, violet);
    };

    setupButton(importButton);
    setupButton(reanalyzeButton);
    reanalyzeButton.setTooltip("Run analysis again using the audio already loaded in memory");
    reanalyzeButton.onClick = [this]
    {
        statusLabel.setText("RE-ANALYSING CACHED AUDIO...", juce::dontSendNotification);
        const int count = processor.analyseLastAudio();
        statusLabel.setText(count > 0 ? "RE-ANALYSIS COMPLETE - " + juce::String(count) + " NOTES"
                                      : "RE-ANALYSIS FAILED - IMPORT AUDIO FIRST",
                            juce::dontSendNotification);
        repaint();
    };
    setupButton(recordButton);
    setupButton(exportButton);
    setupButton(clearButton);
    setupButton(undoButton);
    setupButton(redoButton);
    undoButton.setColour(juce::TextButton::buttonColourId, juce::Colour(0xff26212f).withAlpha(0.72f));
    redoButton.setColour(juce::TextButton::buttonColourId, juce::Colour(0xff26212f).withAlpha(0.72f));
    undoButton.onClick = [this] { undoEdit(); };
    redoButton.onClick = [this] { redoEdit(); };
    setWantsKeyboardFocus(true);

    addAndMakeVisible(dragMidiButton);
    dragMidiButton.createMidiFile = [this] { return processor.createTemporaryMidiFile(); };
   #if JUCE_WINDOWS
    dragMidiButton.performNativeMidiDrag = [](const juce::File& file)
    {
        return performNativeMidiFileDrag(file);
    };
   #endif
    dragMidiButton.dragResult = [this](bool started)
    {
        statusLabel.setText(started
            ? "DRAG MIDI INTO FL STUDIO"
            : "MIDI DRAG COULD NOT START",
            juce::dontSendNotification);
    };

    importButton.onClick = [this] { chooseAudio(); };
    recordButton.onClick = [this]
    {
        const bool shouldStart = !processor.recording.load();
        if (shouldStart)
        {
            processor.clearNotes();
            processor.setRecording(true);
            pianoScrollX.setValue(1.0, juce::dontSendNotification);
            statusLabel.setText("RECORDING - SING OR PLAY INTO THE INPUT", juce::dontSendNotification);
        }
        else
        {
            processor.setRecording(false);
            statusLabel.setText("STOPPING RECORDING...", juce::dontSendNotification);
        }
        repaint();
    };
    exportButton.onClick = [this] { saveMidi(); };
    clearButton.onClick = [this]
    {
        processor.setRecording(false);
        processor.clearNotes();
        statusLabel.setText("NOTES CLEARED", juce::dontSendNotification);
        repaint();
    };

    auto setupSlider = [&](juce::Slider& slider, juce::Label& label,
                           const juce::String& name, double minimum,
                           double maximum, double initial)
    {
        addAndMakeVisible(slider);
        addAndMakeVisible(label);
        label.setText(name, juce::dontSendNotification);
        label.setColour(juce::Label::textColourId, muted);
        slider.setSliderStyle(juce::Slider::LinearHorizontal);
        slider.setTextBoxStyle(juce::Slider::TextBoxRight, false, 54, 21);
        slider.setRange(minimum, maximum, 0.1);
        slider.setValue(initial);
        slider.setColour(juce::Slider::trackColourId, violet);
        slider.setColour(juce::Slider::textBoxTextColourId, text);
    };

    setupSlider(speed, speedLabel, "READ SPEED", 10, 100, 45);
    setupSlider(accuracy, accuracyLabel, "NOTE ACCURACY", 0.50, 0.93, 0.75);
    setupSlider(glide, glideLabel, "PITCH GLIDE", 0, 100, 35);
    setupSlider(vibrato, vibratoLabel, "VIBRATO TOLERANCE", 0, 100, 55);
    setupSlider(stability, stabilityLabel, "TRANSITION STABILITY", 0, 100, 35);
    setupSlider(tempo, tempoLabel, "PROJECT BPM", 40, 240, 120);
    setupSlider(inputGate, inputGateLabel, "INPUT GATE (dB)", -60, -6, -42);
    inputGate.setTextValueSuffix(" dB");
    setupSlider(confirmationDelay, confirmationDelayLabel, "NOTE CONFIRM DELAY", 0, 200, 0);
    confirmationDelay.setTextValueSuffix(" ms");

    speed.onValueChange = [this]
    {
        if (auto* parameter = processor.parameters.getParameter("speed"))
            parameter->setValueNotifyingHost(parameter->convertTo0to1((float)speed.getValue()));
    };
    accuracy.onValueChange = [this]
    {
        if (auto* parameter = processor.parameters.getParameter("accuracy"))
            parameter->setValueNotifyingHost(parameter->convertTo0to1((float)accuracy.getValue()));
    };
    glide.onValueChange = [this]
    {
        if (auto* parameter = processor.parameters.getParameter("glide"))
            parameter->setValueNotifyingHost(parameter->convertTo0to1((float)glide.getValue()));
    };
    vibrato.onValueChange = [this]
    {
        if (auto* parameter = processor.parameters.getParameter("vibrato"))
            parameter->setValueNotifyingHost(parameter->convertTo0to1((float)vibrato.getValue()));
    };
    stability.onValueChange = [this]
    {
        if (auto* parameter = processor.parameters.getParameter("stability"))
            parameter->setValueNotifyingHost(parameter->convertTo0to1((float)stability.getValue()));
    };
    tempo.onValueChange = [this] { processor.bpm.store((float)tempo.getValue()); };
    inputGate.onValueChange = [this]
    {
        if (auto* parameter = processor.parameters.getParameter("inputGateDb"))
            parameter->setValueNotifyingHost(
                parameter->convertTo0to1((float)inputGate.getValue()));
    };
    confirmationDelay.onValueChange = [this]
    {
        if (auto* parameter = processor.parameters.getParameter("confirmationDelayMs"))
            parameter->setValueNotifyingHost(
                parameter->convertTo0to1((float)confirmationDelay.getValue()));
    };

    addAndMakeVisible(quantize);
    addAndMakeVisible(quantizeLabel);
    quantizeLabel.setText("MIDI QUANTIZE", juce::dontSendNotification);
    quantizeLabel.setColour(juce::Label::textColourId, muted);
    quantize.addItem("OFF", 1);
    quantize.addItem("1/4", 2);
    quantize.addItem("1/8", 3);
    quantize.addItem("1/16", 4);
    quantize.addItem("1/32", 5);
    quantize.setSelectedId(1);
    quantize.onChange = [this]
    {
        const int id = quantize.getSelectedId();
        const int divisions[] = { 0, 4, 8, 16, 32 };
        processor.quantizeDivision.store(divisions[juce::jlimit(0, 4, id - 1)]);
    };

    addAndMakeVisible(chordMode);
    addAndMakeVisible(chordModeLabel);
    chordModeLabel.setText("CHORD MODE", juce::dontSendNotification);
    chordModeLabel.setColour(juce::Label::textColourId, muted);
    const juce::String chordNames[] = { "OFF", "MAJOR", "MINOR", "DIMINISHED", "AUGMENTED", "SUS2", "SUS4" };
    for (int i = 0; i < 7; ++i)
        chordMode.addItem(chordNames[i], i + 1);
    chordMode.setSelectedId(1);
    chordMode.onChange = [this]
    {
        if (auto* parameter = processor.parameters.getParameter("chordMode"))
            parameter->setValueNotifyingHost(parameter->convertTo0to1((float)(chordMode.getSelectedId() - 1)));
    };

    addAndMakeVisible(analysisMode);
    addAndMakeVisible(analysisModeLabel);
    analysisModeLabel.setText("ANALYSIS MODE", juce::dontSendNotification);
    analysisModeLabel.setColour(juce::Label::textColourId, muted);
    analysisMode.addItem("VOCAL", 1);
    analysisMode.addItem("POLYPHONIC", 2);
    analysisMode.setSelectedId(1);
    analysisMode.onChange = [this]
    {
        if (auto* parameter = processor.parameters.getParameter("analysisMode"))
            parameter->setValueNotifyingHost(
                parameter->convertTo0to1((float)(analysisMode.getSelectedId() - 1)));
    };

    addAndMakeVisible(pitchLabel);
    pitchLabel.setColour(juce::Label::textColourId, violet);
    pitchLabel.setFont(juce::Font(18.0f));

    addAndMakeVisible(statusLabel);
    statusLabel.setColour(juce::Label::textColourId, muted);
    statusLabel.setFont(juce::Font(11.0f));

    startTimerHz(20);
}

void NocturneAudioProcessorEditor::loadWaveform(const juce::File& file)
{
    waveform.clear();
    juce::AudioFormatManager formats;
    formats.registerBasicFormats();
    std::unique_ptr<juce::AudioFormatReader> reader(formats.createReaderFor(file));
    if (!reader)
        return;

    constexpr int points = 1600;
    juce::AudioBuffer<float> block(1, 1);
    const int64 total = reader->lengthInSamples;
    waveform.reserve(points);

    for (int i = 0; i < points; ++i)
    {
        const int64 position = (total * i) / points;
        if (reader->read(&block, 0, 1, position, true, true))
            waveform.push_back(block.getMagnitude(0, 0, 1));
        else
            waveform.push_back(0.0f);
    }

    importedFileName = file.getFileName();
    repaint();
}

void NocturneAudioProcessorEditor::chooseAudio()
{
    audioFileChooser = std::make_unique<juce::FileChooser>(
        "Import audio", juce::File{}, "*.wav;*.aif;*.aiff;*.flac;*.mp3");

    audioFileChooser->launchAsync(
        juce::FileBrowserComponent::openMode |
        juce::FileBrowserComponent::canSelectFiles,
        [this](const juce::FileChooser& chooser)
        {
            const auto file = chooser.getResult();
            if (file != juce::File{})
            {
                processor.setRecording(false);
                pianoScrollX.setValue(0.0, juce::dontSendNotification);
                statusLabel.setText("ANALYSING: " + file.getFileName(), juce::dontSendNotification);
                loadWaveform(file);
                const int count = processor.analyseFile(file);
                int highestMidi = 60;
                for (const auto& note : processor.getNotesCopy())
                    highestMidi = juce::jmax(highestMidi, note.midi);
                pianoScrollY.setValue(juce::jlimit(36, 127, highestMidi + 2),
                                      juce::dontSendNotification);
                statusLabel.setText(count > 0
                    ? "ANALYSIS COMPLETE - " + juce::String(count) + " NOTES"
                    : "NO NOTES DETECTED - CHECK SOURCE / ANALYSIS MODE",
                    juce::dontSendNotification);
            }
            audioFileChooser.reset();
            repaint();
        });
}

void NocturneAudioProcessorEditor::saveMidi()
{
    midiSaveChooser = std::make_unique<juce::FileChooser>(
        "Export MIDI",
        juce::File::getSpecialLocation(juce::File::userDocumentsDirectory)
            .getChildFile("NOCTURNE.mid"),
        "*.mid");

    midiSaveChooser->launchAsync(
        juce::FileBrowserComponent::saveMode |
        juce::FileBrowserComponent::canSelectFiles,
        [this](const juce::FileChooser& chooser)
        {
            const auto file = chooser.getResult();
            if (file != juce::File{})
            {
                processor.exportMidi(file.withFileExtension("mid"));
                statusLabel.setText("MIDI EXPORTED", juce::dontSendNotification);
            }
            midiSaveChooser.reset();
        });
}

bool NocturneAudioProcessorEditor::isPitchInSelectedScale(int midi) const
{
    const int root = juce::jlimit(0, 11, keySelector.getSelectedId() - 1);
    const int relativePitch = (midi - root + 120) % 12;
    return scaleSemitones(scaleSelector.getSelectedId()).contains(relativePitch);
}

int NocturneAudioProcessorEditor::snapMidiToSelectedScale(int midi) const
{
    if (!scaleSnapButton.getToggleState())
        return juce::jlimit(0, 127, midi);

    midi = juce::jlimit(0, 127, midi);
    for (int distance = 0; distance <= 6; ++distance)
    {
        const int lower = midi - distance;
        if (lower >= 0 && isPitchInSelectedScale(lower))
            return lower;
        const int upper = midi + distance;
        if (distance > 0 && upper <= 127 && isPitchInSelectedScale(upper))
            return upper;
    }
    return midi;
}

void NocturneAudioProcessorEditor::applyPreset(int presetIndex)
{
    struct Preset
    {
        float speed;
        float accuracy;
        float glide;
        float vibrato;
        float stability;
        float gateDb;
        int analysisMode; // 0 = vocal/monophonic, 1 = polyphonic
    };

    static constexpr Preset presets[] = {
        { 55.0f, 0.75f, 35.0f, 55.0f, 35.0f, -42.0f, 0 }, // Vocal natural
        { 52.0f, 0.66f, 52.0f, 72.0f, 24.0f, -52.0f, 0 }, // Soft/breathy
        { 55.0f, 0.84f, 22.0f, 42.0f, 43.0f, -35.0f, 0 }, // Belting
        { 92.0f, 0.68f, 8.0f, 18.0f, 12.0f, -39.0f, 0 },  // Rap/fast syllables
        { 48.0f, 0.78f, 36.0f, 48.0f, 38.0f, -43.0f, 0 }, // Low voice
        { 58.0f, 0.72f, 22.0f, 32.0f, 46.0f, -42.0f, 1 }, // Clean guitar
        { 62.0f, 0.70f, 18.0f, 28.0f, 55.0f, -35.0f, 1 }, // Distorted guitar
        { 48.0f, 0.62f, 20.0f, 12.0f, 54.0f, -43.0f, 1 }, // Piano
        { 52.0f, 0.64f, 25.0f, 18.0f, 50.0f, -46.0f, 1 }, // Electric piano
        { 42.0f, 0.68f, 48.0f, 58.0f, 62.0f, -50.0f, 1 }  // Strings/pad
    };

    if (presetIndex < 0 || presetIndex >= 10)
        return;

    const auto& preset = presets[presetIndex];
    speed.setValue(preset.speed, juce::sendNotificationSync);
    accuracy.setValue(preset.accuracy, juce::sendNotificationSync);
    glide.setValue(preset.glide, juce::sendNotificationSync);
    vibrato.setValue(preset.vibrato, juce::sendNotificationSync);
    stability.setValue(preset.stability, juce::sendNotificationSync);
    inputGate.setValue(preset.gateDb, juce::sendNotificationSync);
    analysisMode.setSelectedId(preset.analysisMode + 1, juce::sendNotificationSync);
    statusLabel.setText("PRESET APPLIED", juce::dontSendNotification);
}

void NocturneAudioProcessorEditor::timerCallback()
{
    const float hz = processor.currentPitchHz.load();
    const bool isRecording = processor.recording.load();
    const float level = processor.inputLevel.load();
    neonPhase += 0.075f;

    // Reveal the settings drawer when the pointer reaches the right edge or enters it.
    const auto pointer = getMouseXYRelative();
    settingsOpen = pointer.x >= getWidth() - 22 ||
                   (settingsReveal > 0.02f && pointer.x >= getWidth() - 278);
    const float targetReveal = settingsOpen ? 1.0f : 0.0f;
    settingsReveal += (targetReveal - settingsReveal) * 0.24f;
    if (std::abs(settingsReveal - targetReveal) < 0.008f)
        settingsReveal = targetReveal;
    settingsHint = juce::jmax(0.0f, settingsHint - 0.018f);
    resized();
    if (neonPhase > juce::MathConstants<float>::twoPi)
        neonPhase -= juce::MathConstants<float>::twoPi;

    recordButton.setButtonText(isRecording ? "STOP" : "RECORD");

    if (hz > 0.0f)
    {
        const int midi = juce::jlimit(0, 127, (int)std::round(69.0 + 12.0 * std::log2(hz / 440.0)));
        const auto noteName = juce::MidiMessage::getMidiNoteName(midi, true, true, 3);
        pitchLabel.setText(juce::String(hz, 1) + " Hz  |  " + noteName, juce::dontSendNotification);
    }
    else
    {
        pitchLabel.setText("NO STABLE PITCH - CHECK AUDIO INPUT", juce::dontSendNotification);
    }

    if (isRecording)
    {
        statusLabel.setText("RECORDING  |  INPUT " + juce::String(level, 3) +
                            "  |  " + juce::String(processor.getNotesCopy().size()) + " NOTES",
                            juce::dontSendNotification);
    }
    else if (level < 0.0005f)
    {
        statusLabel.setText("READY  |  ROUTE VOCAL AUDIO TO THIS TRACK",
                            juce::dontSendNotification);
    }
    else
    {
        statusLabel.setText("INPUT DETECTED  |  PRESS RECORD TO CAPTURE",
                            juce::dontSendNotification);
    }

    repaint();
}

juce::Rectangle<float> NocturneAudioProcessorEditor::pianoBounds() const
{
    return { 30.0f, 312.0f, 680.0f, 270.0f };
}

int NocturneAudioProcessorEditor::pianoTopMidi() const
{
    return juce::jlimit(23, 127, (int)std::round(pianoScrollY.getValue()));
}

void NocturneAudioProcessorEditor::paint(juce::Graphics& g)
{
    g.fillAll(bg);

    const float drawerX = (float)getWidth() - 260.0f * settingsReveal;
    const juce::Rectangle<float> drawerBounds(drawerX, 88.0f, 248.0f, 584.0f);
    if (settingsReveal > 0.001f)
    {
        g.setColour(juce::Colour(0xff17151f).withAlpha(0.97f * settingsReveal));
        g.fillRoundedRectangle(drawerBounds, 12.0f);
        g.setColour(violet.withAlpha(0.35f * settingsReveal));
        g.drawRoundedRectangle(drawerBounds, 12.0f, 1.0f);
        g.setColour(juce::Colour(0xffb68aff).withAlpha((0.25f + settingsHint * 0.65f) * settingsReveal));
        g.fillRoundedRectangle(drawerX + 1.0f, 104.0f, 3.0f, 548.0f, 1.5f);
        g.setColour(juce::Colour(0xffb68aff).withAlpha((0.12f + settingsHint * 0.42f) * settingsReveal));
        g.fillRoundedRectangle(drawerX - 3.0f, 104.0f, 7.0f, 548.0f, 3.0f);
        g.setColour(text.withAlpha(settingsReveal));
        g.setFont(juce::Font(10.5f, juce::Font::bold));
        g.drawText("SETTINGS", juce::Rectangle<int>((int)drawerX + 18, 91, 190, 20), juce::Justification::left);
    }

    // Persistent neon edge cue marks the hidden settings drawer.
    const float edgePulse = 0.5f + 0.5f * std::sin(neonPhase * 1.4f);
    g.setColour(juce::Colour(0xffb68aff).withAlpha(0.45f + settingsHint * 0.45f + edgePulse * 0.12f));
    g.fillRoundedRectangle((float)getWidth() - 3.0f, 112.0f, 2.0f, 500.0f, 1.0f);
    g.setColour(juce::Colour(0xffb68aff).withAlpha(0.10f + settingsHint * 0.22f));
    g.fillRoundedRectangle((float)getWidth() - 7.0f, 112.0f, 6.0f, 500.0f, 3.0f);

    if (settingsReveal > 0.02f)
    {
        const auto& backgroundImage = juce::ImageCache::getFromMemory(
            BinaryData::nocturne_background_jpg, BinaryData::nocturne_background_jpgSize);
        if (backgroundImage.isValid())
        {
            const int artX = getWidth() - juce::roundToInt(252.0f * settingsReveal);
            const juce::Rectangle<int> artBounds(artX + 4, 108, 240, 548);
            g.setColour(bg.withAlpha(0.35f * settingsReveal));
            g.fillRoundedRectangle(artBounds.toFloat(), 12.0f);
            g.setOpacity(0.22f * settingsReveal);
            g.drawImageWithin(backgroundImage,
                              artBounds.getX(), artBounds.getY(),
                              artBounds.getWidth(), artBounds.getHeight(),
                              juce::RectanglePlacement::centred);
            g.setOpacity(1.0f);
            g.setColour(bg.withAlpha(0.10f * settingsReveal));
            g.fillRoundedRectangle(artBounds.toFloat(), 12.0f);
        }
    }

    const juce::Font logoFont(27.0f, juce::Font::bold);
    const juce::String logo = "NOCTURNE";
    float glyphX = 28.0f;
    for (int i = 0; i < logo.length(); ++i)
    {
        const juce::String glyph = logo.substring(i, i + 1);
        const float glyphWidth = logoFont.getStringWidthFloat(glyph);
        const float wave = 0.5f + 0.5f * std::sin(neonPhase - (float)i * 0.68f);
        const auto neon = juce::Colour(0xffb68aff).interpolatedWith(
            juce::Colour(0xff42dcff), wave);
        const juce::Rectangle<int> glyphBounds(
            (int)glyphX, 18, (int)std::ceil(glyphWidth + 2.0f), 38);

        g.setFont(logoFont);
        g.setColour(neon.withAlpha(0.10f + wave * 0.13f));
        g.drawText(glyph, glyphBounds.translated(-1, 0), juce::Justification::centredLeft);
        g.drawText(glyph, glyphBounds.translated(1, 0), juce::Justification::centredLeft);
        g.setColour(neon.withAlpha(0.68f + wave * 0.32f));
        g.drawText(glyph, glyphBounds, juce::Justification::centredLeft);
        glyphX += glyphWidth;
    }

    g.setColour(muted);
    g.setFont(11.0f);
    g.drawText("REAL-TIME VOCAL PITCH TO MIDI", 30, 54, 380, 18, juce::Justification::left);
    g.setColour(line);
    g.drawLine(28, 82, (float)getWidth() - 28, 82, 1.0f);

    g.setColour(panel.withAlpha(0.90f));
    g.fillRoundedRectangle(28, 98, 720, 58, 8);
    g.setColour(muted);
    g.setFont(11.0f);
    g.drawText("LIVE PITCH", 44, 106, 100, 16, juce::Justification::left);
    g.setColour(text);
    g.setFont(16.0f);
    g.drawText(pitchLabel.getText(), 44, 125, 650, 24, juce::Justification::left);

    const juce::Rectangle<float> wave(28, 174, 720, 104);
    g.setColour(panel.withAlpha(0.88f));
    g.fillRoundedRectangle(wave, 8);

    const bool isRecording = processor.recording.load();
    g.setColour(muted);
    g.setFont(10.5f);
    const juce::String waveTitle = isRecording
        ? "LIVE INPUT WAVEFORM  /  RECORDING"
        : (importedFileName.isNotEmpty()
            ? "IMPORTED SOURCE  /  " + importedFileName
            : "LIVE INPUT WAVEFORM");
    g.drawText(waveTitle, 42, 181, 500, 15, juce::Justification::left);

    const float waveLeft = wave.getX() + 12.0f;
    const float waveWidth = wave.getWidth() - 24.0f;
    const float waveMid = wave.getCentreY() + 10.0f;
    const float waveAmp = 32.0f;
    g.setColour(line);
    g.drawLine(waveLeft, waveMid, waveLeft + waveWidth, waveMid, 1.0f);

    juce::Path wavePath;
    if (isRecording)
    {
        const auto snapshot = processor.getLiveWaveformSnapshot();
        for (size_t i = 0; i < snapshot.size(); ++i)
        {
            const float x = waveLeft + waveWidth * (float)i / (float)(snapshot.size() - 1);
            const float sample = juce::jlimit(-1.0f, 1.0f, snapshot[i]);
            const float y = waveMid - sample * waveAmp;
            if (i == 0)
                wavePath.startNewSubPath(x, y);
            else
                wavePath.lineTo(x, y);
        }

        g.setColour(liveColour.withAlpha(0.95f));
        g.strokePath(wavePath, juce::PathStrokeType(1.35f));
    }
    else if (!waveform.empty())
    {
        for (size_t i = 0; i < waveform.size(); ++i)
        {
            const float x = waveLeft + waveWidth * (float)i / (float)(waveform.size() - 1);
            const float y = waveMid - waveform[i] * waveAmp;
            if (i == 0)
                wavePath.startNewSubPath(x, y);
            else
                wavePath.lineTo(x, y);
        }
        for (int i = (int)waveform.size() - 1; i >= 0; --i)
        {
            const float x = waveLeft + waveWidth * (float)i / (float)(waveform.size() - 1);
            wavePath.lineTo(x, waveMid + waveform[(size_t)i] * waveAmp);
        }
        wavePath.closeSubPath();
        g.setColour(waveformColour.withAlpha(0.85f));
        g.fillPath(wavePath);
    }
    else
    {
        g.setColour(muted);
        g.setFont(12.0f);
        g.drawText("Route audio to NOCTURNE to see the live waveform",
                   wave.toNearestInt(), juce::Justification::centred);
    }

    const auto roll = pianoBounds();
    const juce::Rectangle<float> rollFrame(24.0f, 280.0f, 730.0f, 397.0f);
    g.setColour(juce::Colour(0xff17151f).withAlpha(0.96f));
    g.fillRoundedRectangle(rollFrame, 10.0f);
    g.setColour(violet.withAlpha(0.62f));
    g.drawRoundedRectangle(rollFrame, 10.0f, 1.2f);
    g.setColour(panel.withAlpha(0.96f));
    g.fillRoundedRectangle(roll, 6.0f);
    g.setColour(muted);
    g.setFont(10.5f);
    g.drawText("PIANO ROLL",
               40, 288, 105, 15, juce::Justification::left);
    g.drawText(pianoLockButton.isLocked() ? "LOCKED" : "EDIT",
               492, 288, 48, 15, juce::Justification::right);

    const int rows = juce::jlimit(6, 48, (int)std::round(24.0f / pianoZoom));
    const float rowHeight = roll.getHeight() / (float)rows;
    const int topMidi = pianoTopMidi();
    const int selectedRoot = juce::jlimit(0, 11, keySelector.getSelectedId() - 1);
    const auto selectedScale = scaleSemitones(scaleSelector.getSelectedId());
    for (int row = 0; row < rows; ++row)
    {
        const int midi = topMidi - row;
        const int relativePitch = (midi - selectedRoot + 120) % 12;
        if (selectedScale.contains(relativePitch))
        {
            g.setColour(violet.withAlpha(0.075f));
            g.fillRect(roll.getX(), roll.getY() + row * rowHeight, roll.getWidth(), rowHeight);
        }
        else
        {
            g.setColour(juce::Colour(0xff08070d).withAlpha(0.19f));
            g.fillRect(roll.getX(), roll.getY() + row * rowHeight, roll.getWidth(), rowHeight);
        }
    }
    for (int row = 0; row <= rows; ++row)
    {
        const float y = roll.getY() + row * rowHeight;
        g.setColour((row % 12 == 0) ? line.withAlpha(0.9f) : line.withAlpha(0.35f));
        g.drawHorizontalLine((int)y, roll.getX(), roll.getRight());
    }
    for (int column = 1; column < 8; ++column)
    {
        const float x = roll.getX() + roll.getWidth() * column / 8.0f;
        g.setColour(line.withAlpha(0.5f));
        g.drawVerticalLine((int)x, roll.getY(), roll.getBottom());
    }

    const auto noteEvents = processor.getNotesCopy();
    const double recordPosition = processor.recordingPositionSeconds.load();
    double totalTime = 8.0;
    for (const auto& note : noteEvents)
        totalTime = juce::jmax(totalTime, note.start + note.length);
    if (isRecording)
        totalTime = juce::jmax(totalTime, recordPosition + 0.25);

    const double visibleDuration = 8.0 / (double)pianoZoom;
    const double maxScrollStart = juce::jmax(0.0, totalTime - visibleDuration);
    const double visibleStart = maxScrollStart * pianoScrollX.getValue();
    const double visibleEnd = visibleStart + visibleDuration;

    for (const auto& note : noteEvents)
    {
        const double noteEnd = note.start + note.length;
        if (noteEnd < visibleStart || note.start > visibleEnd)
            continue;

        const float x1 = roll.getX() + 8.0f +
            (float)((note.start - visibleStart) / visibleDuration) * (roll.getWidth() - 16.0f);
        const float x2 = roll.getX() + 8.0f +
            (float)((noteEnd - visibleStart) / visibleDuration) * (roll.getWidth() - 16.0f);
        const float nx = juce::jmax(roll.getX() + 1.0f, x1);
        const float right = juce::jmin(roll.getRight() - 1.0f, x2);
        const float nw = juce::jmax(3.0f, right - nx);
        const float ny = roll.getY() + ((float)(topMidi - note.midi) + 0.5f) * rowHeight;
        if (ny < roll.getY() || ny + rowHeight > roll.getBottom())
            continue;

        const float velocityAlpha = 0.30f + 0.70f *
            (float)juce::jlimit(1, 127, note.velocity) / 127.0f;
        g.setColour(violet.withAlpha(velocityAlpha));
        g.fillRoundedRectangle(nx, ny - rowHeight * 0.34f, nw, rowHeight * 0.68f, 3.0f);
        if (nw > 28.0f)
        {
            g.setColour(text);
            g.setFont(9.0f);
            g.drawText(juce::MidiMessage::getMidiNoteName(note.midi, true, true, 3),
                       (int)nx + 3, (int)(ny - rowHeight * 0.34f),
                       (int)nw - 5, (int)(rowHeight * 0.68f),
                       juce::Justification::centredLeft);
        }
    }


    // Editable velocity lane is clipped to the inner frame so bars cannot spill outside.
    const juce::Rectangle<float> velocityBounds(30.0f, 586.0f, 680.0f, 34.0f);
    {
    juce::Graphics::ScopedSaveState velocityClip(g);
    g.reduceClipRegion(velocityBounds.toNearestInt());
    g.setColour(panel.withAlpha(0.92f));
    g.fillRoundedRectangle(velocityBounds, 6.0f);
    g.setColour(muted);
    g.setFont(9.0f);
    g.drawText("VELOCITY", 36, 588, 58, 12, juce::Justification::left);
    for (const auto& note : noteEvents)
    {
        if (note.start + note.length < visibleStart || note.start > visibleEnd)
            continue;
        const float vx = roll.getX() + 8.0f +
            (float)((note.start - visibleStart) / visibleDuration) * (roll.getWidth() - 16.0f);
        const float barHeight = 20.0f * (float)juce::jlimit(1, 127, note.velocity) / 127.0f;
        g.setColour(juce::Colour(0xff9c79e8).withAlpha(0.92f));
        g.fillRoundedRectangle(vx - 1.5f, 616.0f - barHeight, 3.0f, barHeight, 1.2f);
    }
    }

    if (isRecording)
    {
        const float cursorX = roll.getX() + 8.0f +
            (float)((recordPosition - visibleStart) / visibleDuration) * (roll.getWidth() - 16.0f);
        g.setColour(liveColour);
        g.drawVerticalLine((int)cursorX, roll.getY() + 2.0f, roll.getBottom() - 2.0f);
    }

    g.setColour(muted);
    g.setFont(10.0f);
    g.drawText("NOTES  " + juce::String(noteEvents.size()) +
               "     " + detectedKeyText(processor) +
               "     BPM " + juce::String(processor.bpm.load(), 1),
               42, 662, 640, 11, juce::Justification::left);

    g.setColour(line);
    g.drawLine(28, 676, (float)getWidth() - 28, 676, 1.0f);
}

void NocturneAudioProcessorEditor::resized()
{
    presetLabel.setBounds(300, 5, 420, 16);
    presetSelector.setBounds(330, 22, 360, 32);
    previousPresetButton.setBounds(296, 22, 30, 32);
    nextPresetButton.setBounds(694, 22, 30, 32);
    undoButton.setBounds(548, 285, 58, 20);
    redoButton.setBounds(610, 285, 58, 20);
    keyLabel.setBounds(155, 282, 28, 11);
    keySelector.setBounds(155, 294, 66, 18);
    scaleLabel.setBounds(228, 282, 42, 11);
    scaleSelector.setBounds(228, 294, 142, 18);
    scaleSnapButton.setBounds(376, 293, 62, 19);
    pianoLockButton.setBounds(680, 284, 24, 22);
    pianoScrollYLabel.setBounds(718, 312, 20, 14);
    pianoScrollY.setBounds(720, 332, 18, 246);
    pianoScrollXLabel.setBounds(40, 644, 64, 14);
    pianoScrollX.setBounds(108, 642, 582, 18);

    importButton.setBounds(28, 690, 140, 36);
    recordButton.setBounds(177, 690, 120, 36);
    exportButton.setBounds(306, 690, 130, 36);
    dragMidiButton.setBounds(445, 688, 205, 40);
    clearButton.setBounds(660, 690, 90, 36);
    reanalyzeButton.setBounds(560, 177, 176, 24);

    const int drawerX = getWidth() - juce::roundToInt(260.0f * settingsReveal);
    speedLabel.setBounds(drawerX + 20, 112, 210, 18);
    speed.setBounds(drawerX + 20, 131, 210, 26);
    accuracyLabel.setBounds(drawerX + 20, 168, 210, 18);
    accuracy.setBounds(drawerX + 20, 187, 210, 26);
    glideLabel.setBounds(drawerX + 20, 224, 210, 18);
    glide.setBounds(drawerX + 20, 243, 210, 26);
    vibratoLabel.setBounds(drawerX + 20, 280, 210, 18);
    vibrato.setBounds(drawerX + 20, 299, 210, 26);
    stabilityLabel.setBounds(drawerX + 20, 340, 210, 18);
    stability.setBounds(drawerX + 20, 359, 210, 26);
    tempoLabel.setBounds(drawerX + 20, 398, 210, 18);
    tempo.setBounds(drawerX + 20, 417, 210, 26);
    quantizeLabel.setBounds(drawerX + 20, 456, 210, 18);
    quantize.setBounds(drawerX + 20, 476, 210, 28);
    chordModeLabel.setBounds(drawerX + 20, 514, 210, 18);
    chordMode.setBounds(drawerX + 20, 534, 210, 28);
    analysisModeLabel.setBounds(drawerX + 20, 572, 210, 18);
    analysisMode.setBounds(drawerX + 20, 592, 210, 28);
    inputGateLabel.setBounds(drawerX + 20, 630, 210, 18);
    inputGate.setBounds(drawerX + 20, 649, 210, 26);
    confirmationDelayLabel.setBounds(drawerX + 20, 688, 210, 18);
    confirmationDelay.setBounds(drawerX + 20, 707, 210, 26);
    pitchLabel.setBounds(390, 112, 340, 36);
    statusLabel.setBounds(770, 716, 245, 18);
}

void NocturneAudioProcessorEditor::mouseDown(const juce::MouseEvent& event)
{
    draggedNote = -1;
    velocityNote = -1;
    noteDragMode = NoteDragMode::none;
    if (event.mods.isMiddleButtonDown())
    {
        middlePanning = true;
        panStartPoint = event.position;
        panStartX = pianoScrollX.getValue();
        panStartY = pianoScrollY.getValue();
        return;
    }
    if (pianoLockButton.isLocked()) return;

    const auto notesNow = processor.getNotesCopy();
    const auto roll = pianoBounds();
    const juce::Rectangle<float> velocityBounds(30.0f, 586.0f, 680.0f, 34.0f);
    double totalTime = 8.0;
    for (const auto& n : notesNow) totalTime = juce::jmax(totalTime, n.start + n.length);
    const double visibleDuration = 8.0 / (double)pianoZoom;
    const double visibleStart = juce::jmax(0.0, totalTime - visibleDuration) * pianoScrollX.getValue();
    const int rows = juce::jlimit(6, 48, (int)std::round(24.0f / pianoZoom));
    const float rowHeight = roll.getHeight() / (float)rows;
    const int topMidi = pianoTopMidi();

    if (velocityBounds.contains(event.position))
    {
        int nearest = -1;
        float nearestDistance = 8.0f;
        for (int i = 0; i < notesNow.size(); ++i)
        {
            const float nx = roll.getX() + 8.0f +
                (float)((notesNow.getReference(i).start - visibleStart) / visibleDuration) * (roll.getWidth() - 16.0f);
            const float distance = std::abs(event.position.x - nx);
            if (distance < nearestDistance) { nearest = i; nearestDistance = distance; }
        }
        if (nearest >= 0)
        {
            pushUndoState();
            velocityNote = nearest;
            noteDragMode = NoteDragMode::velocity;
            const int value = juce::jlimit(1, 127,
                (int)std::round((616.0f - event.position.y) / 20.0f * 127.0f));
            processor.setNoteVelocity(nearest, value);
            repaint();
        }
        return;
    }

    if (!roll.contains(event.position)) return;

    for (int i = notesNow.size() - 1; i >= 0; --i)
    {
        const auto& note = notesNow.getReference(i);
        const float x = roll.getX() + 8.0f +
            (float)((note.start - visibleStart) / visibleDuration) * (roll.getWidth() - 16.0f);
        const float width = juce::jmax(8.0f,
            (float)(note.length / visibleDuration) * (roll.getWidth() - 16.0f));
        const float y = roll.getY() + ((float)(topMidi - note.midi) + 0.5f) * rowHeight;
        if (event.position.x >= x && event.position.x <= x + width &&
            std::abs(event.position.y - y) < rowHeight * 0.7f)
        {
            if (event.mods.isRightButtonDown())
            {
                pushUndoState();
                processor.removeNote(i);
                repaint();
                return;
            }
            pushUndoState();
            draggedNote = i;
            originalNoteLength = note.length;
            if (event.position.x >= x + width - 7.0f)
                noteDragMode = NoteDragMode::resize;
            else
            {
                noteDragMode = NoteDragMode::move;
                dragOffsetSeconds = (event.position.x - x) / (roll.getWidth() - 16.0f) * visibleDuration;
            }
            return;
        }
    }
}

void NocturneAudioProcessorEditor::mouseDrag(const juce::MouseEvent& event)
{
    if (middlePanning)
    {
        const auto delta = event.position - panStartPoint;
        pianoScrollX.setValue(juce::jlimit(0.0, 1.0, panStartX - (double)delta.x / 500.0));
        const int rows = juce::jlimit(6, 48, (int)std::round(24.0f / pianoZoom));
        pianoScrollY.setValue(juce::jlimit((double)rows, 127.0, panStartY + (double)delta.y * rows / 270.0));
        repaint();
        return;
    }
    if (pianoLockButton.isLocked()) return;
    if (noteDragMode == NoteDragMode::velocity && velocityNote >= 0)
    {
        const int value = juce::jlimit(1, 127,
            (int)std::round((636.0f - event.position.y) / 20.0f * 127.0f));
        processor.setNoteVelocity(velocityNote, value);
        repaint();
        return;
    }
    if (draggedNote < 0 || noteDragMode == NoteDragMode::none) return;

    const auto roll = pianoBounds();
    const auto noteEvents = processor.getNotesCopy();
    if (!juce::isPositiveAndBelow(draggedNote, noteEvents.size())) return;
    double totalTime = 8.0;
    for (const auto& n : noteEvents) totalTime = juce::jmax(totalTime, n.start + n.length);
    const double visibleDuration = 8.0 / (double)pianoZoom;
    const double visibleStart = juce::jmax(0.0, totalTime - visibleDuration) * pianoScrollX.getValue();
    const double secondsPerPixel = visibleDuration / (double)(roll.getWidth() - 16.0);

    if (noteDragMode == NoteDragMode::resize)
    {
        const double length = originalNoteLength +
            (double)(event.position.x - event.getMouseDownPosition().x) * secondsPerPixel;
        processor.setNoteLength(draggedNote, length);
    }
    else
    {
        const int rows = juce::jlimit(6, 48, (int)std::round(24.0f / pianoZoom));
    const float rowHeight = roll.getHeight() / (float)rows;
        const int midi = snapMidiToSelectedScale(pianoTopMidi() -
            (int)std::floor((event.position.y - roll.getY()) / rowHeight));
        const double startTime = visibleStart +
            ((event.position.x - roll.getX() - 8.0) / (roll.getWidth() - 16.0)) *
            visibleDuration - dragOffsetSeconds;
        processor.moveNote(draggedNote, midi, startTime);
    }
    repaint();
}

void NocturneAudioProcessorEditor::mouseUp(const juce::MouseEvent&)
{
    middlePanning = false;
    draggedNote = -1;
    velocityNote = -1;
    noteDragMode = NoteDragMode::none;
}

void NocturneAudioProcessorEditor::mouseWheelMove(const juce::MouseEvent& event, const juce::MouseWheelDetails& wheel)
{
    const auto roll = pianoBounds();
    const auto velocity = juce::Rectangle<float>(30.0f, 586.0f, 680.0f, 34.0f);
    if (!roll.contains(event.position) && !velocity.contains(event.position))
        return;

    // Wheel zoom is a navigation gesture and deliberately ignores the edit lock.
    const float oldZoom = pianoZoom;
    const float direction = wheel.deltaY > 0.0f ? 1.0f : -1.0f;
    const float newZoom = juce::jlimit(0.5f, 4.0f, oldZoom * std::pow(1.18f, direction));
    if (std::abs(newZoom - oldZoom) < 0.0001f)
        return;

    const auto notes = processor.getNotesCopy();
    double totalTime = 8.0;
    for (const auto& note : notes)
        totalTime = juce::jmax(totalTime, note.start + note.length);

    // Keep the time under the pointer stationary while zooming horizontally.
    const double oldDuration = 8.0 / (double)oldZoom;
    const double xRatio = juce::jlimit(0.0, 1.0,
        (double)(event.position.x - roll.getX() - 8.0f) / (double)(roll.getWidth() - 16.0f));
    const double oldMaxStart = juce::jmax(0.0, totalTime - oldDuration);
    const double anchorTime = oldMaxStart * pianoScrollX.getValue() + xRatio * oldDuration;

    // Keep the pitch under the pointer stationary as the visible pitch range changes.
    const int oldRows = juce::jlimit(6, 48, (int)std::round(24.0f / oldZoom));
    const int oldTop = pianoTopMidi();
    const float oldRowHeight = roll.getHeight() / (float)oldRows;
    const int pitchAtPointer = oldTop - (int)std::floor((event.position.y - roll.getY()) / oldRowHeight);

    pianoZoom = newZoom;
    const double newDuration = 8.0 / (double)pianoZoom;
    const double newMaxStart = juce::jmax(0.0, totalTime - newDuration);
    const double newStart = anchorTime - xRatio * newDuration;
    pianoScrollX.setValue(newMaxStart > 0.0
        ? juce::jlimit(0.0, 1.0, newStart / newMaxStart) : 0.0,
        juce::dontSendNotification);

    const int newRows = juce::jlimit(6, 48, (int)std::round(24.0f / pianoZoom));
    const float newRowHeight = roll.getHeight() / (float)newRows;
    const int newTop = pitchAtPointer +
        (int)std::floor((event.position.y - roll.getY()) / newRowHeight);
    pianoScrollY.setValue(juce::jlimit((double)newRows, 127.0, (double)newTop),
                          juce::dontSendNotification);
    repaint();
}

void NocturneAudioProcessorEditor::mouseDoubleClick(const juce::MouseEvent& event)
{
    if (pianoLockButton.isLocked() || !pianoBounds().contains(event.position)) return;
    const auto roll = pianoBounds();
    double totalTime = 8.0;
    for (const auto& n : processor.getNotesCopy()) totalTime = juce::jmax(totalTime, n.start + n.length);
    const double visibleDuration = 8.0 / (double)pianoZoom;
    const double visibleStart = juce::jmax(0.0, totalTime - visibleDuration) * pianoScrollX.getValue();
    const double startTime = juce::jmax(0.0, visibleStart +
        ((event.position.x - roll.getX() - 8.0) / (roll.getWidth() - 16.0)) * visibleDuration);
    const int visibleRows = juce::jlimit(6, 48, (int)std::round(24.0f / pianoZoom));
    const int midi = snapMidiToSelectedScale(pianoTopMidi() -
        (int)std::floor((event.position.y - roll.getY()) / (roll.getHeight() / (float)visibleRows)));
    pushUndoState();
    processor.addNote(midi, startTime, 0.25, 100);
    repaint();
}

void NocturneAudioProcessorEditor::pushUndoState()
{
    undoHistory.push_back(processor.getNotesCopy());
    if ((int)undoHistory.size() > maxUndoStates) undoHistory.erase(undoHistory.begin());
    redoHistory.clear();
}

void NocturneAudioProcessorEditor::undoEdit()
{
    if (undoHistory.empty()) return;
    redoHistory.push_back(processor.getNotesCopy());
    processor.replaceNotes(undoHistory.back());
    undoHistory.pop_back();
    repaint();
}

void NocturneAudioProcessorEditor::redoEdit()
{
    if (redoHistory.empty()) return;
    undoHistory.push_back(processor.getNotesCopy());
    processor.replaceNotes(redoHistory.back());
    redoHistory.pop_back();
    repaint();
}

bool NocturneAudioProcessorEditor::keyPressed(const juce::KeyPress& key)
{
    if (key.getModifiers().isCommandDown() && key.getKeyCode() == 'Z')
    {
        if (key.getModifiers().isShiftDown()) redoEdit(); else undoEdit();
        return true;
    }
    if (key.getModifiers().isCommandDown() && key.getKeyCode() == 'Y')
    {
        redoEdit();
        return true;
    }
    return false;
}
