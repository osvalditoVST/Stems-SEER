#pragma once
#include <juce_audio_utils/juce_audio_utils.h>
#include <array>
#include <atomic>
#include <functional>
#include <memory>
#include <vector>
#include "Core/Analysis.h"
#include "Core/Separator.h"
#include "StemDefs.h"

//==============================================================================
class StemSeerProcessor : public juce::AudioProcessor,
                          public juce::ChangeBroadcaster
{
public:
    enum class State { Idle = 0, Working, Ready, Failed };

    struct Result
    {
        double bpm = 0.0;
        stemseer::KeyResult key;
        double sampleRate = 0.0, durationSec = 0.0;
        juce::String songName;
        juce::File cacheDir;
        std::array<std::vector<float>, stemseer::kNumStems> peaks;
    };

    StemSeerProcessor();
    ~StemSeerProcessor() override;

    // --- AudioProcessor
    void prepareToPlay (double sampleRate, int samplesPerBlock) override;
    void releaseResources() override;
    bool isBusesLayoutSupported (const BusesLayout& layouts) const override;
    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;
    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }
    const juce::String getName() const override { return "StemSeer"; }
    bool acceptsMidi() const override { return false; }
    bool producesMidi() const override { return false; }
    double getTailLengthSeconds() const override { return 0.0; }
    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram (int) override {}
    const juce::String getProgramName (int) override { return {}; }
    void changeProgramName (int, const juce::String&) override {}
    void getStateInformation (juce::MemoryBlock&) override {}
    void setStateInformation (const void*, int) override {}

    // --- Trabajo (hilo de mensajes)
    void loadFile (const juce::File& file);
    void exportStems (const juce::File& folder, bool wav, bool flac, int bitsPerSample);
    juce::String wildcardForAudioFiles() const { return formatManager.getWildcardForAllFormats(); }

    State  getState() const        { return (State) state.load(); }
    float  getProgress() const     { return progress.load(); }
    bool   isExporting() const     { return exporting.load(); }
    juce::String getStatusText() const { const juce::ScopedLock sl (statusLock); return status; }
    const Result& getResult() const    { return result; }     // solo hilo de mensajes

    // --- Reproduccion
    void play();
    void pause();
    void stopAndRewind();
    bool isPlaying() const { return playing.load(); }
    bool hasFinished() const;
    double getPositionSec() const;
    void setPositionSec (double seconds);

    // --- Mezcla (accesibles desde la interfaz)
    std::atomic<float> groupGain[3];
    std::atomic<bool>  groupSolo[3];
    std::atomic<bool>  stemMute[stemseer::kNumStems];

    JUCE_DECLARE_WEAK_REFERENCEABLE (StemSeerProcessor)

private:
    class WorkThread : public juce::Thread
    {
    public:
        WorkThread (const juce::String& n, std::function<void (WorkThread&)> f) : juce::Thread (n), fn (std::move (f)) {}
        void run() override { fn (*this); }
    private:
        std::function<void (WorkThread&)> fn;
    };

    struct Track
    {
        std::unique_ptr<juce::AudioFormatReaderSource> readerSource;
        juce::AudioTransportSource transport;
    };

    void runAnalysis (const juce::File& file, const juce::File& cacheDir, int myJobId, juce::Thread& thread);
    void finishLoading (Result r, int myJobId);
    void failJob (const juce::String& message, int myJobId);
    void setStatus (const juce::String& s) { const juce::ScopedLock sl (statusLock); status = s; }

    juce::AudioFormatManager formatManager;
    juce::TimeSliceThread readAheadThread;

    mutable juce::CriticalSection trackLock;
    std::vector<std::unique_ptr<Track>> tracks;
    juce::AudioBuffer<float> scratch;
    double hostSampleRate = 0.0;
    int hostBlockSize = 0;

    std::unique_ptr<WorkThread> worker, exporter;
    std::atomic<int>   state { 0 };
    std::atomic<int>   jobId { 0 };
    std::atomic<float> progress { 0.0f };
    std::atomic<bool>  exporting { false };
    std::atomic<bool>  playing { false };
    mutable juce::CriticalSection statusLock;
    juce::String status;
    Result result;
    juce::File currentCache;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (StemSeerProcessor)
};
