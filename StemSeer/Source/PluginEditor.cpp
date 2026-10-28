#include "PluginEditor.h"

namespace
{
    const juce::Colour kBg      (0xff0a1020);
    const juce::Colour kPanel   (0xff0f1729);
    const juce::Colour kCard    (0xff0c1324);
    const juce::Colour kBorder  (0xff1e2a44);
    const juce::Colour kText    (0xffe6ecff);
    const juce::Colour kMuted   (0xff8b97b8);
    const juce::Colour kAccent  (0xff1a73ff);
    const juce::Colour kCyan    (0xff35c8ff);
    const juce::Colour kWaveBg  (0xff0a1120);

    void drawPanel (juce::Graphics& g, juce::Rectangle<int> r, juce::Colour fill = kPanel)
    {
        g.setColour (fill);
        g.fillRoundedRectangle (r.toFloat(), 8.0f);
        g.setColour (kBorder);
        g.drawRoundedRectangle (r.toFloat().reduced (0.5f), 8.0f, 1.0f);
    }

    void label (juce::Graphics& g, const juce::String& t, juce::Rectangle<int> r, float size, juce::Colour c,
                bool bold = false, juce::Justification j = juce::Justification::centredLeft)
    {
        g.setColour (c);
        g.setFont (juce::Font (size, bold ? juce::Font::bold : juce::Font::plain));
        g.drawText (t, r, j, true);
    }

    juce::String formatTime (double s)
    {
        const int t = (int) std::round (juce::jmax (0.0, s));
        return juce::String (t / 60) + ":" + juce::String (t % 60).paddedLeft ('0', 2);
    }
}

//==============================================================================
StemSeerEditor::StemSeerEditor (StemSeerProcessor& p)
    : juce::AudioProcessorEditor (&p), processor (p)
{
    setSize (1180, 720);
    setResizable (true, true);
    setResizeLimits (1060, 700, 1700, 1100);

    openButton.setButtonText (T8 ("Abrir archivo"));
    openButton.setColour (juce::TextButton::buttonColourId, kAccent);
    openButton.setColour (juce::TextButton::textColourOffId, juce::Colours::white);
    openButton.onClick = [this] { chooseFile(); };
    addAndMakeVisible (openButton);

    playButton.setButtonText ("Reproducir");
    playButton.setColour (juce::TextButton::buttonColourId, juce::Colour (0xff17233d));
    playButton.setColour (juce::TextButton::textColourOffId, kText);
    playButton.onClick = [this]
    {
        if (processor.isPlaying()) processor.pause(); else processor.play();
        updateControls();
    };
    addAndMakeVisible (playButton);

    tapButton.setButtonText ("Tap");
    tapButton.setColour (juce::TextButton::buttonColourId, juce::Colour (0xff17233d));
    tapButton.setColour (juce::TextButton::textColourOffId, kText);
    tapButton.onClick = [this] { tapTempo(); };
    addAndMakeVisible (tapButton);

    for (int g = 0; g < 3; ++g)
    {
        auto& sl = groupSliders[g];
        sl.setSliderStyle (juce::Slider::LinearHorizontal);
        sl.setTextBoxStyle (juce::Slider::NoTextBox, false, 0, 0);
        sl.setRange (0.0, 1.0, 0.01);
        sl.setValue (processor.groupGain[g].load(), juce::dontSendNotification);
        sl.setColour (juce::Slider::trackColourId, kAccent);
        sl.setColour (juce::Slider::backgroundColourId, juce::Colour (0xff1b2745));
        sl.setColour (juce::Slider::thumbColourId, juce::Colours::white);
        sl.onValueChange = [this, g] { processor.groupGain[g].store ((float) groupSliders[g].getValue()); repaint(); };
        addAndMakeVisible (sl);

        auto& b = soloButtons[g];
        b.setButtonText ("S");
        b.setClickingTogglesState (true);
        b.setToggleState (processor.groupSolo[g].load(), juce::dontSendNotification);
        b.setColour (juce::TextButton::buttonColourId, juce::Colour (0xff131d33));
        b.setColour (juce::TextButton::buttonOnColourId, kAccent);
        b.setColour (juce::TextButton::textColourOffId, kText);
        b.setColour (juce::TextButton::textColourOnId, juce::Colours::white);
        b.onClick = [this, g] { processor.groupSolo[g].store (soloButtons[g].getToggleState()); };
        addAndMakeVisible (b);
    }

    wavToggle.setButtonText ("WAV");
    flacToggle.setButtonText ("FLAC");
    mp3Toggle.setButtonText ("MP3");
    wavToggle.setToggleState (true, juce::dontSendNotification);
    mp3Toggle.setEnabled (false);
    mp3Toggle.setTooltip (T8 ("MP3 no está disponible en esta versión (JUCE no incluye codificador MP3)."));
    for (auto* t : { &wavToggle, &flacToggle, &mp3Toggle })
    {
        t->setColour (juce::ToggleButton::textColourId, kText);
        t->setColour (juce::ToggleButton::tickColourId, kAccent);
        t->setColour (juce::ToggleButton::tickDisabledColourId, kMuted);
        addAndMakeVisible (*t);
    }

    qualityBox.addItem ("16 bits", 1);
    qualityBox.addItem ("24 bits", 2);
    qualityBox.setSelectedId (2, juce::dontSendNotification);
    qualityBox.setColour (juce::ComboBox::backgroundColourId, juce::Colour (0xff131d33));
    qualityBox.setColour (juce::ComboBox::textColourId, kText);
    qualityBox.setColour (juce::ComboBox::outlineColourId, kBorder);
    addAndMakeVisible (qualityBox);

    exportButton.setButtonText ("Exportar stems");
    exportButton.setColour (juce::TextButton::buttonColourId, kAccent);
    exportButton.setColour (juce::TextButton::textColourOffId, juce::Colours::white);
    exportButton.onClick = [this] { chooseExportFolder(); };
    addAndMakeVisible (exportButton);

    progressBar.setColour (juce::ProgressBar::foregroundColourId, kAccent);
    progressBar.setColour (juce::ProgressBar::backgroundColourId, juce::Colour (0xff1b2745));
    addChildComponent (progressBar);

    processor.addChangeListener (this);
    updateControls();
    startTimerHz (20);
}

