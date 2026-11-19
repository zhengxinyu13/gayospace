// 串口参数模型与校验（TS-SW 第 8 节收发路径；F-01 参数配置的数据载体）。
// 纯值对象，无平台 API、无 UI 依赖——core 分层铁律。
#pragma once

#include <QString>
#include <QStringList>

namespace gayospace::serial {

enum class DataBits { Five, Six, Seven, Eight };
enum class Parity { None, Even, Odd };
enum class StopBits { One, OneAndHalf, Two };
enum class FlowControl { None, Hardware, Software };

// 串口会话参数。默认值 = 8N1 @ 115200，最常见调试配置。
struct SerialParams {
    QString portName;
    qint32 baudRate = 115200;
    DataBits dataBits = DataBits::Eight;
    Parity parity = Parity::None;
    StopBits stopBits = StopBits::One;
    FlowControl flowControl = FlowControl::None;

    // 校验通过返回 true；失败原因逐条见 validationErrors()。
    [[nodiscard]] bool isValid() const { return validationErrors().isEmpty(); }

    // 校验失败原因（用户可读，逐条独立）。
    // 说明：数据位/停止位/校验位组合合法性不在此处判定——组合约束由底层驱动
    // 在 open() 时报错并经错误映射呈现（待核：Qt 文档未给出跨平台组合矩阵，
    // 不臆造规则）；此处只做无歧义的硬校验。
    [[nodiscard]] QStringList validationErrors() const;
};

[[nodiscard]] inline bool operator==(const SerialParams& a, const SerialParams& b)
{
    return a.portName == b.portName && a.baudRate == b.baudRate
        && a.dataBits == b.dataBits && a.parity == b.parity
        && a.stopBits == b.stopBits && a.flowControl == b.flowControl;
}

} // namespace gayospace::serial
