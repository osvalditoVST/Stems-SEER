#include "PluginProcessor.h"
#include "PluginEditor.h"

namespace
{
    constexpr int kPeakCount = 600;
    constexpr double kMaxMinutes = 15.0;
}

//==============================================================================
StemSeerProcessor::StemSeerProcessor()
    : AudioProcessor (BusesProperties().withOutput ("Output", juce::AudioChannelSet::stereo(), true)),
      readAheadThread ("StemSeer read-ahead")
{
    for (auto& g : groupGain) g.store (1.0f);
    for (auto& s : groupSolo) s.store (false);
    for (auto& m : stemMute)  m.store (false);

    formatManager.registerBasicFormats();
    readAheadThread.startThread();
    status = T8 ("Abre un archivo de audio para empezar.");
}

StemSeerProcessor::~StemSeerProcessor()
{
    if (worker)   worker->stopThread (20000);
    if (exporter) exporter->stopThread (20000);
    {
        std::vector<std::unique_ptr<Track>> old;
        { const juce::ScopedLock sl (trackLock); old.swap (tracks); }
    }
    readAheadThread.stopThread (2000);
    if (currentCache != juce::File()) currentCache.deleteRecursively();
}

//==============================================================================
void StemSeerProcessor::prepareToPlay (double sampleRate, int samplesPerBlock)
{
    const juce::ScopedLock sl (trackLock);
    hostSampleRate = sampleRate;
    hostBlockSize = samplesPerBlock;
    scratch.setSize (2, samplesPerBlock);
    for (auto& t : tracks) t->transport.prepareToPlay (samplesPerBlock, sampleRate);
}

void StemSeerProcessor::releaseResources()
{
    const juce::ScopedLock sl (trackLock);
    for (auto& t : tracks) t->transport.releaseResources();
}

bool StemSeerProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
{
    return layouts.getMainOutputChannelSet() == juce::AudioChannelSet::stereo();
}

void StemSeerProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer&)
{
    juce::ScopedNoDenormals noDenormals;
    buffer.clear();

    const juce::ScopedTryLock sl (trackLock);
    if (! sl.isLocked() || tracks.empty() || scratch.getNumSamples() == 0) return;

    const int total = buffer.getNumSamples();
    const int outCh = juce::jmin (2, buffer.getNumChannels());
    const bool anySolo = groupSolo[0].load() || groupSolo[1].load() || groupSolo[2].load();

    int done = 0;
    while (done < total)
    {
        const int chunk = juce::jmin (total - done, scratch.getNumSamples());
        for (size_t s = 0; s < tracks.size(); ++s)
        {
            juce::AudioSourceChannelInfo info (&scratch, 0, chunk);
            tracks[s]->transport.getNextAudioBlock (info);       // siempre se lee: mantiene los stems sincronizados

            const int g = kStemDefs[s].group;
            const bool audible = (! anySolo || groupSolo[g].load()) && ! stemMute[s].load();
            if (! audible) continue;
            const float gain = groupGain[g].load();
            for (int ch = 0; ch < outCh; ++ch)
                buffer.addFrom (ch, done, scratch, ch, 0, chunk, gain);
        }
        done += chunk;
    }
}

juce::AudioProcessorEditor* StemSeerProcessor::createEditor() { return new StemSeerEditor (*this); }

//==============================================================================
//  Reproduccion
//==============================================================================
void StemSeerProcessor::play()
{
    const juce::ScopedLock sl (trackLock);
    if (tracks.empty()) return;
    for (auto& t : tracks) t->transport.start();
    playing = true;
}

void StemSeerProcessor::pause()
{
    const juce::ScopedLock sl (trackLock);
    for (auto& t : tracks) t->transport.stop();
    playing = false;
}

void StemSeerProcessor::stopAndRewind()
{
    const juce::ScopedLock sl (trackLock);
    for (auto& t : tracks) { t->transport.stop(); t->transport.setPosition (0.0); }
    playing = false;
}