StemSeerEditor::~StemSeerEditor()
{
    stopTimer();
    processor.removeChangeListener (this);
}

//==============================================================================
void StemSeerEditor::changeListenerCallback (juce::ChangeBroadcaster*)
{
    if (processor.getState() == StemSeerProcessor::State::Ready)
    {
        peakMax = 1.0e-6f;
        for (auto& pk : processor.getResult().peaks)
            for (float v : pk) peakMax = juce::jmax (peakMax, v);
    }
    tapBpm = 0.0;
    taps.clear();
    updateControls();
    repaint();
}

void StemSeerEditor::timerCallback()
{
    using S = StemSeerProcessor::State;
    const bool busy = processor.getState() == S::Working || processor.isExporting();
    progressValue = (double) processor.getProgress();
    if (progressBar.isVisible() != busy) { progressBar.setVisible (busy); updateControls(); }

    if (processor.isPlaying() && processor.hasFinished())
    {
        processor.stopAndRewind();
        updateControls();
    }

    const auto L = computeLayout();
    if (processor.isPlaying()) repaint (L.stems);
    if (busy) repaint (L.leftCard);
}

void StemSeerEditor::updateControls()
{
    using S = StemSeerProcessor::State;
    const bool ready = processor.getState() == S::Ready;
    const bool working = processor.getState() == S::Working;
    playButton.setEnabled (ready);
    playButton.setButtonText (processor.isPlaying() ? "Pausa" : "Reproducir");
    exportButton.setEnabled (ready && ! processor.isExporting());
    openButton.setEnabled (! working);
}

void StemSeerEditor::chooseFile()
{
    chooser = std::make_unique<juce::FileChooser> (T8 ("Selecciona una canción"), juce::File(), processor.wildcardForAudioFiles());
    juce::Component::SafePointer<StemSeerEditor> safe (this);
    chooser->launchAsync (juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles,
                          [safe] (const juce::FileChooser& fc)
                          {
                              const auto f = fc.getResult();
                              if (safe != nullptr && f.existsAsFile()) safe->processor.loadFile (f);
                          });
}

