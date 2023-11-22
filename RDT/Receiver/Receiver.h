﻿#pragma once
#include "common.h"
using namespace std;

#define DEFAULT_IP "127.0.0.1" // 默认服务器 IP 地址
#define DEFAULT_PORT 8889 // 默认服务器端口号

class Receiver {
	const char* serverIP; // 服务器 IP
	unsigned short serverPort; // 服务器端口号
	unsigned int seq; // 序列号
	unsigned int ack; // 确认号
	SOCKET recvSocket; // 接收套接字
	SOCKADDR_IN recvAddr; // 接收方地址
	SOCKADDR_IN senderAddr; // 发送方地址
public:
	Receiver();
	~Receiver();
	void init(const char* serverIP, unsigned short serverPort); // 初始化
	void start(); // 开始接收
	void recvFile(const char* filePath); // 接收文件
	void close(); // 关闭接收端
};
