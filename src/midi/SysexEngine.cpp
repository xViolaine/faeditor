#include "midi/SysexEngine.h"
#include "midi/RolandChecksum.h"

#include <RtMidi.h>
#include "midi/MidiApi.h"
#include <QThread>
#include <QDateTime>
#include <QDebug>
#include <atomic>
#include <string>

#if defined(Q_OS_IOS)
#include <CoreMIDI/CoreMIDI.h>
#endif

SysexEngine::SysexEngine(QObject *parent)
    : QObject(parent)
{
}

SysexEngine::~SysexEngine()
{
    closePorts();
}

bool SysexEngine::openPorts(int inIndex, int outIndex, QString *error)
{
    closePorts();
    try {
        static std::atomic<int> clientSerial{0};
        const auto clientName = std::string("FAEditor-") + std::to_string(++clientSerial);
        m_in = std::make_unique<RtMidiIn>(faeditor::midiApi(), clientName);
        m_out = std::make_unique<RtMidiOut>(faeditor::midiApi(), clientName + "-out");
        m_in->ignoreTypes(false, false, false);
        m_in->setCallback(&SysexEngine::rtMidiCallback, this);
        m_in->openPort(static_cast<unsigned int>(inIndex), "FAEditor In");
        m_out->openPort(static_cast<unsigned int>(outIndex), "FAEditor Out");
        m_open = true;
        return true;
    } catch (const RtMidiError &e) {
        if (error)
            *error = QString::fromStdString(e.getMessage());
        closePorts();
        return false;
    }
}

void SysexEngine::resetHostMidi()
{
    closePorts();
#if defined(Q_OS_IOS)
    MIDIRestart();
#endif
}

void SysexEngine::closePorts()
{
    m_open = false;
    if (m_in) {
        try {
            m_in->cancelCallback();
            if (m_in->isPortOpen())
                m_in->closePort();
        } catch (...) {
        }
        m_in.reset();
    }
    if (m_out) {
        try {
            if (m_out->isPortOpen())
                m_out->closePort();
        } catch (...) {
        }
        m_out.reset();
    }
}

bool SysexEngine::isOpen() const
{
    return m_open.load();
}

bool SysexEngine::portsHealthy() const
{
    if (!m_open.load() || !m_in || !m_out)
        return false;
    try {
        return m_in->isPortOpen() && m_out->isPortOpen();
    } catch (...) {
        return false;
    }
}

void SysexEngine::setDeviceId(quint8 id)
{
    m_deviceId = id;
}

void SysexEngine::setModelId(const QByteArray &modelId)
{
    if (!modelId.isEmpty())
        m_modelId = modelId;
}

void SysexEngine::rtMidiCallback(double deltaTime, std::vector<unsigned char> *message, void *userData)
{
    auto *self = static_cast<SysexEngine *>(userData);
    if (!self || !self->m_open.load())
        return;
    self->onMidiMessage(deltaTime, message);
}

void SysexEngine::onMidiMessage(double, std::vector<unsigned char> *message)
{
    if (!m_open.load() || !message || message->empty())
        return;

    QByteArray raw(reinterpret_cast<const char *>(message->data()),
                   static_cast<int>(message->size()));
    emit sysexReceived(raw);

    if (raw.size() < 3 || static_cast<quint8>(raw[0]) != 0xF0)
        return;

    // Universal Identity Reply: F0 7E dev 06 02 41 ...
    if (raw.size() >= 15
        && static_cast<quint8>(raw[1]) == 0x7E
        && static_cast<quint8>(raw[3]) == 0x06
        && static_cast<quint8>(raw[4]) == 0x02
        && static_cast<quint8>(raw[5]) == 0x41) {
        QMutexLocker lock(&m_mutex);
        m_identityDeviceId = static_cast<quint8>(raw[2]);
        m_identityReply = raw;
        m_gotIdentity = true;
        m_cond.wakeAll();
        return;
    }

    // DT1: F0 41 dev <variable model id> 12 aa bb cc dd data... sum F7
    const int commandOffset = 3 + m_modelId.size();
    const int addressOffset = commandOffset + 1;
    const int dataOffset = addressOffset + 4;
    if (raw.size() >= dataOffset + 3
        && static_cast<quint8>(raw[1]) == roland::kManufacturerId
        && raw.mid(3, m_modelId.size()) == m_modelId
        && static_cast<quint8>(raw[commandOffset]) == roland::kCmdDt1) {
        const auto device = static_cast<quint8>(raw[2]);
        Q_UNUSED(device);
        roland::Address address{{
            static_cast<quint8>(raw[addressOffset]),
            static_cast<quint8>(raw[addressOffset + 1]),
            static_cast<quint8>(raw[addressOffset + 2]),
            static_cast<quint8>(raw[addressOffset + 3])
        }};
        const int dataLen = raw.size() - dataOffset - 2;
        if (dataLen <= 0)
            return;
        QByteArray data = raw.mid(dataOffset, dataLen);

        QMutexLocker lock(&m_mutex);
        if (m_pendingDt1Data.isEmpty()) {
            m_pendingAddress = address;
            m_pendingDt1Data = data;
        } else {
            m_pendingDt1Data.append(data);
        }
        m_gotDt1 = true;
        m_cond.wakeAll();
    }
}

QByteArray SysexEngine::buildRq1(const roland::Address &address, int size) const
{
    const auto sz = roland::sizeFromInt(size);
    QByteArray body;
    body.append(reinterpret_cast<const char *>(address.data()), 4);
    body.append(reinterpret_cast<const char *>(sz.data()), 4);
    const quint8 sum = roland::computeChecksum(body);

    QByteArray msg;
    msg.append(char(0xF0));
    msg.append(char(roland::kManufacturerId));
    msg.append(char(m_deviceId));
    msg.append(m_modelId);
    msg.append(char(roland::kCmdRq1));
    msg.append(body);
    msg.append(char(sum));
    msg.append(char(0xF7));
    return msg;
}