void StemSeerEditor::chooseExportFolder()
{
    chooser = std::make_unique<juce::FileChooser> (T8 ("Carpeta de exportación"),
                                                   juce::File::getSpecialLocation (juce::File::userDocumentsDirectory));
    juce::Component::SafePointer<StemSeerEditor> safe (this);
    chooser->launchAsync (juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectDirectories,
                          [safe] (const juce::FileChooser& fc)
                          {
                              const auto dir = fc.getResult();
                              if (safe == nullptr || ! dir.isDirectory()) return;
                              const int bits = safe->qualityBox.getSelectedId() == 1 ? 16 : 24;
                              safe->processor.exportStems (dir, safe->wavToggle.getToggleState(), safe->flacToggle.getToggleState(), bits);
                          });
}

void StemSeerEditor::tapTempo()
{
    const double now = juce::Time::getMillisecondCounterHiRes();
    if (! taps.isEmpty() && now - taps.getLast() > 2000.0) taps.clear();
    taps.add (now);
    while (taps.size() > 8) taps.remove (0);
    if (taps.size() >= 2)
        tapBpm = 60000.0 / ((taps.getLast() - taps[0]) / (double) (taps.size() - 1));
    repaint (computeLayout().header);
}

void StemSeerEditor::seekFromMouse (const juce::MouseEvent& e)
{
    if (processor.getState() != StemSeerProcessor::State::Ready) return;
    const auto L = computeLayout();
    const auto p = e.getPosition();
    const double dur = processor.getResult().durationSec;
    for (int s = 0; s < stemseer::kNumStems; ++s)
    {
        const auto w = L.wave[s];
        if (w.getHeight() > 0 && (draggingSeek ? (p.y >= L.groupCard[0].getY() && p.y <= L.groupCard[2].getBottom()) : w.contains (p)))
        {
            const double frac = juce::jlimit (0.0, 1.0, (double) (p.x - w.getX()) / (double) juce::jmax (1, w.getWidth()));
            processor.setPositionSec (frac * dur);
            draggingSeek = true;
            repaint (L.stems);
            return;
        }
    }
}

void StemSeerEditor::mouseDown (const juce::MouseEvent& e)
{
    if (processor.getState() != StemSeerProcessor::State::Ready) return;
    const auto L = computeLayout();
    for (int s = 0; s < stemseer::kNumStems; ++s)
    {
        if (L.row[s].withWidth (116).contains (e.getPosition()))
        {
            processor.stemMute[s].store (! processor.stemMute[s].load());
            repaint (L.stems);
            return;
        }
    }
    seekFromMouse (e);
}

void StemSeerEditor::mouseDrag (const juce::MouseEvent& e)
{
    if (draggingSeek) seekFromMouse (e);
}

//==============================================================================
StemSeerEditor::Layout StemSeerEditor::computeLayout() const
{
    Layout L;
    auto area = getLocalBounds().reduced (8);
    L.header = area.removeFromTop (70);          area.removeFromTop (8);
    L.bottom = area.removeFromBottom (78);       area.removeFromBottom (8);
    L.left   = area.removeFromLeft (220);        area.removeFromLeft (8);
    L.right  = area.removeFromRight (310);       area.removeFromRight (8);
    L.stems  = area;

    auto r = L.right;
    L.analysis = r.removeFromTop ((int) ((float) r.getHeight() * 0.50f));
    r.removeFromTop (8);
    L.viz = r;

    // Cabecera
    L.bpmBlock   = juce::Rectangle<int> (L.header.getX() + 300, L.header.getY(), 190, L.header.getHeight());
    L.scaleBlock = juce::Rectangle<int> (L.header.getX() + 510, L.header.getY(), 230, L.header.getHeight());
    L.noteStrip  = juce::Rectangle<int> (L.header.getX() + 750, L.header.getY() + 14, juce::jmax (120, L.header.getWidth() - 750 - 90), L.header.getHeight() - 28);

    // Panel izquierdo
    auto lc = L.left;
    lc.removeFromTop (88);
    L.leftCard = lc.removeFromTop (170);
    lc.removeFromTop (8);
    L.leftBrand = lc.removeFromBottom (92);
    lc.removeFromBottom (8);
    L.leftFeatures = lc;

    // Stems
    auto inner = L.stems.reduced (10);
    inner.removeFromTop (30);
    const int groupSizes[3] = { 4, 5, 2 };
    const int headerH = 44, gap = 8, pad = 6;
    int rowH = (inner.getHeight() - 3 * headerH - 2 * gap - 3 * pad) / stemseer::kNumStems;
    rowH = juce::jlimit (18, 40, rowH);

    int idx = 0;
    for (int g = 0; g < 3; ++g)
    {
        auto card = inner.removeFromTop (headerH + groupSizes[g] * rowH + pad);
        inner.removeFromTop (gap);
        L.groupCard[g] = card;
        auto c = card;
        L.groupHeader[g] = c.removeFromTop (headerH);
        for (int i = 0; i < groupSizes[g]; ++i, ++idx)
        {
            auto row = c.removeFromTop (rowH);
            L.row[idx]  = row;
            L.dot[idx]  = juce::Rectangle<int> (row.getX() + 12, row.getCentreY() - 6, 12, 12);
            L.wave[idx] = juce::Rectangle<int> (row.getX() + 120, row.getY() + 2, juce::jmax (10, row.getWidth() - 130), juce::jmax (4, row.getHeight() - 4));
        }
    }
    return L;
}