bool StemSeerProcessor::hasFinished() const
{
    const juce::ScopedTryLock sl (trackLock);
    return sl.isLocked() && ! tracks.empty() && tracks[0]->transport.hasStreamFinished();
}

double StemSeerProcessor::getPositionSec() const
{
    const juce::ScopedLock sl (trackLock);
    return tracks.empty() ? 0.0 : tracks[0]->transport.getCurrentPosition();
}

void StemSeerProcessor::setPositionSec (double seconds)
{
    const juce::ScopedLock sl (trackLock);
    for (auto& t : tracks) t->transport.setPosition (juce::jmax (0.0, seconds));
}

//==============================================================================
//  Carga y analisis
//==============================================================================
void StemSeerProcessor::loadFile (const juce::File& file)
{
    if (worker) worker->stopThread (20000);          // cancelar el trabajo anterior
    if (exporter) exporter->stopThread (20000);
    exporting = false;
    const int myId = ++jobId;

    playing = false;
    {
        std::vector<std::unique_ptr<Track>> old;
        { const juce::ScopedLock sl (trackLock); old.swap (tracks); }
    }
    if (currentCache != juce::File()) currentCache.deleteRecursively();
    result = Result();

    currentCache = juce::File::getSpecialLocation (juce::File::tempDirectory)
                       .getChildFile ("StemSeer")
                       .getChildFile (juce::String::toHexString (file.hashCode64()) + "_" + juce::String (juce::Time::currentTimeMillis()));

    state = (int) State::Working;
    progress = 0.0f;
    setStatus (T8 ("Leyendo archivo…"));
    sendChangeMessage();

    const juce::File cache = currentCache;
    worker = std::make_unique<WorkThread> ("StemSeer analisis", [this, file, cache, myId] (WorkThread& th)
    {
        runAnalysis (file, cache, myId, th);
    });
    worker->startThread();
}

void StemSeerProcessor::failJob (const juce::String& message, int myId)
{
    juce::MessageManager::callAsync ([weak = juce::WeakReference<StemSeerProcessor> (this), message, myId]
    {
        if (auto* p = weak.get())
            if (p->jobId.load() == myId)
            {
                p->setStatus (message);
                p->state = (int) State::Failed;
                p->sendChangeMessage();
            }
    });
}

