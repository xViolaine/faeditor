#include "platform/RolandFAPlatform.h"
#include "midi/SysexEngine.h"

#include <RtMidi.h>
#include "midi/MidiApi.h"

using namespace roland;

bool RolandFAPlatform::isConnected() const { return m_engine && m_engine->isOpen(); }

InstrumentPlatform::DeviceProfile RolandFAPlatform::profile() const
{
    using A=FeatureAccess; using W=Workspace; using T=ToneEngineCapability;
    return {QStringLiteral("Roland"),QStringLiteral("FA-06/07/08"),
            {T::SuperNaturalSynth,T::PcmSynth,T::SuperNaturalAcoustic,T::PcmDrum,T::SuperNaturalDrum},16,
            {{W::MidiConnection,A::ReadWrite},{W::DeviceIdentity,A::ReadOnly},{W::StudioSets,A::ReadWrite},
             {W::ToneEditing,A::ReadWrite},{W::AudioFx,A::ReadWrite},{W::NotePreview,A::ReadWrite},{W::Library,A::ReadWrite}}};
}

bool RolandFAPlatform::nameIsDawControl(const QString &name)
{
    const auto n=name.toLower();
    return n.contains(QStringLiteral("daw")) || n.contains(QStringLiteral("mackie"))
           || n.startsWith(QStringLiteral("midiin2")) || n.startsWith(QStringLiteral("midiout2"))
           || (n.contains(QStringLiteral("ctrl")) && !n.contains(QStringLiteral("controller")));
}

bool RolandFAPlatform::nameLooksLikeFa(const QString &name)
{
    if (nameIsDawControl(name)) return false;
    const auto n=name.toUpper();
    if (n.contains(QStringLiteral("FA-06")) || n.contains(QStringLiteral("FA-07")) || n.contains(QStringLiteral("FA-08"))
        || n.contains(QStringLiteral("FA 06")) || n.contains(QStringLiteral("FA 07")) || n.contains(QStringLiteral("FA 08"))
        || n.contains(QStringLiteral("FA06")) || n.contains(QStringLiteral("FA07")) || n.contains(QStringLiteral("FA08"))) return true;
    return n.contains(QLatin1String("ROLAND")) && n.contains(QLatin1String("FA"));
}

bool RolandFAPlatform::discoverMidiPorts(QVector<MidiPort> *inputs,QVector<MidiPort> *outputs,QString *error)
{
    if (inputs) inputs->clear(); if (outputs) outputs->clear();
    try {
        RtMidiIn in(faeditor::midiApi()); RtMidiOut out(faeditor::midiApi());
        if (inputs) for (unsigned i=0;i<in.getPortCount();++i) { const auto n=QString::fromStdString(in.getPortName(i)); inputs->push_back({int(i),n,nameIsDawControl(n),nameLooksLikeFa(n)}); }
        if (outputs) for (unsigned i=0;i<out.getPortCount();++i) { const auto n=QString::fromStdString(out.getPortName(i)); outputs->push_back({int(i),n,nameIsDawControl(n),nameLooksLikeFa(n)}); }
        return true;
    } catch (const RtMidiError &e) { if (error) *error=QString::fromStdString(e.getMessage()); return false; }
}

bool RolandFAPlatform::openMidiConnection(int in,int out,QString *error){return m_engine&&m_engine->openPorts(in,out,error);}
void RolandFAPlatform::closeMidiConnection(){if(m_engine)m_engine->closePorts();}
bool RolandFAPlatform::midiConnectionHealthy()const{return m_engine&&m_engine->portsHealthy();}
bool RolandFAPlatform::detectDevice(quint8 *deviceId,int timeoutMs,QString *error)
{
    if (error) error->clear();
    if (!m_engine || !m_engine->sendIdentityRequest(error)) return false;
    quint8 id=0x10;
    if (!m_engine->waitIdentityReply(&id,timeoutMs)) { m_engine->setDeviceId(0x10); if(deviceId)*deviceId=0x10; return false; }
    m_engine->setDeviceId(id); if(deviceId)*deviceId=id; return true;
}
bool RolandFAPlatform::sendPreviewNote(int channel,int note,int velocity,bool on,QString *error)
{
    QByteArray message; message.append(char((on?0x90:0x80)|(channel&0x0f))); message.append(char(note&0x7f)); message.append(char(velocity&0x7f));
    return m_engine&&m_engine->sendMessage(message,error);
}

bool RolandFAPlatform::recallPerformance(int msb, int lsb, int program, QString *error)
{
    if (!isConnected()) { if (error) *error = QStringLiteral("Not connected"); return false; }
    if (!m_engine->writeParam(Address{{0x01, 0x00, 0x00, 0x00}}, QByteArray(1, char(1)), error))
        return false;
    QByteArray select; select.append(char(msb & 0x7f)); select.append(char(lsb & 0x7f)); select.append(char(program & 0x7f));
    return m_engine->write(Address{{0x01, 0x00, 0x00, 0x04}}, select, error);
}