void StemSeerEditor::resized()
{
    const auto L = computeLayout();
    openButton.setBounds (L.left.getX(), L.left.getY(), L.left.getWidth(), 40);
    playButton.setBounds (L.left.getX(), L.left.getY() + 46, L.left.getWidth(), 34);
    progressBar.setBounds (L.leftCard.getX() + 12, L.leftCard.getBottom() - 24, L.leftCard.getWidth() - 24, 14);
    tapButton.setBounds (L.bpmBlock.getX() + 112, L.bpmBlock.getY() + 30, 52, 28);

    for (int g = 0; g < 3; ++g)
    {
        const auto gh = L.groupHeader[g];
        groupSliders[g].setBounds (gh.getRight() - 236, gh.getCentreY() - 10, 130, 20);
        soloButtons[g].setBounds (gh.getRight() - 48, gh.getCentreY() - 13, 32, 26);
    }

    const auto b = L.bottom;
    wavToggle.setBounds  (b.getX() + 330, b.getY() + 38, 66, 26);
    flacToggle.setBounds (b.getX() + 400, b.getY() + 38, 70, 26);
    mp3Toggle.setBounds  (b.getX() + 474, b.getY() + 38, 66, 26);
    qualityBox.setBounds (b.getX() + 580, b.getY() + 36, 150, 30);
    exportButton.setBounds (b.getRight() - 214, b.getY() + 14, 200, b.getHeight() - 28);
}

//==============================================================================
void StemSeerEditor::paint (juce::Graphics& g)
{
    g.fillAll (kBg);
    const auto L = computeLayout();
    drawHeader (g, L);
    drawLeft (g, L);
    drawStems (g, L);
    drawAnalysis (g, L);
    drawViz (g, L);
    drawBottom (g, L);
}