void StemSeerProcessor::runAnalysis (const juce::File& file, const juce::File& cacheDir, int myId, juce::Thread& thread)
{
    using namespace stemseer;

    std::unique_ptr<juce::AudioFormatReader> reader (formatManager.createReaderFor (file));
    if (reader == nullptr) { failJob (T8 ("No se pudo abrir el archivo de audio."), myId); return; }

    const double sr = reader->sampleRate;
    const juce::int64 len = reader->lengthInSamples;
    if (sr <= 0.0 || len < 44100) { failJob (T8 ("El archivo es demasiado corto o está dañado."), myId); return; }
    if ((double) len / sr > kMaxMinutes * 60.0) { failJob (T8 ("El archivo supera los 15 minutos."), myId); return; }

    // ---- Lectura (estereo en RAM como float)
    std::vector<float> L ((size_t) len), R ((size_t) len);
    {
        juce::AudioBuffer<float> block (2, 65536);
        for (juce::int64 pos = 0; pos < len;)
        {
            if (thread.threadShouldExit()) return;
            const int n = (int) juce::jmin<juce::int64> (65536, len - pos);
            block.clear();
            reader->read (&block, 0, n, pos, true, true);
            const float* l = block.getReadPointer (0);
            const float* r = reader->numChannels > 1 ? block.getReadPointer (1) : l;
            std::copy (l, l + n, L.begin() + (std::ptrdiff_t) pos);
            std::copy (r, r + n, R.begin() + (std::ptrdiff_t) pos);
            pos += n;
            progress = 0.03f * (float) pos / (float) len;
        }
    }
    reader.reset();

    Result res;
    res.sampleRate = sr;
    res.durationSec = (double) len / sr;
    res.songName = file.getFileNameWithoutExtension();
    res.cacheDir = cacheDir;

    // ---- BPM y escala
    setStatus (T8 ("Detectando BPM y escala…"));
    {
        std::vector<float> mono ((size_t) len);
        for (size_t i = 0; i < mono.size(); ++i) mono[i] = 0.5f * (L[i] + R[i]);

        auto cb = [&] (float base, float scale) { return [&, base, scale] (float p) { progress = base + p * scale; return ! thread.threadShouldExit(); }; };
        res.bpm = detectBpm (mono, sr, cb (0.03f, 0.04f));
        if (thread.threadShouldExit()) return;
        res.key = detectKey (mono, sr, cb (0.07f, 0.02f));
        if (thread.threadShouldExit()) return;
    }

    // ---- Separacion (los stems se escriben a disco al vuelo para ahorrar RAM)
    setStatus (T8 ("Separando stems…"));
    if (! cacheDir.createDirectory()) { failJob (T8 ("No se pudo crear la carpeta temporal."), myId); return; }

    juce::WavAudioFormat wav;
    std::array<std::unique_ptr<juce::AudioFormatWriter>, kNumStems> writers;
    for (int s = 0; s < kNumStems; ++s)
    {
        std::unique_ptr<juce::FileOutputStream> os (cacheDir.getChildFile ("stem_" + juce::String (s) + ".wav").createOutputStream());
        if (os == nullptr) { failJob (T8 ("No se pudo escribir en la carpeta temporal."), myId); return; }
        writers[(size_t) s].reset (wav.createWriterFor (os.get(), sr, 2u, 24, juce::StringPairArray(), 0));
        if (writers[(size_t) s] == nullptr) { failJob (T8 ("No se pudo crear el archivo WAV."), myId); return; }
        os.release();
        res.peaks[(size_t) s].assign ((size_t) kPeakCount, 0.0f);
    }

    const juce::int64 samplesPerBucket = juce::jmax<juce::int64> (1, (len + kPeakCount - 1) / kPeakCount);
    std::array<juce::int64, kNumStems> written;
    written.fill (0);

    const bool ok = separateStems (L.data(), R.data(), (size_t) len, sr,
        [&] (int s, const float* l, const float* r, int n)
        {
            const float* ch[2] = { l, r };
            writers[(size_t) s]->writeFromFloatArrays (ch, 2, n);
            auto& pk = res.peaks[(size_t) s];
            for (int i = 0; i < n; ++i)
            {
                const size_t b = (size_t) juce::jmin<juce::int64> (kPeakCount - 1, (written[(size_t) s] + i) / samplesPerBucket);
                pk[b] = juce::jmax (pk[b], std::abs (l[i]), std::abs (r[i]));
            }
            written[(size_t) s] += n;
        },
        [&] (float p) { progress = 0.09f + 0.91f * p; return ! thread.threadShouldExit(); });

    for (auto& w : writers) w.reset();      // cierra y vuelca a disco
    if (! ok || thread.threadShouldExit()) return;

    juce::MessageManager::callAsync ([weak = juce::WeakReference<StemSeerProcessor> (this), r = std::move (res), myId]() mutable
    {
        if (auto* p = weak.get()) p->finishLoading (std::move (r), myId);
    });
}

