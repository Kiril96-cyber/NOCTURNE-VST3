#include "PluginEditor.h"
#include <utility>

static std::pair<int, int> visibleMidiRange(const juce::Array<NocturneAudioProcessor::NoteEvent>& notes)
{
    int top = 84, bottom = 60;
    if (!notes.isEmpty())
    {
        top = 0; bottom = 127;
        for (const auto& note : notes) { top = juce::jmax(top, note.midi); bottom = juce::jmin(bottom, note.midi); }
        top = juce::jmin(127, top + 2); bottom = juce::jmax(0, bottom - 2);
        while (top - bottom + 1 < 24)
        {
            if (top < 127) ++top;
            if (top - bottom + 1 >= 24) break;
            if (bottom > 0) --bottom;
            if (top == 127 && bottom == 0) break;
        }
    }
    return { top, bottom };
}

static const juce::Colour bg(0xff101016), panel(0xff191923), line(0xff343044), violet(0xff9c83c7), text(0xffe8e3ef), muted(0xff938ba3), waveformColour(0xff6c5b88);

NocturneAudioProcessorEditor::NocturneAudioProcessorEditor(NocturneAudioProcessor& p) : AudioProcessorEditor(&p), processor(p)
{
    setSize(1040, 700);
    auto setupButton = [&](juce::TextButton& b) { addAndMakeVisible(b); b.setColour(juce::TextButton::buttonColourId, panel); b.setColour(juce::TextButton::textColourOffId, text); b.setColour(juce::TextButton::buttonOnColourId, violet); };
    setupButton(importButton); setupButton(recordButton); setupButton(exportButton); setupButton(copyButton); setupButton(clearButton);
    importButton.onClick = [this] { chooseAudio(); };
    recordButton.onClick = [this] { bool v = !processor.recording.load(); processor.recording.store(v); recordButton.setButtonText(v ? "■ STOP" : "● RECORD"); };
    exportButton.onClick = [this] { saveMidi(); }; copyButton.onClick = [this] { copyNotes(); }; clearButton.onClick = [this] { processor.clearNotes(); };

    auto setupSlider = [&](juce::Slider& s, juce::Label& l, const juce::String& name, double min, double max, double value) {
        addAndMakeVisible(s); addAndMakeVisible(l); l.setText(name, juce::dontSendNotification); l.setColour(juce::Label::textColourId, muted);
        s.setSliderStyle(juce::Slider::LinearHorizontal); s.setTextBoxStyle(juce::Slider::TextBoxRight, false, 54, 21);
        s.setRange(min, max, 0.1); s.setValue(value); s.setColour(juce::Slider::trackColourId, violet); s.setColour(juce::Slider::textBoxTextColourId, text);
    };
    setupSlider(speed, speedLabel, "READ SPEED", 10, 100, 45);
    setupSlider(accuracy, accuracyLabel, "NOTE ACCURACY", 0.5, 0.99, 0.82);
    setupSlider(glide, glideLabel, "PITCH GLIDE", 0, 100, 25);
    setupSlider(vibrato, vibratoLabel, "VIBRATO TOLERANCE", 0, 100, 35);
    setupSlider(tempo, tempoLabel, "PROJECT BPM", 40, 240, 120);
    speed.onValueChange = [this] { if (auto* p = processor.parameters.getParameter("speed")) p->setValueNotifyingHost(p->convertTo0to1((float)speed.getValue())); };
    accuracy.onValueChange = [this] { if (auto* p = processor.parameters.getParameter("accuracy")) p->setValueNotifyingHost(p->convertTo0to1((float)accuracy.getValue())); };
    glide.onValueChange = [this] { if (auto* p = processor.parameters.getParameter("glide")) p->setValueNotifyingHost(p->convertTo0to1((float)glide.getValue())); };
    vibrato.onValueChange = [this] { if (auto* p = processor.parameters.getParameter("vibrato")) p->setValueNotifyingHost(p->convertTo0to1((float)vibrato.getValue())); };
    tempo.onValueChange = [this] { processor.bpm.store((float)tempo.getValue()); };
    speed.setValue(processor.parameters.getRawParameterValue("speed")->load(), juce::dontSendNotification);
    accuracy.setValue(processor.parameters.getRawParameterValue("accuracy")->load(), juce::dontSendNotification);
    glide.setValue(processor.parameters.getRawParameterValue("glide")->load(), juce::dontSendNotification);
    vibrato.setValue(processor.parameters.getRawParameterValue("vibrato")->load(), juce::dontSendNotification);
    tempo.setValue(processor.bpm.load(), juce::dontSendNotification);

    addAndMakeVisible(quantize); addAndMakeVisible(quantizeLabel);
    quantizeLabel.setText("MIDI QUANTIZE", juce::dontSendNotification); quantizeLabel.setColour(juce::Label::textColourId, muted);
    quantize.addItem("OFF", 1); quantize.addItem("1/4", 2); quantize.addItem("1/8", 3); quantize.addItem("1/16", 4); quantize.addItem("1/32", 5);
    quantize.setSelectedId(1); quantize.onChange = [this] { const int id = quantize.getSelectedId(); const int divisions[] = { 0, 4, 8, 16, 32 }; processor.quantizeDivision.store(divisions[juce::jlimit(0, 4, id - 1)]); };
    const int savedDivision = processor.quantizeDivision.load();
    const int savedId = savedDivision == 0 ? 1 : savedDivision == 4 ? 2 : savedDivision == 8 ? 3 : savedDivision == 16 ? 4 : 5;
    quantize.setSelectedId(savedId, juce::dontSendNotification);

    addAndMakeVisible(pitchLabel); pitchLabel.setColour(juce::Label::textColourId, violet); pitchLabel.setFont(juce::Font(18.0f));
    addAndMakeVisible(statusLabel); statusLabel.setColour(juce::Label::textColourId, muted);
    startTimerHz(12);
}

