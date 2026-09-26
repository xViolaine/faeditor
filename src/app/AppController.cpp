#include "app/AppController.h"
#include "model/PartModel.h"
#include "export/DawExporter.h"

#include <QThread>
#include <QTimer>
#include <QGuiApplication>
#include <QCoreApplication>
#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QUrlQuery>
#include <QDebug>
#include <QByteArray>

AppController::AppController(QObject *parent)
    : QObject(parent)
{
    m_engine = new SysexEngine(this);
    m_platform = new AutoDetectRolandPlatform(m_engine);
    m_midi = new MidiDeviceModel(m_platform, this);
    m_undo = new UndoController(this);
    m_studioSet = new StudioSetModel(m_platform, m_undo, this);
    m_studioSets = new StudioSetBrowserModel(m_platform, m_studioSet, this);
    m_tones = new ToneBrowserModel(this);
    m_tones->loadCatalog();
    m_waveforms = new WaveformCatalog(this);
    // Bundled Sound List names — load every start so Tone Edit never depends on Waves.
    m_waveforms->loadCatalog();
    m_audioFx = new AudioFxModel(m_platform, this);
    m_tone = new TemporaryToneModel(m_engine, m_platform, m_studioSet, this);
    if (m_tone) {
        if (auto *sn = m_tone->snSynth())
            sn->setWaveformCatalog(m_waveforms);
        if (auto *pcm = m_tone->pcmSynth())
            pcm->setWaveformCatalog(m_waveforms);
    }
    m_library = new ProjectStore(m_studioSet, m_audioFx, m_tone, this);
    m_svdImport = new SvdImportModel(m_platform, m_studioSet, this);
    m_studioSetOrder = new SvdStudioSetOrderModel(this);
    m_scene = new FantomSceneModel(m_platform, this);
    connect(m_svdImport, &SvdImportModel::tonePushed, this,
            [this](const QString &name, int part) {
        setHint(QStringLiteral("Imported “%1” to Part %2 Temporary SN-A tone. Use Write on the FA to store it permanently.")
                    .arg(name).arg(part));
        if (m_tone)
            m_tone->pull();
    });

    m_studioSet->setToneNameResolver([this](int msb, int lsb, int pc) {
        return m_tones->resolveName(msb, lsb, pc);
    });

    connect(m_studioSet, &StudioSetModel::autosaveRequested, this, [this]() {
        m_autosaveTimer.start(400);
    });
    connect(m_studioSet, &StudioSetModel::deviceTraceChanged, this, [this]() {
        if (!m_studioSet->deviceTrace().isEmpty())
            setHint(m_studioSet->deviceTrace());
    });
    m_midiListenFlush.setSingleShot(true);
    m_midiListenFlush.setInterval(400);
    connect(&m_midiListenFlush, &QTimer::timeout, this, &AppController::flushMidiListen);
    connect(m_engine, &SysexEngine::sysexReceived, this, &AppController::onIncomingMidi,
            Qt::QueuedConnection);
    m_autosaveTimer.setSingleShot(true);
    connect(&m_autosaveTimer, &QTimer::timeout, m_library, &ProjectStore::autosave);

    connect(m_studioSets, &StudioSetBrowserModel::recalled, this, [this](int) {
        setHint(QStringLiteral("Set loaded. Select a part, then click a tone to assign (icon previews)."));
        setMainTab(0); // Stay on Sets & Tones workspace
    });

    connect(m_studioSets, &StudioSetBrowserModel::statusTextChanged, this, [this]() {
        if (!m_studioSets->statusText().isEmpty())
            setHint(m_studioSets->statusText());
    });

    connect(m_midi, &MidiDeviceModel::connectedChanged, this, [this]() {
        QTimer::singleShot(0,this,[this](){emit deviceFamilyChanged();});
        if (m_midi->connected()) {
            m_keepInstrumentConnection = true;
            return;
        }
        // iPhone lock drops USB MIDI while Qt still reports ApplicationActive.
        // Keep the session so we reconnect on return; only Disconnect clears it.
        if (isMobileUi()) {
            if (m_keepInstrumentConnection && !m_reconnectingForeground
                && qGuiApp && qGuiApp->applicationState() == Qt::ApplicationActive)
                startForegroundReconnect();
            return;
        }
        if (!m_reconnectingForeground
            && qGuiApp
            && qGuiApp->applicationState() == Qt::ApplicationActive) {
            m_keepInstrumentConnection = false;
        }
        setHint(m_midi->statusText().isEmpty()
                    ? QStringLiteral("FA disconnected — cable unplugged or powered off")
                    : m_midi->statusText());
    });

    m_library->recoverAutosaveIfNeeded();
    setHint(QStringLiteral("Looking for a Roland FA or FANTOM-0…"));

    m_foregroundReconnectTimer.setSingleShot(true);
    connect(&m_foregroundReconnectTimer, &QTimer::timeout, this, &AppController::tryForegroundReconnect);

    consumeLaunchArguments();

    if (isMobileUi()) {
        setHint(m_storeScreenshotView.isEmpty()
                    ? QStringLiteral("Connect MIDI to read your FA. Use GENERIC USB mode.")
                    : m_workflowHint);
        connect(qGuiApp, &QGuiApplication::applicationStateChanged,
                this, &AppController::handleApplicationState);
    } else if (m_storeScreenshotView.isEmpty()) {
        // After the desktop UI is up, auto-connect and pull Temporary data.
        QTimer::singleShot(500, this, &AppController::startupConnect);
    }
}

