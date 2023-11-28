#include "common.h"

unsigned short cal_checksum(DataPacket_t packet) {
    /*
     * 计算校验和
     * 返回值暂时不取反
     */
    unsigned int sum = 0;
    int count = (packet->dataLength + PKT_HEADER_SIZE) / 2;
    unsigned short * buf = (unsigned short *)packet;
    while (count--) {
        sum += *buf;
        if (sum & 0x10000) { // 溢出
            sum &= 0xFFFF;
            sum++;
        }
        buf++;
    }
    if ((packet->dataLength + PKT_HEADER_SIZE) & 0x1) { // 须补齐 16 位
        // (packet->dataLength + PKT_HEADER_SIZE) % 2 == 1
        packet->data[packet->dataLength] = 0;
        sum += *buf;
        // sum += (*buf) & 0xFF00;
        if (sum & 0x10000) { // 溢出
            sum &= 0xFFFF;
            sum++;
        }
    }
    // 返回值不取反
    return (sum & 0xFFFF);
}

DataPacket_t make_packet(
    unsigned int srcIP, unsigned int dstIP,
    unsigned short srcPort, unsigned short dstPort,
    unsigned int seq, unsigned int ack,
    unsigned int flags,
    const char* data, int dataLen
) {
    /*
     * 数据打包
     * Args:
     *      @srcIP:   Source IP Address
     *      @dstIP:   Destination IP Address
     *      @srcPort: Source Port
     *      @dstPort: Destination Port
     *      @seq:     Sequence Number
     *      @ack:     Acknowledgement Number
     *      @flags:   Flags
     *      @data:    data payload
     *      @dataLen: data payload length
     * Return:
     *     data packet
     */
    DataPacket_t packet = new DataPacket;
    packet->srcIP = srcIP;
    packet->dstIP = dstIP;
    packet->srcPort = srcPort;
    packet->dstPort = dstPort;
    packet->seq = seq;
    packet->ack = ack;
    packet->flags = flags;
    packet->checksum = 0; // 校验和清零
    packet->dataLength = dataLen; // 数据长度
    if (memcpy_s(packet->data, sizeof(packet->data), data, dataLen))
        die("Data Buffer Overflow");
    packet->checksum = ~(cal_checksum(packet)); // 校验和取反
    return packet;
}

bool parse_packet(char* dataPacket, int packetLen, DataPacket_t& packet) {
    /*
     * 数据解包
     * Args:
     *      @dataPacket: data packet(Header and data payload)
     *      @packetLen: data packet length (including Header)
     *      @packet: 解析出的数据包
     * Return:
     *      return true if packet is NOT corrupt
     */
    packet = (DataPacket_t)dataPacket;
    if (cal_checksum(packet) == 0xFFFF && packetLen == PKT_HEADER_SIZE + packet->dataLength)
        return true;
    return false;
}

void send_packet(SOCKET sock, SOCKADDR_IN addr, DataPacket_t packet) {
    /*
     * 发送数据包
     * Args:
     *      @sock: 套接字
     *      @addr: 目标地址
     *      @packet: 数据包
     */
    int packetLen = PKT_HEADER_SIZE + packet->dataLength;
    if (sendto(sock, (char*)packet, packetLen, 0, (SOCKADDR*)&addr, sizeof(addr)) == SOCKET_ERROR) {
        log(LOG_TYPE_ERROR, std::format("sendto() failed with error: {}", WSAGetLastError()));
        closesocket(sock);
        WSACleanup();
        exit(1);
    }
    log(LOG_TYPE_PKT, "[Send]", packet);
}

int recv_packet(SOCKET sock, SOCKADDR_IN& addr, DataPacket_t& packet) {
    /*
     * 接收数据包
     * Args:
     *      @sock: 套接字
     *      @addr: 发送方地址
     *      @packet: 解析出的数据包
     * Return:
     *      return packet length if packet is NOT corrupt
     *      return -1 if packet is corrupt
     */
    int addrLen = sizeof(addr);
    char* dataPacket = new char[sizeof(DataPacket)];
    // char dataPacket[sizeof(DataPacket)];
    int packetLen = recvfrom(sock, dataPacket, sizeof(DataPacket), 0, (SOCKADDR*)&addr, &addrLen);
    if (packetLen == SOCKET_ERROR) {
        log(LOG_TYPE_ERROR, std::format("recvfrom() failed with error: {}", WSAGetLastError()));
        closesocket(sock);
        WSACleanup();
        exit(1);
    }
    if (packetLen == 0) {
        log(LOG_TYPE_INFO, "recvfrom() failed: connection closed");
        closesocket(sock);
        WSACleanup();
        exit(1);
    }
    bool notCorrupt = parse_packet(dataPacket, packetLen, packet);
    log(LOG_TYPE_PKT, "[Recv]", packet);
    if (notCorrupt)
        return packetLen;
    return -1;
}