void StemSeerEditor::drawHeader (juce::Graphics& g, const Layout& L)
{
    drawPanel (g, L.header);
    const bool ready = processor.getState() == StemSeerProcessor::State::Ready;
    const auto& res = processor.getResult();

    // Logo (barras)
    static const float bars[] = { 0.35f, 0.7f, 1.0f, 0.55f, 0.85f, 0.4f, 0.65f };
    for (int i = 0; i < 7; ++i)
    {
        const float h = 44.0f * bars[i];
        g.setColour (kCyan.interpolatedWith (kAccent, (float) i / 6.0f));
        g.fillRoundedRectangle ((float) (L.header.getX() + 16 + i * 7), (float) L.header.getCentreY() - h * 0.5f, 3.5f, h, 1.5f);
    }
    g.setFont (juce::Font (30.0f, juce::Font::bold));
    g.setColour (kText);
    g.drawText ("Stem", L.header.getX() + 74, L.header.getY() + 6, 90, 36, juce::Justification::centredLeft);
    g.setColour (kCyan);
    g.drawText ("Seer", L.header.getX() + 74 + 86, L.header.getY() + 6, 90, 36, juce::Justification::centredLeft);
    label (g, T8 ("SEPARADOR DE STEMS Y ANÁLISIS MUSICAL"), { L.header.getX() + 76, L.header.getY() + 42, 230, 16 }, 9.5f, kMuted);

    // BPM
    g.setColour (kBorder);
    g.drawVerticalLine (L.bpmBlock.getX() - 10, (float) L.header.getY() + 10, (float) L.header.getBottom() - 10);
    label (g, "BPM", { L.bpmBlock.getX(), L.bpmBlock.getY() + 8, 100, 18 }, 13.0f, kMuted);
    const double bpmShown = tapBpm > 0.0 ? tapBpm : res.bpm;
    label (g, (ready || tapBpm > 0.0) && bpmShown > 0.0 ? juce::String (bpmShown, 1) : juce::String ("--"),
           { L.bpmBlock.getX(), L.bpmBlock.getY() + 26, 110, 36 }, 30.0f, kText, true);

    // Escala
    g.setColour (kBorder);
    g.drawVerticalLine (L.scaleBlock.getX() - 10, (float) L.header.getY() + 10, (float) L.header.getBottom() - 10);
    label (g, "Escala detectada", { L.scaleBlock.getX(), L.scaleBlock.getY() + 8, 200, 18 }, 13.0f, kMuted);
    if (ready && res.key.valid)
    {
        label (g, T8 (stemseer::keyTitle (res.key).c_str()), { L.scaleBlock.getX(), L.scaleBlock.getY() + 24, 220, 26 }, 21.0f, kText, true);
        label (g, "(" + T8 (stemseer::modeName (res.key).c_str()) + ")", { L.scaleBlock.getX(), L.scaleBlock.getY() + 48, 220, 16 }, 12.5f, kMuted);
    }
    else
        label (g, "--", { L.scaleBlock.getX(), L.scaleBlock.getY() + 24, 220, 26 }, 21.0f, kText, true);

    // Tira de notas
    const bool fl = ready && res.key.valid && stemseer::usesFlats (res.key);
    const float cw = (float) L.noteStrip.getWidth() / 12.0f;
    for (int i = 0; i < 12; ++i)
    {
        const bool tonic = ready && res.key.valid && res.key.tonic == i;
        const bool inKey = ready && res.key.valid && stemseer::inScale (res.key, i);
        auto cell = juce::Rectangle<float> (L.noteStrip.getX() + cw * (float) i, (float) L.noteStrip.getY(), cw, 22.0f);
        label (g, T8 (stemseer::noteName (i, fl).c_str()), cell.toNearestInt(), 12.0f,
               tonic ? kCyan : (inKey ? kText : kMuted), tonic, juce::Justification::centred);
        if (tonic)
        {
            g.setColour (kAccent);
            g.fillRoundedRectangle (cell.getX() + 2.0f, cell.getBottom() + 4.0f, cell.getWidth() - 4.0f, 7.0f, 3.5f);
        }
    }
    label (g, "VST3", { L.header.getRight() - 80, L.header.getY(), 70, L.header.getHeight() }, 16.0f, kMuted, false, juce::Justification::centredRight);
}

void StemSeerEditor::drawLeft (juce::Graphics& g, const Layout& L)
{
    using S = StemSeerProcessor::State;
    drawPanel (g, L.leftCard, kPanel);
    const auto st = processor.getState();
    const auto& res = processor.getResult();
    auto c = L.leftCard.reduced (14, 12);

    if (st == S::Ready)
    {
        label (g, T8 ("Análisis completado"), c.removeFromTop (22), 14.0f, kText, true);
        c.removeFromTop (4);
        label (g, "Tiempo: " + formatTime (res.durationSec) + "   |   BPM: " + juce::String (res.bpm, 1), c.removeFromTop (18), 12.0f, kMuted);
        label (g, "Escala: " + T8 (stemseer::keyTitle (res.key).c_str()) + " (" + T8 (stemseer::modeName (res.key).c_str()) + ")", c.removeFromTop (18), 12.0f, kMuted);
        c.removeFromTop (6);
        label (g, res.songName, c.removeFromTop (18), 12.0f, kCyan);
    }
    else
    {
        label (g, st == S::Working ? "Procesando..." : (st == S::Failed ? "Error" : "Listo para empezar"), c.removeFromTop (22), 14.0f,
               st == S::Failed ? juce::Colour (0xffff6b6b) : kText, true);
        c.removeFromTop (4);
        g.setColour (kMuted);
        g.setFont (12.0f);
        g.drawFittedText (processor.getStatusText(), c.removeFromTop (60), juce::Justification::topLeft, 4);
    }
    if (processor.isExporting() || (st == S::Ready && processor.getStatusText().startsWith (T8 ("Exportación"))))
    {
        g.setColour (kMuted);
        g.setFont (11.0f);
        g.drawFittedText (processor.getStatusText(), L.leftCard.reduced (14, 0).withTrimmedTop (L.leftCard.getHeight() - 62).withHeight (36), juce::Justification::topLeft, 3);
    }

    // Funciones
    auto f = L.leftFeatures.reduced (4, 4);
    const char* titles[3]  = { "Separación de stems", "Detección de escala", "Detección de BPM" };
    const char* details[3] = { "Análisis espectral (HPSS)", "Análisis armónico (cromagrama)", "Onset & tempo tracking" };
    for (int i = 0; i < 3; ++i)
    {
        auto row = f.removeFromTop (46);
        g.setColour (juce::Colour (0xff17233d));
        g.fillEllipse ((float) row.getX() + 2.0f, (float) row.getCentreY() - 15.0f, 30.0f, 30.0f);
        g.setColour (kCyan);
        g.fillEllipse ((float) row.getX() + 12.0f, (float) row.getCentreY() - 5.0f, 10.0f, 10.0f);
        label (g, T8 (titles[i]), { row.getX() + 42, row.getY() + 5, row.getWidth() - 42, 18 }, 13.0f, kText);
        label (g, T8 (details[i]), { row.getX() + 42, row.getY() + 23, row.getWidth() - 42, 16 }, 11.0f, kMuted);
    }

    // Marca
    drawPanel (g, L.leftBrand, kPanel);
    auto b = L.leftBrand.reduced (12, 10);
    label (g, T8 ("Inspirado en Moisés"), b.removeFromTop (20), 13.0f, kCyan, true);
    g.setColour (kMuted);
    g.setFont (11.0f);
    g.drawFittedText (T8 ("Separación de stems, detección de tonalidad y tempo. Más control, más creatividad."), b, juce::Justification::topLeft, 4);
}