void AppController::setHint(const QString &h)
{
    if (m_workflowHint == h)
        return;
    m_workflowHint = h;
    emit workflowHintChanged();
}

static QString formatIncomingMidi(const QByteArray &raw)
{
    if (raw.isEmpty())
        return {};
    const auto b0 = static_cast<quint8>(raw[0]);
    if (b0 == 0xF8 || b0 == 0xFE || b0 == 0xFA || b0 == 0xFB || b0 == 0xFC)
        return {};
    if (b0 != 0xF0) {
        if ((b0 & 0xF0) == 0xC0 && raw.size() >= 2)
            return QStringLiteral("PC ch%1 %2").arg((b0 & 0x0F) + 1).arg(quint8(raw[1]));
        return {};
    }
    if (raw.size() >= 5 && quint8(raw[1]) == 0x7E && quint8(raw[3]) == 0x06)
        return {};
    if (raw.size() >= 14 && quint8(raw[1]) == 0x41 && quint8(raw[6]) == 0x12) {
        const quint8 a0 = quint8(raw[7]);
        const quint8 a1 = quint8(raw[8]);
        const quint8 a2 = quint8(raw[9]);
        const quint8 a3 = quint8(raw[10]);
        const quint8 data = quint8(raw[11]);
        if (a0 == 0x18 && a1 == 0x00 && a2 == 0x00 && a3 == 0x54)
            return QStringLiteral("CurrentPart %1").arg(data + 1);
        if (a0 == 0x18 && a1 == 0x00 && a2 >= 0x40 && a2 <= 0x4F && a3 == 0x02)
            return QStringLiteral("KbdSw P%1=%2").arg(a2 - 0x40 + 1).arg(data);
        if (a0 == 0x18 && a1 == 0x00 && a2 == 0x50 && a3 == 0x34)
            return QStringLiteral("PadPartSelect %1").arg(data);
        return QStringLiteral("%1 %2 %3 %4=%5")
            .arg(a0, 2, 16, QLatin1Char('0'))
            .arg(a1, 2, 16, QLatin1Char('0'))
            .arg(a2, 2, 16, QLatin1Char('0'))
            .arg(a3, 2, 16, QLatin1Char('0'))
            .arg(data, 2, 16, QLatin1Char('0'));
    }
    return QString::fromLatin1(raw.toHex(' ')).left(72);
}