// 判断消息类型
bool isBEG(DataPacket_t packet) {
    return (packet->flags & BEG);
}
bool isEND(DataPacket_t packet) {
    return (packet->flags & END);
}
bool isSYN(DataPacket_t packet) {
    return (packet->flags & SYN);
}
bool isFIN(DataPacket_t packet) {
    return (packet->flags & FIN);
}
bool isACK(DataPacket_t packet) {
    return (packet->flags & ACK);
}

// 设置标志位
void setBEG(DataPacket_t packet) {
    packet->flags |= BEG;
}
void setEND(DataPacket_t packet) {
    packet->flags |= END;
}
void setSYN(DataPacket_t packet) {
    packet->flags |= SYN;
}
void setFIN(DataPacket_t packet) {
    packet->flags |= FIN;
}
void setACK(DataPacket_t packet) {
    packet->flags |= ACK;
}

// Error Handler
void die(const char* msg) {
    perror(msg);
    exit(1);
}

// Log
void log(LogType type, const std::string& msg, DataPacket_t packet, bool showTime) {
    if (showTime) {
        // 获取并输出当前时间
        std::chrono::system_clock::time_point now = std::chrono::system_clock::now();
        std::time_t now_c = std::chrono::system_clock::to_time_t(now);
        std::tm localTime;
        localtime_s(&localTime, &now_c);
        // 格式化输出时间，并转为 string
        std::stringstream timeSStr;
        timeSStr << std::put_time(&localTime, "[%Y-%m-%d %H:%M:%S]");
        std::cout << timeSStr.str() << std::endl;
    }

    // 输出日志
    HANDLE hConsole = GetStdHandle(STD_OUTPUT_HANDLE); // 命令行句柄
    switch (type) {
    case LOG_TYPE_INFO:
        SetConsoleTextAttribute(hConsole, FOREGROUND_INTENSITY); // White
        std::cout << "[INFO] ";
        // Reset to default color
	    SetConsoleTextAttribute(hConsole, FOREGROUND_RED | FOREGROUND_GREEN | FOREGROUND_BLUE);
        std::cout << msg << std::endl;
        break;
    case LOG_TYPE_ERROR:
        SetConsoleTextAttribute(hConsole, FOREGROUND_RED); // Red
        std::cout << "[ERROR] ";
        // Reset to default color
	    SetConsoleTextAttribute(hConsole, FOREGROUND_RED | FOREGROUND_GREEN | FOREGROUND_BLUE);
        std::cout << msg << std::endl;
        break;
    case LOG_TYPE_PKT:
        SetConsoleTextAttribute(hConsole, FOREGROUND_GREEN); // Green
        std::cout << "[PKT] " << msg << std::endl;
        // Reset to default color
        SetConsoleTextAttribute(hConsole, FOREGROUND_RED | FOREGROUND_GREEN | FOREGROUND_BLUE);
        // std::cout << "    srcIP: " << packet->srcIP << std::endl;
        // std::cout << "    dstIP: " << packet->dstIP << std::endl;
        std::cout << "    srcPort: " << packet->srcPort << std::endl;
        std::cout << "    dstPort: " << packet->dstPort << std::endl;
        std::cout << "    seq: " << packet->seq << std::endl;
        std::cout << "    ack: " << packet->ack << std::endl;
        std::cout << "    flags: " << packet->flags << std::endl;
        std::cout << "    checksum: " << packet->checksum << std::endl;
        std::cout << "    dataLength: " << packet->dataLength << std::endl;
        // std::cout << "    data: " << packet->data << std::endl;
        std::cout << std::endl;
        break;
    default:
        break;
    }
}
