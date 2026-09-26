#include "platform/AutoDetectRolandPlatform.h"
#include "midi/SysexEngine.h"
#include <RtMidi.h>
#include "midi/MidiApi.h"

AutoDetectRolandPlatform::AutoDetectRolandPlatform(SysexEngine *e):m_engine(e),m_fa(e),m_fantom(e){select(Family::FA);}
InstrumentPlatform *AutoDetectRolandPlatform::active(){return m_family==Family::Fantom0?static_cast<InstrumentPlatform*>(&m_fantom):static_cast<InstrumentPlatform*>(&m_fa);}
const InstrumentPlatform *AutoDetectRolandPlatform::active()const{return m_family==Family::Fantom0?static_cast<const InstrumentPlatform*>(&m_fantom):static_cast<const InstrumentPlatform*>(&m_fa);}
void AutoDetectRolandPlatform::select(Family f){m_family=f;if(m_engine)m_engine->setModelId(f==Family::Fantom0?QByteArray::fromHex("0000005b"):QByteArray::fromHex("000077"));}
InstrumentPlatform::DeviceProfile AutoDetectRolandPlatform::profile()const{return active()->profile();}
bool AutoDetectRolandPlatform::isConnected()const{return m_engine&&m_engine->isOpen();}
bool AutoDetectRolandPlatform::midiConnectionHealthy()const{return m_engine&&m_engine->portsHealthy();}

bool AutoDetectRolandPlatform::discoverMidiPorts(QVector<MidiPort>*ins,QVector<MidiPort>*outs,QString*error)
{
    m_lastInputs.clear();m_lastOutputs.clear();
    try{RtMidiIn in(faeditor::midiApi());RtMidiOut out(faeditor::midiApi());
        auto make=[](int i,const QString&n){auto u=n.toUpper();const bool daw=u.contains("DAW")||u.contains("CTRL")||u.startsWith("MIDIIN2")||u.startsWith("MIDIOUT2");const bool match=!daw&&((u.contains("ROLAND")&&u.contains("FA"))||u.contains("FA-06")||u.contains("FA-07")||u.contains("FA-08")||u.contains("FANTOM-06")||u.contains("FANTOM-07")||u.contains("FANTOM-08"));return MidiPort{i,n,daw,match};};
        for(unsigned i=0;i<in.getPortCount();++i)m_lastInputs.push_back(make(int(i),QString::fromStdString(in.getPortName(i))));
        for(unsigned i=0;i<out.getPortCount();++i)m_lastOutputs.push_back(make(int(i),QString::fromStdString(out.getPortName(i))));
        if(ins)*ins=m_lastInputs;if(outs)*outs=m_lastOutputs;return true;
    }catch(const RtMidiError&e){if(error)*error=QString::fromStdString(e.getMessage());return false;}
}

bool AutoDetectRolandPlatform::openMidiConnection(int in,int out,QString*error)
{
    const QString name=in>=0&&in<m_lastInputs.size()?m_lastInputs[in].name:QString();
    select(name.contains(QStringLiteral("FANTOM"),Qt::CaseInsensitive)?Family::Fantom0:Family::FA);
    return m_engine&&m_engine->openPorts(in,out,error);
}
void AutoDetectRolandPlatform::closeMidiConnection(){if(m_engine){m_engine->closePorts();m_engine->setModelId(QByteArray::fromHex("000077"));}m_family=Family::Unknown;}
void AutoDetectRolandPlatform::resetMidiHost(){closeMidiConnection();if(m_engine)m_engine->resetHostMidi();}

bool AutoDetectRolandPlatform::detectDevice(quint8*deviceId,int timeoutMs,QString*error)
{
    if(error)error->clear();if(!m_engine||!m_engine->sendIdentityRequest(error))return false;quint8 id=0x10;
    if(!m_engine->waitIdentityReply(&id,timeoutMs)){if(error)*error=QStringLiteral("No supported Roland Identity Reply");return false;}
    const auto r=m_engine->lastIdentityReply();
    if(r.size()>=15&&quint8(r[6])==0x5b&&quint8(r[7])==0x03&&quint8(r[8])==0&&quint8(r[9])==1&&quint8(r[10])<=2)select(Family::Fantom0);
    else select(Family::FA);
    m_engine->setDeviceId(id);if(deviceId)*deviceId=id;return true;
}