void AppController::onIncomingMidi(const QByteArray &raw)
{
    const QString line = formatIncomingMidi(raw);
    if (line.isEmpty())
        return;
    m_midiListenBurst.append(line);
    while (m_midiListenBurst.size() > 24)
        m_midiListenBurst.removeFirst();
    if (!m_midiListenFlush.isActive())
        m_midiListenFlush.start();
}

void AppController::flushMidiListen()
{
    if (m_midiListenBurst.isEmpty())
        return;
    const QString text = QStringLiteral("FA→ ") + m_midiListenBurst.join(QStringLiteral(" · "));
    qInfo().noquote() << text;
    setHint(text);
    m_midiListenBurst.clear();
}

bool AppController::isMobileUi() const
{
    return QGuiApplication::platformName() == QStringLiteral("ios")
        || QCoreApplication::arguments().contains(QStringLiteral("--mobile-ui"));
}

void AppController::pauseInstrumentForBackground()
{
    m_foregroundReconnectTimer.stop();
    m_reconnectingForeground = false;
    m_autosaveTimer.stop();
    if (m_library)
        m_library->autosave();
    if (m_studioSets)
        m_studioSets->cancelScan();
    if (m_midi && m_midi->connected())
        m_keepInstrumentConnection = true;
    if (m_midi)
        m_midi->disconnectDevice();
    if (m_keepInstrumentConnection)
        setHint(QStringLiteral("MIDI paused — will reconnect when you return."));
}

void AppController::handleApplicationState(Qt::ApplicationState state)
{
    if (state == Qt::ApplicationInactive
        || state == Qt::ApplicationSuspended
        || state == Qt::ApplicationHidden) {
        pauseInstrumentForBackground();
        return;
    }

    if (state != Qt::ApplicationActive)
        return;

    if (m_keepInstrumentConnection)
        startForegroundReconnect();
    else if (m_midi)
        m_midi->refresh();
}

void AppController::startForegroundReconnect()
{
    m_foregroundReconnectTimer.stop();
    m_foregroundReconnectAttempt = 0;
    m_reconnectingForeground = true;
    setHint(QStringLiteral("Reconnecting MIDI…"));
    // USB class-compliant MIDI is not enumerable immediately after iPhone unlock.
    m_foregroundReconnectTimer.start(1500);
}

void AppController::tryForegroundReconnect()
{
    if (!m_midi || !m_keepInstrumentConnection) {
        m_reconnectingForeground = false;
        m_foregroundReconnectTimer.stop();
        return;
    }
    if (qGuiApp && qGuiApp->applicationState() != Qt::ApplicationActive) {
        m_reconnectingForeground = false;
        m_foregroundReconnectTimer.stop();
        return;
    }

    setHint(QStringLiteral("Reconnecting MIDI…"));
    if (m_midi->autoConnectFa()) {
        m_reconnectingForeground = false;
        m_foregroundReconnectTimer.stop();
        QTimer::singleShot(600, this, [this]() {
            if (m_midi && m_midi->connected() && m_keepInstrumentConnection)
                finishSessionAfterMidiConnect();
        });
        return;
    }

    static const int kDelaysMs[] = {1500, 2000, 3000, 5000, 8000, 12000, 15000, 15000};
    if (m_foregroundReconnectAttempt < int(sizeof(kDelaysMs) / sizeof(kDelaysMs[0]))) {
        const int delay = kDelaysMs[m_foregroundReconnectAttempt++];
        m_foregroundReconnectTimer.start(delay);
        return;
    }

    m_reconnectingForeground = false;
    setHint(QStringLiteral("MIDI did not return after sleep — tap Connect."));
}

void AppController::disconnectInstrument()
{
    m_keepInstrumentConnection = false;
    m_reconnectingForeground = false;
    m_foregroundReconnectTimer.stop();
    if (m_midi)
        m_midi->disconnectDevice();
    setHint(QStringLiteral("Disconnected. Tap Connect when the FA is ready."));
}