void StemSeerEditor::drawStems (juce::Graphics& g, const Layout& L)
{
    drawPanel (g, L.stems);
    label (g, T8 ("STEMS EXTRAÍDOS"), { L.stems.getX() + 14, L.stems.getY() + 8, 300, 22 }, 14.0f, kText, true);

    const bool ready = processor.getState() == StemSeerProcessor::State::Ready;
    const auto& res = processor.getResult();

    const char* gNames[3] = { "DRUMS", "MELODÍAS", "EXTRAS" };
    const char* gSubs[3]  = { "Kick / Bass / Hi-hat / Percusión", "Guitarras / Pianos / Strings / Flautas / Pads", "Voces / FX / Otros" };
    const char* gLetters[3] = { "D", "M", "E" };
    const juce::uint32 gColours[3] = { 0xffff3b57, 0xff14b8a6, 0xff3b9cff };

    for (int gi = 0; gi < 3; ++gi)
    {
        drawPanel (g, L.groupCard[gi], kCard);
        const auto gh = L.groupHeader[gi];
        const juce::Colour gc (gColours[gi]);
        auto icon = juce::Rectangle<float> ((float) gh.getX() + 12.0f, (float) gh.getCentreY() - 18.0f, 36.0f, 36.0f);
        g.setColour (gc.withAlpha (0.15f));
        g.fillRoundedRectangle (icon, 8.0f);
        g.setColour (gc);
        g.drawRoundedRectangle (icon, 8.0f, 1.5f);
        label (g, gLetters[gi], icon.toNearestInt(), 16.0f, gc, true, juce::Justification::centred);

        label (g, T8 (gNames[gi]), { gh.getX() + 58, gh.getY() + 6, gh.getWidth() - 310, 20 }, 15.0f, kText, true);
        label (g, T8 (gSubs[gi]), { gh.getX() + 58, gh.getY() + 25, gh.getWidth() - 310, 16 }, 11.0f, kMuted);
        label (g, juce::String ((int) std::round (groupSliders[gi].getValue() * 100.0)) + "%",
               { gh.getRight() - 100, gh.getCentreY() - 10, 48, 20 }, 12.5f, kText, false, juce::Justification::centredRight);
    }

    for (int s = 0; s < stemseer::kNumStems; ++s)
    {
        const auto& d = kStemDefs[s];
        const juce::Colour col (d.colour);
        const bool muted = processor.stemMute[s].load();

        g.setColour (muted ? col.withAlpha (0.25f) : col);
        g.fillEllipse (L.dot[s].toFloat());
        label (g, T8 (d.name), { L.dot[s].getRight() + 10, L.row[s].getY(), 90, L.row[s].getHeight() }, 13.0f, muted ? kMuted : kText);

        const auto w = L.wave[s];
        g.setColour (kWaveBg);
        g.fillRect (w);

        const float mid = (float) w.getCentreY();
        if (ready && ! res.peaks[(size_t) s].empty())
        {
            const auto& pk = res.peaks[(size_t) s];
            const int n = (int) pk.size(), W = w.getWidth();
            const float hmax = (float) w.getHeight() * 0.5f - 1.0f;
            g.setColour (col.withAlpha (muted ? 0.25f : 0.9f));
            for (int x = 0; x < W; ++x)
            {
                const int i0 = x * n / W, i1 = juce::jmax (i0 + 1, (x + 1) * n / W);
                float m = 0.0f;
                for (int i = i0; i < juce::jmin (i1, n); ++i) m = juce::jmax (m, pk[(size_t) i]);
                const float a = std::pow (juce::jlimit (0.0f, 1.0f, m / peakMax), 0.6f);
                const float h = juce::jmax (0.5f, a * hmax);
                g.drawVerticalLine (w.getX() + x, mid - h, mid + h);
            }
        }
        else
        {
            g.setColour (col.withAlpha (0.18f));
            g.drawHorizontalLine ((int) mid, (float) w.getX(), (float) w.getRight());
        }
    }

    if (ready && res.durationSec > 0.0)
    {
        const double frac = juce::jlimit (0.0, 1.0, processor.getPositionSec() / res.durationSec);
        const int x = L.wave[0].getX() + (int) (frac * L.wave[0].getWidth());
        g.setColour (juce::Colours::white.withAlpha (0.85f));
        g.drawVerticalLine (x, (float) L.groupCard[0].getY(), (float) L.groupCard[2].getBottom());
    }
}

