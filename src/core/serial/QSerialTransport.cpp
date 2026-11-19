#include "QSerialTransport.h"

#include <atomic>

namespace gayospace::serial {

namespace {
// 跨线程队列连接（worker → GUI）需要元类型注册；注册幂等，首次构造时完成。
std::atomic_bool g_metaTypesRegistered{false};
void ensureMetaTypes()
{
    bool expected = false;
    if (g_metaTypesRegistered.compare_exchange_strong(expected, true)) {
        qRegisterMetaType<SerialErrorKind>("gayospace::serial::SerialErrorKind");
    }
}
} // namespace

QSerialTransport::QSerialTransport(QObject* parent)
    : ISerialTransport(parent)
    , m_port(new QSerialPort(this)) // 父对象绑定 → 同线程销毁，亲和性成立
{
    ensureMetaTypes();

    connect(m_port, &QSerialPort::readyRead, this, [this]() {
        emit bytesReceived(m_port->readAll());
    });

    // QSerialPort 的错误枚举 → 领域错误；NoError 不上报（避免噪声）。
    connect(m_port, &QSerialPort::errorOccurred, this, [this](QSerialPort::SerialPortError e) {
        if (e == QSerialPort::NoError) {
            return;
        }
        const auto kind = mapQtError(e);
        emit errorOccurred(kind, userMessage(kind));
    });

    // 传输层视角的断开：ResourceError（拔线）或设备消失后端口失效。
    // 其余错误不必然断开（如 Timeout），交由 SerialChannel 判定。
    connect(m_port, &QSerialPort::errorOccurred, this, [this](QSerialPort::SerialPortError e) {
        if (e == QSerialPort::ResourceError) {
            emit closed();
        }
    });
}

bool QSerialTransport::open(const SerialParams& params)
{
    if (m_port->isOpen()) {
        close();
    }

    m_port->setPortName(params.portName);
    m_port->setBaudRate(params.baudRate);

    switch (params.dataBits) {
    case DataBits::Five:  m_port->setDataBits(QSerialPort::Data5); break;
    case DataBits::Six:   m_port->setDataBits(QSerialPort::Data6); break;
    case DataBits::Seven: m_port->setDataBits(QSerialPort::Data7); break;
    case DataBits::Eight: m_port->setDataBits(QSerialPort::Data8); break;
    }

    switch (params.parity) {
    case Parity::None: m_port->setParity(QSerialPort::NoParity); break;
    case Parity::Even: m_port->setParity(QSerialPort::EvenParity); break;
    case Parity::Odd:  m_port->setParity(QSerialPort::OddParity); break;
    }

    switch (params.stopBits) {
    case StopBits::One:        m_port->setStopBits(QSerialPort::OneStop); break;
    case StopBits::OneAndHalf: m_port->setStopBits(QSerialPort::OneAndHalfStop); break;
    case StopBits::Two:        m_port->setStopBits(QSerialPort::TwoStop); break;
    }

    switch (params.flowControl) {
    case FlowControl::None:     m_port->setFlowControl(QSerialPort::NoFlowControl); break;
    case FlowControl::Hardware: m_port->setFlowControl(QSerialPort::HardwareControl); break;
    case FlowControl::Software: m_port->setFlowControl(QSerialPort::SoftwareControl); break;
    }

    if (!m_port->open(QIODevice::ReadWrite)) {
        const auto kind = mapQtError(m_port->error());
        emit errorOccurred(kind, userMessage(kind));
        return false;
    }
    emit opened();
    return true;
}

void QSerialTransport::close()
{
    if (m_port->isOpen()) {
        m_port->close();
        emit closed();
    }
}

bool QSerialTransport::isOpen() const
{
    return m_port->isOpen();
}

qint64 QSerialTransport::writeBytes(const QByteArray& data)
{
    if (data.isEmpty()) {
        return 0;
    }
    return m_port->write(data);
}

} // namespace gayospace::serial