void AppController::openMidiDialog()
{
    if (m_midi) {
        m_midi->refresh();
        // Stale "connected" after FA power-cycle / CoreMIDI renumber
        if (m_midi->connected() && m_platform && !m_platform->isConnected())
            m_midi->disconnectDevice();
    }
    // Force a rising edge so the Popup always opens, even if already flagged open.
    if (m_connectDialogOpen) {
        m_connectDialogOpen = false;
        emit connectDialogOpenChanged();
    }
    m_connectDialogOpen = true;
    emit connectDialogOpenChanged();
}

void AppController::startupConnect()
{
    if (!m_midi) {
        setHint(QStringLiteral("Connect MIDI, then open a Studio Set."));
        return;
    }

    setHint(QStringLiteral("Looking for Roland FA / FANTOM-0 MIDI ports…"));
    if (!m_midi->autoConnectFa()) {
        setHint(QStringLiteral("No supported Roland found — power on / plug in USB, then click MIDI."));
        return;
    }
    finishSessionAfterMidiConnect();
}

void AppController::finishSessionAfterMidiConnect()
{
    m_keepInstrumentConnection = true;
    emit deviceFamilyChanged();

    if (fantomDevice()) {
        setHint(QStringLiteral("Connected to %1 — pulling Temporary Scene…").arg(m_platform->profile().model));
        if (m_scene->pull()) setHint(QStringLiteral("Ready — Temporary Scene “%1”. All edits remain temporary until saved on the FANTOM.").arg(m_scene->name()));
        else setHint(QStringLiteral("FANTOM connected, but Scene pull failed: %1").arg(m_scene->status()));
        return;
    }

    setHint(QStringLiteral("Connected — pulling Temporary Studio Set…"));
    const bool pulled = pull();
    if (pulled && m_audioFx)
        m_audioFx->pullFromDevice();

    if (pulled) {
        setHint(QStringLiteral("Ready — “%1”. Change part on the FA pads (Transmit Edit Data ON).")
                    .arg(m_studioSet->name()));
    } else {
        setHint(QStringLiteral("MIDI connected, but pull failed — click Pull Temp (FA must be ready)."));
    }
}

bool AppController::exportStudioSetMidi(const QUrl &url)
{
    QString error;
    const QString path = url.isLocalFile() ? url.toLocalFile() : url.toString();
    const bool ok = DawExporter::writeStudioSetMidi(*m_studioSet, path, &error);
    setHint(ok ? QStringLiteral("Exported Studio Set MIDI setup.")
               : QStringLiteral("Studio Set export failed: %1").arg(error));
    return ok;
}

bool AppController::exportToneNamesMidnam(const QUrl &url)
{
    QString error;
    const QString path = url.isLocalFile() ? url.toLocalFile() : url.toString();
    const bool ok = DawExporter::writeMidnam(*m_tones, path, &error);
    setHint(ok ? QStringLiteral("Exported Roland FA tone names (.midnam).")
               : QStringLiteral("Tone-name export failed: %1").arg(error));
    return ok;
}

void AppController::setMainTab(int v)
{
    v = qBound(0, v, 3);
    if (m_mainTab == v)
        return;
    m_mainTab = v;
    emit mainTabChanged();
    // 0 Sets & Tones (+ Library), 1 Mixer, 2 Effects, 3 Tone
    if (v == 2 && m_midi && m_midi->connected())
        m_audioFx->pullFromDevice();
    if (v == 3 && m_midi && m_midi->connected() && m_tone)
        m_tone->pull();
}

void AppController::setConnectDialogOpen(bool v)
{
    if (m_connectDialogOpen == v)
        return;
    m_connectDialogOpen = v;
    emit connectDialogOpenChanged();
}