void StemSeerEditor::drawAnalysis (juce::Graphics& g, const Layout& L)
{
    drawPanel (g, L.analysis);
    const bool ready = processor.getState() == StemSeerProcessor::State::Ready;
    const auto& res = processor.getResult();
    const bool hasKey = ready && res.key.valid;

    auto a = L.analysis.reduced (14, 10);
    label (g, T8 ("ANÁLISIS MUSICAL"), a.removeFromTop (22), 14.0f, kText, true);
    a.removeFromTop (6);

    auto top = a.removeFromTop (56);
    drawPanel (g, top, kCard);
    auto t1 = top.reduced (12, 6);
    auto t2 = t1.removeFromRight (t1.getWidth() / 3);
    label (g, "Escala", t1.removeFromTop (16), 11.5f, kMuted);
    label (g, hasKey ? T8 (stemseer::keyTitle (res.key).c_str()) + " (" + T8 (stemseer::modeName (res.key).c_str()) + ")" : juce::String ("--"), t1, 15.0f, kText, true);
    label (g, "BPM", t2.removeFromTop (16), 11.5f, kMuted);
    label (g, ready && res.bpm > 0 ? juce::String (res.bpm, 1) : juce::String ("--"), t2, 15.0f, kText, true);

    a.removeFromTop (10);

    // Rueda de notas
    auto wheelArea = a.removeFromLeft (150);
    const float R = (float) juce::jmin (wheelArea.getWidth() - 44, wheelArea.getHeight() - 36) * 0.5f;
    const float cx = (float) wheelArea.getCentreX(), cy = (float) wheelArea.getCentreY();
    const bool fl = hasKey && stemseer::usesFlats (res.key);
    for (int i = 0; i < 12; ++i)
    {
        const float a0 = juce::degreesToRadians ((float) i * 30.0f - 14.0f);
        const float a1 = juce::degreesToRadians ((float) i * 30.0f + 14.0f);
        juce::Path p;
        p.addPieSegment (cx - R, cy - R, R * 2.0f, R * 2.0f, a0, a1, 0.55f);
        const bool tonic = hasKey && res.key.tonic == i;
        const bool inKey = hasKey && stemseer::inScale (res.key, i);
        g.setColour (tonic ? kAccent : (inKey ? juce::Colour (0xff1f3358) : juce::Colour (0xff131d33)));
        g.fillPath (p);

        const float ang = juce::degreesToRadians ((float) i * 30.0f);
        const juce::Point<float> lp (cx + std::sin (ang) * (R + 13.0f), cy - std::cos (ang) * (R + 13.0f));
        label (g, T8 (stemseer::noteName (i, fl).c_str()),
               juce::Rectangle<float> (lp.x - 12.0f, lp.y - 9.0f, 24.0f, 18.0f).toNearestInt(), 11.5f,
               tonic ? kCyan : kMuted, tonic, juce::Justification::centred);
    }

    // Modo
    a.removeFromLeft (8);
    drawPanel (g, a, kCard);
    auto m = a.reduced (10, 8);
    label (g, "Modo", m.removeFromTop (16), 11.5f, kMuted);
    if (hasKey)
    {
        label (g, T8 (stemseer::modeName (res.key).c_str()), m.removeFromTop (22), 16.0f, kText, true);
        g.setColour (kMuted);
        g.setFont (11.5f);
        g.drawFittedText (T8 (stemseer::modeSubtitle (res.key).c_str()), m.removeFromTop (34), juce::Justification::topLeft, 2);
        g.setColour (kCyan.withAlpha (0.9f));
        g.setFont (12.0f);
        g.drawFittedText (T8 (stemseer::scaleNotes (res.key).c_str()), m, juce::Justification::topLeft, 3);
    }
    else
        label (g, "--", m.removeFromTop (22), 16.0f, kText, true);
}