void StemSeerProcessor::finishLoading (Result r, int myId)
{
    if (jobId.load() != myId) return;           // llego tarde: el usuario cargo otro archivo

    std::vector<std::unique_ptr<Track>> newTracks;
    for (int s = 0; s < stemseer::kNumStems; ++s)
    {
        std::unique_ptr<juce::AudioFormatReader> reader (formatManager.createReaderFor (r.cacheDir.getChildFile ("stem_" + juce::String (s) + ".wav")));
        if (reader == nullptr) { failJob (T8 ("No se pudieron abrir los stems generados."), myId); return; }

        auto t = std::make_unique<Track>();
        const double srcSr = reader->sampleRate;
        t->readerSource = std::make_unique<juce::AudioFormatReaderSource> (reader.release(), true);
        t->transport.setSource (t->readerSource.get(), 65536, &readAheadThread, srcSr, 2);
        if (hostSampleRate > 0.0) t->transport.prepareToPlay (hostBlockSize, hostSampleRate);
        newTracks.push_back (std::move (t));
    }

    {
        const juce::ScopedLock sl (trackLock);
        tracks.swap (newTracks);
    }
    result = std::move (r);
    progress = 1.0f;
    setStatus (T8 ("Análisis completado"));
    state = (int) State::Ready;
    sendChangeMessage();
}

//==============================================================================
//  Exportacion
//==============================================================================
void StemSeerProcessor::exportStems (const juce::File& folder, bool wantWav, bool wantFlac, int bits)
{
    if (getState() != State::Ready || exporting.load()) return;
    if (! wantWav && ! wantFlac) { setStatus (T8 ("Elige al menos un formato de exportación.")); sendChangeMessage(); return; }
    if (exporter) exporter->stopThread (5000);

    exporting = true;
    progress = 0.0f;
    setStatus (T8 ("Exportando stems…"));
    sendChangeMessage();

    const Result r = result;
    exporter = std::make_unique<WorkThread> ("StemSeer export", [this, folder, wantWav, wantFlac, bits, r] (WorkThread& th)
    {
        juce::WavAudioFormat wavFormat;
        juce::FlacAudioFormat flacFormat;
        const int total = stemseer::kNumStems * ((wantWav ? 1 : 0) + (wantFlac ? 1 : 0));
        int done = 0, failed = 0;

        for (int s = 0; s < stemseer::kNumStems && ! th.threadShouldExit(); ++s)
        {
            for (int pass = 0; pass < 2 && ! th.threadShouldExit(); ++pass)
            {
                const bool isWav = (pass == 0);
                if ((isWav && ! wantWav) || (! isWav && ! wantFlac)) continue;

                std::unique_ptr<juce::AudioFormatReader> reader (formatManager.createReaderFor (r.cacheDir.getChildFile ("stem_" + juce::String (s) + ".wav")));
                const auto name = juce::File::createLegalFileName (r.songName + " - " + kStemDefs[s].fileLabel);
                const juce::File out = folder.getChildFile (name + (isWav ? ".wav" : ".flac"));
                out.deleteFile();
                std::unique_ptr<juce::FileOutputStream> os (out.createOutputStream());

                bool ok = false;
                if (reader != nullptr && os != nullptr)
                {
                    juce::AudioFormat& fmt = isWav ? static_cast<juce::AudioFormat&> (wavFormat) : static_cast<juce::AudioFormat&> (flacFormat);
                    std::unique_ptr<juce::AudioFormatWriter> w (fmt.createWriterFor (os.get(), reader->sampleRate, 2u, bits, juce::StringPairArray(), 0));
                    if (w != nullptr)
                    {
                        os.release();
                        ok = w->writeFromAudioReader (*reader, 0, reader->lengthInSamples);
                    }
                }
                if (! ok) ++failed;
                progress = (float) (++done) / (float) total;
            }
        }

        juce::String msg = th.threadShouldExit() ? T8 ("Exportación cancelada.")
                         : failed == 0 ? T8 ("Exportación completa: ") + juce::String (done) + T8 (" archivos en ") + folder.getFullPathName()
                                       : T8 ("Exportación terminada con ") + juce::String (failed) + T8 (" errores.");

        juce::MessageManager::callAsync ([weak = juce::WeakReference<StemSeerProcessor> (this), msg]
        {
            if (auto* p = weak.get()) { p->exporting = false; p->setStatus (msg); p->sendChangeMessage(); }
        });
    });
    exporter->startThread();
}