void AppController::applyToneToSelectedPart(int toneRow)
{
    const auto tone = m_tones->toneAt(toneRow);
    if (tone.isEmpty())
        return;
    auto *p = m_studioSet->selectedPartModel();
    if (!p)
        return;
    p->setBankMsb(tone.value(QStringLiteral("bankMsb")).toInt());
    p->setBankLsb(tone.value(QStringLiteral("bankLsb")).toInt());
    p->setProgram(tone.value(QStringLiteral("program")).toInt());
    p->setToneName(tone.value(QStringLiteral("name")).toString());
    setHint(QStringLiteral("Part %1 → %2").arg(p->partNumber()).arg(p->toneName()));
}

void AppController::previewTone(int toneRow)
{
    applyToneToSelectedPart(toneRow);
    if (!m_platform || !m_platform->supportsWorkspace(InstrumentPlatform::Workspace::NotePreview, true)
        || !m_platform->isConnected())
        return;
    auto *p = m_studioSet->selectedPartModel();
    if (!p)
        return;
    const quint8 ch = static_cast<quint8>(p->receiveChannel() & 0x0F);
    m_platform->sendPreviewNote(ch, 60, 100, true, nullptr);
    QTimer::singleShot(300, this, [this, ch]() {
        if (m_platform)
            m_platform->sendPreviewNote(ch, 60, 0, false, nullptr);
    });
}

bool AppController::pull()
{
    if (fantomDevice()) {
        const bool ok = m_scene && m_scene->pull();
        if (ok)
            setHint(QStringLiteral("Pulled Temporary Scene “%1”.").arg(m_scene->name()));
        return ok;
    }
    const bool ok = m_studioSet->pullFromDevice();
    if (ok)
        setHint(QStringLiteral("Pulled Temporary Studio Set “%1”. Select a part and change its tone.")
                    .arg(m_studioSet->name()));
    return ok;
}

bool AppController::push()
{
    if (fantomDevice()) {
        const bool ok = m_scene && m_scene->push();
        setHint(ok ? QStringLiteral("Pushed Temporary Scene. Save it on the FANTOM if you want to keep it.")
                   : m_scene->status());
        return ok;
    }
    // Blobs + typed studio overlays, then tone blobs, then System Audio FX + Master EQ.
    if (!m_studioSet->pushToDevice())
        return false;
    if (m_tone && m_library) {
        // Settle after Studio Set rewrite before Temporary Tone DT1.
        QThread::msleep(250);
        const auto blobs = m_library->toneBlobs();
        if (!blobs.isEmpty() && !m_tone->pushToneBlobs(blobs)) {
            setHint(QStringLiteral("Studio Set pushed, but tone blob push failed."));
            return false;
        }
    }
    if (m_audioFx && !m_audioFx->pushToDevice()) {
        setHint(QStringLiteral("Studio Set pushed, but Audio FX push failed."));
        return false;
    }
    if (!m_studioSet->pushSystemMasterEq()) {
        setHint(QStringLiteral("Studio Set + Audio FX pushed, but Master EQ push failed."));
        return false;
    }
    setHint(QStringLiteral("Temporary updated on the FA."));
    return true;
}

bool AppController::fantomDevice() const
{
    return m_platform && m_platform->isFantom0();
}

void AppController::previewFantomZone(int zone,int note,int velocity,bool on)
{
    if (!m_platform || !fantomDevice())
        return;
    QString error;
    m_platform->sendPreviewNote(qBound(0,zone,15),qBound(0,note,127),
                                qBound(0,velocity,127),on,&error);
}

bool AppController::openStudioSet(int row)
{
    m_studioSets->setCurrentRow(row);
    return m_studioSets->recallRow(row, true);
}

void AppController::goChangeToneForPart(int partIndex)
{
    m_studioSet->setSelectedPart(partIndex);
    setMainTab(0); // Sets & Tones workspace
    setHint(QStringLiteral("Changing instrument for Part %1 — click a tone to assign, or the icon to preview.")
                .arg(partIndex + 1));
}

bool AppController::saveToLibrary()
{
    if (!m_library || !m_library->save())
        return false;
    setMainTab(0); // Sets & Tones (library panel)
    setHint(QStringLiteral("Saved “%1” to library.").arg(m_library->currentName()));
    return true;
}