void StemSeerEditor::drawViz (juce::Graphics& g, const Layout& L)
{
    drawPanel (g, L.viz);
    const bool ready = processor.getState() == StemSeerProcessor::State::Ready;
    const auto& res = processor.getResult();

    auto a = L.viz.reduced (14, 10);
    label (g, T8 ("VISUALIZACIÓN DE STEMS"), a.removeFromTop (22), 14.0f, kText, true);
    a.removeFromTop (6);
    auto axis = a.removeFromBottom (18);
    drawPanel (g, a, kCard);
    auto inner = a.reduced (6, 6);
    auto names = inner.removeFromLeft (70);
    const int rowH = juce::jmax (1, inner.getHeight() / stemseer::kNumStems);

    for (int s = 0; s < stemseer::kNumStems; ++s)
    {
        const juce::Colour col (kStemDefs[s].colour);
        const int y = inner.getY() + s * rowH;
        label (g, T8 (kStemDefs[s].name), { names.getX(), y, names.getWidth(), rowH }, 10.5f, kText);
        if (! ready || res.peaks[(size_t) s].empty()) continue;

        const auto& pk = res.peaks[(size_t) s];
        const int n = (int) pk.size(), W = inner.getWidth();
        for (int x = 0; x < W; x += 2)
        {
            const int i0 = x * n / W, i1 = juce::jmax (i0 + 1, (x + 2) * n / W);
            float mx = 0.0f;
            for (int i = i0; i < juce::jmin (i1, n); ++i) mx = juce::jmax (mx, pk[(size_t) i]);
            const float amp = std::pow (juce::jlimit (0.0f, 1.0f, mx / peakMax), 0.5f);
            g.setColour (col.withAlpha (0.08f + 0.92f * amp));
            g.fillRect (inner.getX() + x, y + 1, 2, juce::jmax (1, rowH - 2));
        }
    }

    if (ready)
        for (int i = 0; i < 5; ++i)
        {
            const double t = res.durationSec * i / 4.0;
            label (g, formatTime (t), { inner.getX() + (inner.getWidth() * i) / 5, axis.getY(), inner.getWidth() / 5, axis.getHeight() }, 10.5f, kMuted);
        }
}

void StemSeerEditor::drawBottom (juce::Graphics& g, const Layout& L)
{
    drawPanel (g, L.bottom);
    const auto b = L.bottom;
    label (g, "SALIDA / MIX", { b.getX() + 14, b.getY() + 10, 200, 16 }, 10.5f, kMuted, true);
    drawPanel (g, { b.getX() + 14, b.getY() + 30, 280, 28 }, kCard);
    label (g, "Stems individuales", { b.getX() + 24, b.getY() + 30, 260, 28 }, 13.0f, kText);
    label (g, "Cada stem en una pista separada", { b.getX() + 14, b.getY() + 58, 280, 16 }, 10.5f, kMuted);

    g.setColour (kBorder);
    g.drawVerticalLine (b.getX() + 312, (float) b.getY() + 10, (float) b.getBottom() - 10);
    label (g, T8 ("FORMATOS DE EXPORTACIÓN"), { b.getX() + 330, b.getY() + 10, 240, 16 }, 10.5f, kMuted, true);
    label (g, "Calidad", { b.getX() + 580, b.getY() + 10, 120, 16 }, 10.5f, kMuted, true);
}