void NocturneAudioProcessorEditor::loadWaveform(const juce::File& file)
{
    waveform.clear();
    juce::AudioFormatManager fm; fm.registerBasicFormats();
    std::unique_ptr<juce::AudioFormatReader> reader(fm.createReaderFor(file));
    if (!reader) return;
    constexpr int points = 1600;
    juce::AudioBuffer<float> block(1, 1);
    const int64 total = reader->lengthInSamples;
    waveform.reserve(points);
    for (int i = 0; i < points; ++i) {
        const int64 pos = (total * i) / points;
        reader->read(&block, 0, 1, pos, true, true);
        waveform.push_back(block.getMagnitude(0, 0, 1));
    }
    repaint();
}
void NocturneAudioProcessorEditor::chooseAudio()
{
    audioFileChooser = std::make_unique<juce::FileChooser>(
        "Import audio",
        juce::File{},
        "*.wav;*.aif;*.aiff;*.flac;*.mp3"
    );

    audioFileChooser->launchAsync(
        juce::FileBrowserComponent::openMode |
        juce::FileBrowserComponent::canSelectFiles,
        [this](const juce::FileChooser& chooser)
        {
            const auto file = chooser.getResult();

            if (file != juce::File{})
            {
                statusLabel.setText("Analysing: " + file.getFileName(), juce::dontSendNotification);
                loadWaveform(file);
                processor.analyseFile(file);
                statusLabel.setText(
                    "Analysis complete — drag notes to edit pitch and timing",
                    juce::dontSendNotification
                );
            }

            audioFileChooser.reset();
        }
    );
}
void NocturneAudioProcessorEditor::saveMidi()
{
    midiSaveChooser = std::make_unique<juce::FileChooser>(
        "Export MIDI",
        juce::File::getSpecialLocation(juce::File::userDocumentsDirectory).getChildFile("NOCTURNE.mid"),
        "*.mid"
    );

    midiSaveChooser->launchAsync(
        juce::FileBrowserComponent::saveMode |
        juce::FileBrowserComponent::canSelectFiles,
        [this](const juce::FileChooser& chooser)
        {
            const auto file = chooser.getResult();

            if (file != juce::File{})
            {
                processor.exportMidi(file.withFileExtension("mid"));
                statusLabel.setText(
                    "MIDI exported with current BPM / quantize",
                    juce::dontSendNotification
                );
            }

            midiSaveChooser.reset();
        }
    );
}
void NocturneAudioProcessorEditor::copyNotes() { juce::SystemClipboard::copyTextToClipboard(processor.notesAsText()); statusLabel.setText("Note names copied", juce::dontSendNotification); }
void NocturneAudioProcessorEditor::timerCallback()
{
    float hz = processor.currentPitchHz.load();
    pitchLabel.setText(hz > 0 ? juce::String(hz, 1) + " Hz  •  " + juce::MidiMessage::getMidiNoteName((int)std::round(69 + 12 * std::log2(hz / 440)), true, true, 3) : "—  WAITING FOR INPUT", juce::dontSendNotification);
    repaint();
}
juce::Rectangle<float> NocturneAudioProcessorEditor::pianoBounds() const { return { 28.0f, 298.0f, 720.0f, 300.0f }; }