bool RolandFAPlatform::readTemporaryPerformanceName(QString *name, QString *error)
{
    QByteArray data;
    if (!m_engine || !m_engine->read(addr::kStudioSetCommon, 16, &data, error, 2500)) return false;
    if (name) *name = QString::fromLatin1(data.constData(), qMin(16, data.size())).trimmed();
    return true;
}

Address RolandFAPlatform::studioAddress(StudioBlock b, int i) const
{
    switch (b) {
    case StudioBlock::Common: return addr::kStudioSetCommon; case StudioBlock::Chorus: return addr::kStudioSetChorus;
    case StudioBlock::Reverb: return addr::kStudioSetReverb; case StudioBlock::Ifx: return addr::kStudioSetIfx;
    case StudioBlock::MasterComp: return addr::kStudioSetMasterComp; case StudioBlock::Controller: return addr::kStudioSetController;
    case StudioBlock::PadCommon: return addr::kStudioSetPadCommon; case StudioBlock::Midi: return addr::midiChannel(i);
    case StudioBlock::Part: return addr::part(i); case StudioBlock::PartEq: return addr::partEq(i);
    case StudioBlock::Zone: return addr::zone(i); case StudioBlock::Pad: return addr::pad(i);
    } return {};
}
bool RolandFAPlatform::readStudioBlock(StudioBlock b,int i,int s,QByteArray*d,QString*e){return m_engine&&m_engine->read(studioAddress(b,i),s,d,e);}
bool RolandFAPlatform::writeStudioBlock(StudioBlock b,int i,const QByteArray&d,QString*e){return m_engine&&m_engine->write(studioAddress(b,i),d,e);}
bool RolandFAPlatform::writeStudioParameter(StudioBlock b,int i,int o,const QByteArray&d,QString*e){return m_engine&&m_engine->writeParam(addOffset(studioAddress(b,i),o),d,e);}
bool RolandFAPlatform::readMasterEq(QByteArray*d,QString*e){return m_engine&&m_engine->read(addr::kSystemMasterEq,sysOff::MasterEqSize,d,e);}
bool RolandFAPlatform::writeMasterEq(const QByteArray&d,QString*e){return m_engine&&m_engine->write(addr::kSystemMasterEq,d,e);}

Address RolandFAPlatform::audioAddress(AudioBlock block) const
{
    switch (block) {
    case AudioBlock::SystemCommon: return addr::kSystemCommon;
    case AudioBlock::InputEfx: return addr::kSystemInputEfx;
    case AudioBlock::Tfx: return addr::kSystemTfx;
    case AudioBlock::SystemController: return addr::kSystemController;
    }
    return {};
}

bool RolandFAPlatform::readAudioBlock(AudioBlock b,int s,QByteArray*d,QString*e){return m_engine&&m_engine->read(audioAddress(b),s,d,e);}
bool RolandFAPlatform::writeAudioBlock(AudioBlock b,const QByteArray&d,QString*e){return m_engine&&m_engine->write(audioAddress(b),d,e);}
bool RolandFAPlatform::writeAudioParameter(AudioBlock b,int o,const QByteArray&d,QString*e){return m_engine&&m_engine->writeParam(addOffset(audioAddress(b),o),d,e);}

Address RolandFAPlatform::address(int part, ToneEngine engine, ToneSection section, int index) const
{
    switch (section) {
    case ToneSection::Mfx: return addr::mfx(part, engine);
    case ToneSection::MfxSwitch: return addr::mfxSwitch(part, engine);
    case ToneSection::SnCommon: return addr::snSynthCommon(part);
    case ToneSection::SnMisc: return addr::snSynthMisc(part);
    case ToneSection::SnPartial: return addr::snSynthPartial(part, index);
    case ToneSection::PcmCommon: return addr::pcmSynthCommon(part);
    case ToneSection::PcmPmt: return addr::pcmSynthPmt(part);
    case ToneSection::PcmCommon2: return addr::pcmSynthCommon2(part);
    case ToneSection::PcmPartial: return addr::pcmSynthPartial(part, index);
    case ToneSection::SnAcousticCommon: return addr::snAcousticCommon(part);
    }
    return {};
}

bool RolandFAPlatform::readToneSection(int part, ToneEngine engine, ToneSection section,
                                       int index, int size, QByteArray *data, QString *error)
{
    return m_engine && m_engine->read(address(part, engine, section, index), size, data, error);
}

bool RolandFAPlatform::writeToneSection(int part, ToneEngine engine, ToneSection section,
                                        int index, const QByteArray &data, QString *error)
{
    return m_engine && m_engine->write(address(part, engine, section, index), data, error);
}

bool RolandFAPlatform::writeToneParameter(int part, ToneEngine engine, ToneSection section,
                                          int index, int offset, const QByteArray &data, QString *error)
{
    return m_engine && m_engine->writeParam(addOffset(address(part, engine, section, index), offset), data, error);
}