QByteArray SysexEngine::buildDt1(const roland::Address &address, const QByteArray &data) const
{
    QByteArray body;
    body.append(reinterpret_cast<const char *>(address.data()), 4);
    body.append(data);
    const quint8 sum = roland::computeChecksum(body);

    QByteArray msg;
    msg.append(char(0xF0));
    msg.append(char(roland::kManufacturerId));
    msg.append(char(m_deviceId));
    msg.append(m_modelId);
    msg.append(char(roland::kCmdDt1));
    msg.append(body);
    msg.append(char(sum));
    msg.append(char(0xF7));
    return msg;
}

bool SysexEngine::sendRaw(const QByteArray &msg, QString *error)
{
    if (!m_out || !m_open) {
        if (error)
            *error = QStringLiteral("MIDI output not open");
        return false;
    }
    try {
        std::vector<unsigned char> buf(msg.begin(), msg.end());
        m_out->sendMessage(&buf);
        return true;
    } catch (const RtMidiError &e) {
        if (error)
            *error = QString::fromStdString(e.getMessage());
        return false;
    }
}

bool SysexEngine::read(const roland::Address &address, int size, QByteArray *outData, QString *error, int timeoutMs)
{
    if (!outData)
        return false;

    {
        QMutexLocker lock(&m_mutex);
        m_gotDt1 = false;
        m_pendingDt1Data.clear();
        m_pendingAddress = address;
    }

    QString err;
    if (!sendRaw(buildRq1(address, size), &err)) {
        if (error)
            *error = err;
        return false;
    }

    QByteArray collected;
    const qint64 deadline = QDateTime::currentMSecsSinceEpoch() + timeoutMs;
    while (QDateTime::currentMSecsSinceEpoch() < deadline) {
        QMutexLocker lock(&m_mutex);
        if (!m_gotDt1) {
            m_cond.wait(&m_mutex, 50);
        }
        if (m_gotDt1) {
            collected.append(m_pendingDt1Data);
            m_pendingDt1Data.clear();
            m_gotDt1 = false;
            // If we have enough, stop; else keep waiting for more packets briefly
            if (collected.size() >= size)
                break;
            // wait a bit more for continuation packets
            m_cond.wait(&m_mutex, roland::kInterPacketMs + 30);
            if (m_gotDt1) {
                collected.append(m_pendingDt1Data);
                m_pendingDt1Data.clear();
                m_gotDt1 = false;
                if (collected.size() >= size)
                    break;
            } else if (collected.size() > 0) {
                // some blocks return exact documented size; accept what we got
                break;
            }
        }
    }

    if (collected.isEmpty()) {
        if (error)
            *error = QStringLiteral("SysEx read timeout");
        return false;
    }

    *outData = collected.left(size);
    return true;
}

bool SysexEngine::write(const roland::Address &address, const QByteArray &data, QString *error)
{
    int offset = 0;
    while (offset < data.size()) {
        const int chunk = qMin(roland::kMaxPacketData, data.size() - offset);
        const auto chunkAddr = roland::addOffset(address, static_cast<quint32>(offset));
        // Note: Roland address offset for multi-byte is not always linear byte offset;
        // for contiguous parameter maps within a block, byte offset in LSB works for small chunks.
        // For large transfers we write at starting address of each chunk using 7-bit address arithmetic.
        Q_UNUSED(chunkAddr);
        roland::Address a = address;
        // Advance address by offset using 7-bit fields
        quint32 v = roland::addressToU32(address) + static_cast<quint32>(offset);
        a = roland::u32ToAddress(v);

        QString err;
        if (!sendRaw(buildDt1(a, data.mid(offset, chunk)), &err)) {
            if (error)
                *error = err;
            return false;
        }
        offset += chunk;
        if (offset < data.size())
            QThread::msleep(static_cast<unsigned long>(roland::kInterPacketMs));
    }
    return true;
}

bool SysexEngine::writeParam(const roland::Address &address, const QByteArray &data, QString *error)
{
    return write(address, data, error);
}

bool SysexEngine::sendMessage(const QByteArray &message, QString *error)
{
    return sendRaw(message, error);
}

bool SysexEngine::sendIdentityRequest(QString *error)
{
    {
        QMutexLocker lock(&m_mutex);
        m_gotIdentity = false;
        m_identityReply.clear();
    }
    // F0 7E 7F 06 01 F7
    QByteArray msg;
    msg.append(char(0xF0));
    msg.append(char(0x7E));
    msg.append(char(0x7F));
    msg.append(char(0x06));
    msg.append(char(0x01));
    msg.append(char(0xF7));
    return sendRaw(msg, error);
}

QByteArray SysexEngine::lastIdentityReply() const
{
    QMutexLocker lock(&m_mutex);
    return m_identityReply;
}

bool SysexEngine::waitIdentityReply(quint8 *deviceIdOut, int timeoutMs)
{
    const qint64 deadline = QDateTime::currentMSecsSinceEpoch() + timeoutMs;
    while (QDateTime::currentMSecsSinceEpoch() < deadline) {
        QMutexLocker lock(&m_mutex);
        if (m_gotIdentity) {
            if (deviceIdOut)
                *deviceIdOut = m_identityDeviceId;
            return true;
        }
        m_cond.wait(&m_mutex, 50);
    }
    return false;
}
