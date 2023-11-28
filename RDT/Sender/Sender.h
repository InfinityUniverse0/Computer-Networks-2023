#pragma once
#include "common.h"
using namespace std;

#define DEFAULT_SERVER_IP "127.0.0.1" // 默认服务器 IP 地址
#define DEFAULT_SERVER_PORT 8888 // 默认服务器端口号（实则为路由器端口）
#define TIME_OUT_SECS 1 // 超时时间（秒）
#define TIME_OUT_USECS 0 // 超时时间（微秒）

class Sender {
	const char* serverIP; // 服务器 IP
	unsigned short serverPort; // 服务器端口号
	unsigned int seq; // 序列号
	unsigned int ack; // 确认号
	SOCKET senderSocket; // 发送方套接字
	SOCKADDR_IN senderAddr; // 发送方地址
	SOCKADDR_IN recvAddr; // 接收方地址

	// 用于超时重传
	bool acked; // 是否收到 ACK
public:
	Sender();
	~Sender();
	void init(const char* serverIP, unsigned short serverPort); // 初始化
	void connect(); // 连接服务器
	void sendFile(const char* filePath); // 发送文件
	void close(); // 关闭发送端
	void sendPacket(DataPacket_t packet); // 发送数据包
	bool recvACK(); // 接收 ACK
};