void NocturneAudioProcessorEditor::paint(juce::Graphics& g)
{
    g.fillAll(bg);
    g.setColour(violet); g.setFont(juce::Font(27.0f, juce::Font::bold)); g.drawText("NOCTURNE", 28, 18, 260, 38, juce::Justification::left);
    g.setColour(muted); g.setFont(11.0f); g.drawText("POLYPHONIC FILE  •  MONOPHONIC LIVE  /  DARK EDITION", 30, 54, 380, 18, juce::Justification::left);
    g.setColour(line); g.drawLine(28, 82, (float)getWidth() - 28, 82, 1.0f);
    g.setColour(panel); g.fillRoundedRectangle(28, 98, 720, 58, 8);
    g.setColour(muted); g.setFont(11.0f); g.drawText("LIVE PITCH", 44, 106, 100, 16, juce::Justification::left);
    g.setColour(text); g.setFont(16.0f); g.drawText(pitchLabel.getText(), 44, 125, 330, 24, juce::Justification::left);

    // Imported audio waveform panel
    const juce::Rectangle<float> wave(28, 174, 720, 104);
    g.setColour(panel); g.fillRoundedRectangle(wave, 8);
    g.setColour(muted); g.setFont(10.5f); g.drawText("AUDIO WAVEFORM  /  IMPORTED SOURCE", 42, 181, 330, 15, juce::Justification::left);
    g.setColour(line); g.drawLine(wave.getX()+12, wave.getCentreY()+9, wave.getRight()-12, wave.getCentreY()+9, 1.0f);
    if (!waveform.empty()) {
        juce::Path path; const float left=wave.getX()+12, width=wave.getWidth()-24, mid=wave.getCentreY()+10, amp=32;
        for (size_t i=0; i<waveform.size(); ++i) { float x=left+width*(float)i/(float)(waveform.size()-1); float y=mid-waveform[i]*amp; if(i==0) path.startNewSubPath(x,y); else path.lineTo(x,y); }
        for (int i=(int)waveform.size()-1; i>=0; --i) { float x=left+width*(float)i/(float)(waveform.size()-1); float y=mid+waveform[(size_t)i]*amp; path.lineTo(x,y); }
        path.closeSubPath(); g.setColour(waveformColour.withAlpha(0.85f)); g.fillPath(path);
    } else {
        g.setColour(muted); g.setFont(12.0f); g.drawText("Import an audio file to display its waveform", wave.toNearestInt(), juce::Justification::centred);
    }

    // Editable piano roll
    auto roll=pianoBounds(); g.setColour(panel); g.fillRoundedRectangle(roll, 8);
    g.setColour(muted); g.setFont(10.5f); g.drawText("PIANO ROLL  /  DRAG NOTES TO CHANGE PITCH & POSITION", 42, 286, 520, 15, juce::Justification::left);
    auto notes=processor.getNotesCopy();
    const auto pitchRange = visibleMidiRange(notes);
    const int topMidi = pitchRange.first, bottomMidi = pitchRange.second;
    const int rows = topMidi - bottomMidi + 1;
    const float rowH = roll.getHeight() / (float)rows;
    for (int r = 0; r <= rows; ++r) { float y=roll.getY()+r*rowH; g.setColour((r%12==0)?line.withAlpha(0.9f):line.withAlpha(0.35f)); g.drawHorizontalLine((int)y,roll.getX(),roll.getRight()); }
    for(int c=1;c<8;++c) { float x=roll.getX()+roll.getWidth()*c/8.0f; g.setColour(line.withAlpha(0.5f)); g.drawVerticalLine((int)x,roll.getY(),roll.getBottom()); }
    double maxTime=8.0; for(const auto& n:notes) maxTime=juce::jmax(maxTime,n.start+n.length);
    for(int i=0;i<notes.size();++i) {
        const auto& n=notes.getReference(i);
        float nx=roll.getX()+8+(float)(n.start/maxTime)*(roll.getWidth()-16);
        float nw=juce::jmax(8.0f,(float)(n.length/maxTime)*(roll.getWidth()-16));
        float ny=roll.getY()+((float)(topMidi-n.midi)+0.5f)*rowH;
        if(ny<roll.getY() || ny+rowH>roll.getBottom()) continue;
        g.setColour(violet.withAlpha(0.88f)); g.fillRoundedRectangle(nx,ny-rowH*0.34f,nw,rowH*0.68f,3.0f);
        g.setColour(text); g.setFont(9.0f); if(nw>28) g.drawText(juce::MidiMessage::getMidiNoteName(n.midi,true,true,3), (int)nx+3,(int)(ny-rowH*0.34f),(int)nw-5,(int)(rowH*0.68f),juce::Justification::centredLeft);
    }
    g.setColour(muted); g.setFont(10.0f); g.drawText("NOTES  " + juce::String(notes.size()) + "     •     BPM " + juce::String(processor.bpm.load(),1), 42, 604, 260, 15, juce::Justification::left);
    g.setColour(line); g.drawLine(28, 628, (float)getWidth()-28, 628, 1.0f);
}
void NocturneAudioProcessorEditor::resized()
{
    importButton.setBounds(28, 642, 140, 36); recordButton.setBounds(177, 642, 120, 36); exportButton.setBounds(306, 642, 130, 36); copyButton.setBounds(445, 642, 120, 36); clearButton.setBounds(574, 642, 85, 36);
    speedLabel.setBounds(780, 102, 220, 18); speed.setBounds(780, 121, 220, 26);
    accuracyLabel.setBounds(780, 158, 220, 18); accuracy.setBounds(780, 177, 220, 26);
    glideLabel.setBounds(780, 214, 220, 18); glide.setBounds(780, 233, 220, 26);
    vibratoLabel.setBounds(780, 270, 220, 18); vibrato.setBounds(780, 289, 220, 26);
    tempoLabel.setBounds(780, 330, 220, 18); tempo.setBounds(780, 349, 220, 26);
    quantizeLabel.setBounds(780, 388, 220, 18); quantize.setBounds(780, 408, 220, 28);
    pitchLabel.setBounds(390, 112, 340, 36); statusLabel.setBounds(675, 648, 340, 24);
}
void NocturneAudioProcessorEditor::mouseDown(const juce::MouseEvent& e)
{
    auto roll=pianoBounds(); if(!roll.contains(e.position)) return;
    auto notes=processor.getNotesCopy();
    const auto pitchRange = visibleMidiRange(notes);
    const int topMidi = pitchRange.first, bottomMidi = pitchRange.second;
    const float rowH = roll.getHeight() / (float)(topMidi - bottomMidi + 1);
    double maxTime=8.0; for(const auto& n:notes) maxTime=juce::jmax(maxTime,n.start+n.length);
    for(int i=notes.size()-1;i>=0;--i) {
        const auto& n=notes.getReference(i); float x=roll.getX()+8+(float)(n.start/maxTime)*(roll.getWidth()-16); float w=juce::jmax(8.0f,(float)(n.length/maxTime)*(roll.getWidth()-16));
        float y=roll.getY()+((float)(topMidi-n.midi)+0.5f)*rowH;
        if(e.position.x>=x && e.position.x<=x+w && std::abs(e.position.y-y)<rowH*0.7f) { draggedNote=i; dragOffsetSeconds=(e.position.x-x)/(roll.getWidth()-16)*maxTime; return; }
    }
}
void NocturneAudioProcessorEditor::mouseDrag(const juce::MouseEvent& e)
{
    if(draggedNote<0) return;
    auto roll=pianoBounds(); auto notes=processor.getNotesCopy(); if(!juce::isPositiveAndBelow(draggedNote,notes.size())) return;
    double maxTime=8.0; for(const auto& n:notes) maxTime=juce::jmax(maxTime,n.start+n.length);
    const auto pitchRange = visibleMidiRange(notes);
    const int topMidi = pitchRange.first, bottomMidi = pitchRange.second;
    const float rowH = roll.getHeight() / (float)(topMidi - bottomMidi + 1);
    int midi=juce::jlimit(bottomMidi, topMidi, topMidi-(int)std::floor((e.position.y-roll.getY())/rowH));
    double start=((e.position.x-roll.getX()-8)/(roll.getWidth()-16))*maxTime-dragOffsetSeconds;
    processor.moveNote(draggedNote,midi,start); repaint();
}
