// Common Header
#pragma once

#include <winsock2.h>
#include <ws2tcpip.h>
#include <string.h>
#include <filesystem> // C++17
#include <fstream>
#include <iostream>
#include <windows.h>
#include <chrono>
#include <format> // C++20
#include <iomanip>
#include <sstream>

#pragma comment(lib, "ws2_32.lib") // 链接 ws2_32.lib 库文件到此项目中

/*
Data Packet Format:
+-------------------------------------+
|          Source IP Address          |
+-------------------------------------+
|        Destination IP Address       |
+------------------+------------------+
|   Source Port    | Destination Port |
+------------------+------------------+
|        Sequence Number(seq)         |
+-------------------------------------+
|      Acknowledgement Number(ACK)    |
+-------------------------------------+
|    0   | BEG | END | SYN | FIN | ACK|
+------------------+------------------+
|     Checksum     |    Data Length   |
+------------------+------------------+
|                Data                 |
+-------------------------------------+

Each line contains 32 bits(4 Bytes).
BEG, END, SYN, FIN, ACK each contains 1 bit.
BEG: Begin of File
END: End of File
*/

#define MAX_DATA_LENGTH 10240 // 10KiB

#define BEG (0x10)
#define END (0x8)
#define SYN (0x4)
#define FIN (0x2)
#define ACK (0x1)

struct DataPacket {
    unsigned int srcIP;
    unsigned int dstIP;
    unsigned short srcPort;
    unsigned short dstPort;
    unsigned int seq;
    unsigned int ack;
    unsigned int flags; // SYN: 0100, FIN: 0010, ACK: 0001
    unsigned short checksum;
    unsigned short dataLength;
    char data[MAX_DATA_LENGTH];
};

#define PKT_HEADER_SIZE (sizeof(struct DataPacket) - sizeof(char) * MAX_DATA_LENGTH)
typedef struct DataPacket* DataPacket_t;

// 判断消息类型
bool isBEG(DataPacket_t packet);
bool isEND(DataPacket_t packet);
bool isSYN(DataPacket_t packet);
bool isFIN(DataPacket_t packet);
bool isACK(DataPacket_t packet);

// 设置标志位
void setBEG(DataPacket_t packet);
void setEND(DataPacket_t packet);
void setSYN(DataPacket_t packet);
void setFIN(DataPacket_t packet);
void setACK(DataPacket_t packet);

// 数据打包
DataPacket_t make_packet(
    unsigned int srcIP, unsigned int dstIP,
    unsigned short srcPort, unsigned short dstPort,
    unsigned int seq, unsigned int ack,
    unsigned int flags,
    const char* data, int dataLen
);
// 数据解包
bool parse_packet(char* dataPacket, int packetLen, DataPacket_t& packet);

// 差错检验
unsigned short cal_checksum(DataPacket_t packet);

// Error Handler
void die(const char* msg);


// Log
enum LogType {
    LOG_TYPE_INFO,
    LOG_TYPE_ERROR,
    LOG_TYPE_PKT,
};

void log(LogType type, const std::string& msg, DataPacket_t packet = nullptr, bool showTime = true);