bool AppController::loadLibrary(int row)
{
    if (!m_library) {
        setHint(QStringLiteral("Library is not available."));
        return false;
    }
    if (!m_library->load(row)) {
        const auto err = m_library->lastError();
        setHint(err.isEmpty() ? QStringLiteral("Failed to load library file.") : err);
        return false;
    }
    setHint(QStringLiteral("Loaded “%1” from library.").arg(m_library->currentName()));
    return true;
}

void AppController::consumeLaunchArguments()
{
    const QString envView = qEnvironmentVariable("FAEDITOR_DEMO_SCENARIO");
    if (!envView.isEmpty())
        applyStoreScreenshot(envView);

    const auto args = QCoreApplication::arguments();
    for (int i = 1; i < args.size(); ++i) {
        const QString &arg = args.at(i);
        if (arg.startsWith(QLatin1String("faeditor://"))) {
            handleLaunchUrl(QUrl(arg));
            continue;
        }
        QString view;
        if (arg.startsWith(QLatin1String("--demo-scenario=")))
            view = arg.section(QLatin1Char('='), 1);
        else if (arg.startsWith(QLatin1String("--store-screenshot=")))
            view = arg.section(QLatin1Char('='), 1);
        else if ((arg == QLatin1String("--demo-scenario")
                  || arg == QLatin1String("--store-screenshot"))
                 && i + 1 < args.size()) {
            view = args.at(++i);
        }
        if (!view.isEmpty())
            applyStoreScreenshot(view);
    }
}

QString AppController::normalizeStoreScreenshotView(const QString &view) const
{
    const QString v = view.trimmed().toLower();
    static const QStringList allowed = {
        QStringLiteral("sets"), QStringLiteral("mixer"), QStringLiteral("mixer-part"),
        QStringLiteral("effects"), QStringLiteral("tone"), QStringLiteral("tone-filter"),
        QStringLiteral("tone-amp"), QStringLiteral("library")
    };
    if (allowed.contains(v))
        return v;
    return {};
}

bool AppController::handleLaunchUrl(const QUrl &url)
{
    if (url.scheme() != QLatin1String("faeditor"))
        return false;
    if (url.host() != QLatin1String("demo") && url.path() != QLatin1String("/demo"))
        return false;
    const QUrlQuery query(url);
    QString view = query.queryItemValue(QStringLiteral("scenario"));
    if (view.isEmpty())
        view = query.queryItemValue(QStringLiteral("view"));
    if (view.isEmpty())
        view = QStringLiteral("sets");
    return applyStoreScreenshot(view);
}

bool AppController::loadStoreScreenshotFixture()
{
    static const QStringList paths = {
        QStringLiteral(":/qt/qml/FAEditor/demo/studio-set.json"),
        QStringLiteral(":/FAEditor/demo/studio-set.json"),
        QStringLiteral(":/demo/studio-set.json")
    };
    QByteArray bytes;
    for (const auto &path : paths) {
        QFile f(path);
        if (f.open(QIODevice::ReadOnly)) {
            bytes = f.readAll();
            break;
        }
    }
    if (bytes.isEmpty()) {
        setHint(QStringLiteral("Store screenshot fixture is missing."));
        return false;
    }
    const auto doc = QJsonDocument::fromJson(bytes);
    if (!doc.isObject())
        return false;
    const auto root = doc.object();
    const auto studio = root.value(QStringLiteral("studioSet")).toObject();
    if (studio.isEmpty() || !m_studioSet || !m_studioSet->fromJson(studio))
        return false;
    if (m_audioFx)
        m_audioFx->fromJson(root.value(QStringLiteral("audioFx")).toObject());
    m_studioSet->setSelectedPart(0);
    if (m_tone)
        m_tone->setSelectedStage(QStringLiteral("osc"));
    if (m_library) {
        m_studioSet->setName(QStringLiteral("Soundtrack"));
        m_library->saveNamedCopy(QStringLiteral("Soundtrack"));
        m_studioSet->setName(QStringLiteral("Piano Stack"));
        m_library->saveNamedCopy(QStringLiteral("Piano Stack"));
        m_studioSet->fromJson(studio);
        m_library->saveNamedCopy(QStringLiteral("Live Band"));
    }
    return true;
}

