#pragma once
#include "PluginProcessor.h"

class StemSeerEditor : public juce::AudioProcessorEditor,
                       private juce::ChangeListener,
                       private juce::Timer
{
public:
    explicit StemSeerEditor (StemSeerProcessor&);
    ~StemSeerEditor() override;

    void paint (juce::Graphics&) override;
    void resized() override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseDrag (const juce::MouseEvent&) override;
    void mouseUp (const juce::MouseEvent&) override { draggingSeek = false; }

private:
    struct Layout
    {
        juce::Rectangle<int> header, left, stems, right, analysis, viz, bottom;
        juce::Rectangle<int> leftCard, leftFeatures, leftBrand;
        juce::Rectangle<int> bpmBlock, scaleBlock, noteStrip;
        juce::Rectangle<int> groupCard[3], groupHeader[3];
        juce::Rectangle<int> row[stemseer::kNumStems], dot[stemseer::kNumStems], wave[stemseer::kNumStems];
    };

    Layout computeLayout() const;
    void drawHeader (juce::Graphics&, const Layout&);
    void drawLeft (juce::Graphics&, const Layout&);
    void drawStems (juce::Graphics&, const Layout&);
    void drawAnalysis (juce::Graphics&, const Layout&);
    void drawViz (juce::Graphics&, const Layout&);
    void drawBottom (juce::Graphics&, const Layout&);

    void changeListenerCallback (juce::ChangeBroadcaster*) override;
    void timerCallback() override;
    void updateControls();
    void seekFromMouse (const juce::MouseEvent&);
    void chooseFile();
    void chooseExportFolder();
    void tapTempo();

    StemSeerProcessor& processor;

    juce::TextButton openButton, playButton, tapButton, exportButton;
    juce::TextButton soloButtons[3];
    juce::Slider groupSliders[3];
    juce::ToggleButton wavToggle, flacToggle, mp3Toggle;
    juce::ComboBox qualityBox;
    double progressValue = 0.0;
    juce::ProgressBar progressBar { progressValue };

    std::unique_ptr<juce::FileChooser> chooser;
    juce::Array<double> taps;
    double tapBpm = 0.0;
    float peakMax = 1.0f;
    bool draggingSeek = false;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (StemSeerEditor)
};
