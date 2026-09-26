#include "platform/RolandFantomPlatform.h"
#include "midi/SysexEngine.h"

#include <RtMidi.h>
#include "midi/MidiApi.h"

using namespace roland;

namespace {
constexpr Address kSetup{{0x01,0x00,0x00,0x00}};
constexpr Address kSceneCommon{{0x02,0x00,0x00,0x00}};
constexpr Address kMasterEq{{0x00,0x00,0x06,0x00}};
constexpr QByteArrayView kFantomModelId{"\x00\x00\x00\x5b", 4};
}

RolandFantomPlatform::RolandFantomPlatform(SysexEngine *engine) : m_engine(engine)
{
    if (m_engine)
        m_engine->setModelId(QByteArray(kFantomModelId.data(), kFantomModelId.size()));
}

InstrumentPlatform::DeviceProfile RolandFantomPlatform::profile() const
{
    using A=FeatureAccess; using W=Workspace; using T=ToneEngineCapability;
    return {QStringLiteral("Roland"),m_detectedModel,
            {T::ZCore,T::SuperNaturalAcoustic,T::VirtualToneWheel,T::ExpansionSuperNatural,
             T::ModelTone,T::PcmDrum},16,
            {{W::MidiConnection,A::ReadWrite},{W::DeviceIdentity,A::ReadOnly},
             {W::StudioSets,A::ReadWrite},{W::ToneEditing,A::ReadWrite},
             {W::AudioFx,A::ReadWrite},{W::NotePreview,A::ReadWrite},{W::Library,A::ReadWrite}}};
}

bool RolandFantomPlatform::isConnected() const { return m_engine && m_engine->isOpen(); }
bool RolandFantomPlatform::midiConnectionHealthy() const { return m_engine && m_engine->portsHealthy(); }

bool RolandFantomPlatform::nameIsDawControl(const QString &name)
{
    const auto n=name.toLower();
    return n.contains(QStringLiteral("daw")) || n.contains(QStringLiteral("ctrl"))
           || n.startsWith(QStringLiteral("midiin2")) || n.startsWith(QStringLiteral("midiout2"));
}

bool RolandFantomPlatform::nameLooksLikeFantom0(const QString &name)
{
    if (nameIsDawControl(name)) return false;
    QString n=name.toUpper(); n.remove(QLatin1Char('-')); n.remove(QLatin1Char(' '));
    return n.contains(QStringLiteral("FANTOM06")) || n.contains(QStringLiteral("FANTOM07"))
           || n.contains(QStringLiteral("FANTOM08"));
}

bool RolandFantomPlatform::discoverMidiPorts(QVector<MidiPort> *inputs,QVector<MidiPort> *outputs,QString *error)
{
    if(inputs)inputs->clear(); if(outputs)outputs->clear();
    try {
        RtMidiIn in(faeditor::midiApi()); RtMidiOut out(faeditor::midiApi());
        if(inputs)for(unsigned i=0;i<in.getPortCount();++i){auto n=QString::fromStdString(in.getPortName(i));inputs->push_back({int(i),n,nameIsDawControl(n),nameLooksLikeFantom0(n)});}
        if(outputs)for(unsigned i=0;i<out.getPortCount();++i){auto n=QString::fromStdString(out.getPortName(i));outputs->push_back({int(i),n,nameIsDawControl(n),nameLooksLikeFantom0(n)});}
        return true;
    } catch(const RtMidiError &e){if(error)*error=QString::fromStdString(e.getMessage());return false;}
}

bool RolandFantomPlatform::openMidiConnection(int in,int out,QString *error)
{ return m_engine && m_engine->openPorts(in,out,error); }
void RolandFantomPlatform::closeMidiConnection(){if(m_engine)m_engine->closePorts();}

bool RolandFantomPlatform::detectDevice(quint8 *deviceId,int timeoutMs,QString *error)
{
    if(error)error->clear();
    if(!m_engine||!m_engine->sendIdentityRequest(error))return false;
    quint8 id=0x10;
    if(!m_engine->waitIdentityReply(&id,timeoutMs)){if(error)*error=QStringLiteral("No FANTOM-0 Identity Reply");return false;}
    const QByteArray reply=m_engine->lastIdentityReply();
    // Identity family 5B 03, family number 00 01; byte 10 identifies 06/07/08.
    if(reply.size()<15 || quint8(reply[6])!=0x5b || quint8(reply[7])!=0x03
       || quint8(reply[8])!=0x00 || quint8(reply[9])!=0x01 || quint8(reply[10])>2){
        if(error)*error=QStringLiteral("Connected Roland device is not a FANTOM-06/07/08"); return false;
    }
    m_detectedModel=QStringLiteral("FANTOM-%1").arg(6+quint8(reply[10]));
    m_engine->setDeviceId(id); if(deviceId)*deviceId=id; return true;
}

bool RolandFantomPlatform::sendPreviewNote(int channel,int note,int velocity,bool on,QString *error)
{
    QByteArray m; m.append(char((on?0x90:0x80)|(channel&15)));m.append(char(note&127));m.append(char(velocity&127));
    return m_engine&&m_engine->sendMessage(m,error);
}