bool AppController::applyStoreScreenshot(const QString &view)
{
    const QString normalized = normalizeStoreScreenshotView(view);
    if (normalized.isEmpty()) {
        qWarning() << "Unknown store screenshot view:" << view;
        return false;
    }
    if (!loadStoreScreenshotFixture())
        return false;
    if (m_midi)
        m_midi->setPreviewConnected(QStringLiteral("FA-08"));
    m_keepInstrumentConnection = false;
    if (normalized == QLatin1String("library"))
        setMainTab(0);
    else if (normalized == QLatin1String("mixer") || normalized == QLatin1String("mixer-part"))
        setMainTab(1);
    else if (normalized == QLatin1String("effects"))
        setMainTab(2);
    else if (normalized == QLatin1String("tone")
             || normalized == QLatin1String("tone-filter")
             || normalized == QLatin1String("tone-amp")) {
        setMainTab(3);
        if (m_tone) {
            if (normalized == QLatin1String("tone-filter"))
                m_tone->setSelectedStage(QStringLiteral("filter"));
            else if (normalized == QLatin1String("tone-amp"))
                m_tone->setSelectedStage(QStringLiteral("amp"));
            else
                m_tone->setSelectedStage(QStringLiteral("osc"));
        }
    } else
        setMainTab(0);
    setHint(QStringLiteral("Ready — “%1”.").arg(m_studioSet->name()));
    m_storeScreenshotView = normalized;
    emit storeScreenshotViewChanged();
    QTimer::singleShot(800, this, [this, normalized]() {
        qInfo().noquote() << QStringLiteral("DEMO_READY:store:%1:en_US").arg(normalized);
    });
    return true;
}

QString AppController::qtLicenseNotice() const
{
    // Same attribution pattern as righthere-app (Utils::licenseInfo / QtLegal::aboutQT).
    const QString caption = QStringLiteral(
        "<h3>Qt libraries</h3>"
        "<p>This software uses the Qt libraries, version %1.</p>"
        "<p>We have used a precompiled version of Qt (LGPLv3), which has been downloaded "
        "from the official website <a href=\"https://%2\">%2</a>.</p>")
        .arg(QLatin1String(QT_VERSION_STR), QStringLiteral("qt.io/download"));

    const QString body = QStringLiteral(
        "<p>Qt is a C++ toolkit for cross-platform application development.</p>"
        "<p>Qt provides single-source portability across all major desktop operating systems. "
        "It is also available for embedded Linux and other embedded and mobile operating systems.</p>"
        "<p>Qt is available under multiple licensing options designed to accommodate the needs "
        "of our various users.</p>"
        "<p>Qt licensed under our commercial license agreement is appropriate for development of "
        "proprietary/commercial software where you do not want to share any source code with third "
        "parties or otherwise cannot comply with the terms of GNU (L)GPL.</p>"
        "<p>Qt licensed under GNU (L)GPL is appropriate for the development of Qt applications "
        "provided you can comply with the terms and conditions of the respective licenses.</p>"
        "<p>Please see <a href=\"https://%2/\">%2</a> for an overview of Qt licensing.</p>"
        "<p>Copyright (C) %1 The Qt Company Ltd and other contributors.</p>"
        "<p>Qt and the Qt logo are trademarks of The Qt Company Ltd.</p>"
        "<p>Qt is The Qt Company Ltd product developed as an open source project. "
        "See <a href=\"https://%3/\">%3</a> for more information.</p>")
        .arg(QStringLiteral("2026"),
             QStringLiteral("qt.io/licensing"),
             QStringLiteral("qt.io"));

    return caption + body;
}