bool RolandFantomPlatform::recallPerformance(int msb,int lsb,int program,QString *error)
{
    QByteArray select;select.append(char(msb&127));select.append(char(lsb&127));select.append(char(program&127));
    return m_engine&&m_engine->write(kSetup,select,error);
}

bool RolandFantomPlatform::readTemporaryPerformanceName(QString *name,QString *error)
{
    QByteArray data;if(!m_engine||!m_engine->read(kSceneCommon,16,&data,error,2500))return false;
    const int nul=data.indexOf('\0');if(nul>=0)data.truncate(nul);if(name)*name=QString::fromLatin1(data).trimmed();return true;
}

Address RolandFantomPlatform::sceneAddress(StudioBlock block,int index)
{
    switch(block){
    case StudioBlock::Common:return kSceneCommon;
    case StudioBlock::Chorus:return {{0x02,0x00,0x02,0x00}};
    case StudioBlock::Reverb:return {{0x02,0x00,0x03,0x00}};
    case StudioBlock::Ifx:return {{0x02,0x00,quint8(index==0?0x04:0x06),0x00}};
    case StudioBlock::Part:return {{0x02,0x00,quint8(0x10+index),0x00}};
    case StudioBlock::PartEq:return {{0x02,0x00,quint8(0x20+index),0x00}};
    case StudioBlock::Zone:return {{0x02,0x00,quint8(0x30+index),0x00}};
    case StudioBlock::Controller:return {{0x02,0x00,0x40,0x00}};
    case StudioBlock::MasterComp:return {{0x00,0x00,0x04,0x00}};
    case StudioBlock::Midi:return kSetup;
    case StudioBlock::PadCommon: case StudioBlock::Pad:return {};
    }
    return {};
}

bool RolandFantomPlatform::readStudioBlock(StudioBlock b,int i,int s,QByteArray*d,QString*e){auto a=sceneAddress(b,i);if(a==Address{}){if(e)*e=QStringLiteral("Block unavailable on FANTOM-0");return false;}return m_engine&&m_engine->read(a,s,d,e);}
bool RolandFantomPlatform::writeStudioBlock(StudioBlock b,int i,const QByteArray&d,QString*e){auto a=sceneAddress(b,i);if(a==Address{}){if(e)*e=QStringLiteral("Block unavailable on FANTOM-0");return false;}return m_engine&&m_engine->write(a,d,e);}
bool RolandFantomPlatform::writeStudioParameter(StudioBlock b,int i,int o,const QByteArray&d,QString*e){return m_engine&&m_engine->writeParam(addOffset(sceneAddress(b,i),o),d,e);}
bool RolandFantomPlatform::readMasterEq(QByteArray*d,QString*e){return m_engine&&m_engine->read(kMasterEq,9,d,e);}
bool RolandFantomPlatform::writeMasterEq(const QByteArray&d,QString*e){return m_engine&&m_engine->write(kMasterEq,d,e);}

Address RolandFantomPlatform::audioAddress(AudioBlock b) const
{
    switch(b){case AudioBlock::SystemCommon:return {{0x00,0x00,0x00,0x00}};case AudioBlock::SystemController:return {{0x00,0x00,0x02,0x00}};case AudioBlock::Tfx:return {{0x00,0x00,0x10,0x00}};case AudioBlock::InputEfx:return {{0x00,0x00,0x12,0x00}};}return {};
}
bool RolandFantomPlatform::readAudioBlock(AudioBlock b,int s,QByteArray*d,QString*e){return m_engine&&m_engine->read(audioAddress(b),s,d,e);}
bool RolandFantomPlatform::writeAudioBlock(AudioBlock b,const QByteArray&d,QString*e){return m_engine&&m_engine->write(audioAddress(b),d,e);}
bool RolandFantomPlatform::writeAudioParameter(AudioBlock b,int o,const QByteArray&d,QString*e){return m_engine&&m_engine->writeParam(addOffset(audioAddress(b),o),d,e);}

Address RolandFantomPlatform::zCoreAddress(int zone,ToneSection section,int index)
{
    Address base{{0x02,quint8(0x10+zone),0x00,0x00}};
    switch(section){
    case ToneSection::PcmCommon:return base;
    case ToneSection::Mfx:return addOffset(base,addressToU32({{0x00,0x00,0x01,0x00}}));
    case ToneSection::PcmPmt:return addOffset(base,addressToU32({{0x00,0x00,0x10,0x00}}));
    case ToneSection::PcmPartial:return addOffset(base,addressToU32({{0x00,0x00,quint8(0x20+index),0x00}}));
    default:return base;
    }
}
bool RolandFantomPlatform::readToneSection(int p,ToneEngine,ToneSection s,int i,int n,QByteArray*d,QString*e){return m_engine&&m_engine->read(zCoreAddress(p,s,i),n,d,e);}
bool RolandFantomPlatform::writeToneSection(int p,ToneEngine,ToneSection s,int i,const QByteArray&d,QString*e){return m_engine&&m_engine->write(zCoreAddress(p,s,i),d,e);}
bool RolandFantomPlatform::writeToneParameter(int p,ToneEngine,ToneSection s,int i,int o,const QByteArray&d,QString*e){return m_engine&&m_engine->writeParam(addOffset(zCoreAddress(p,s,i),o),d,e);}
