﻿#pragma once
#include "common.h"
#include <thread>
#include <atomic>
#include <mutex>
#include <functional>  // for std::bind
using namespace std;

#define DEFAULT_SERVER_IP "127.0.0.1" // 默认服务器 IP 地址
#define DEFAULT_SERVER_PORT 8888 // 默认服务器端口号（实则为路由器端口）
#define TIME_OUT_SECS 1 // 超时时间（秒）
#define TIME_OUT_USECS 0 // 超时时间（微秒）

// #define DEFAULT_WINDOW_SIZE 10 // 默认窗口大小

class Sender {
	const char* serverIP; // 服务器 IP
	unsigned short serverPort; // 服务器端口号
	unsigned int seq; // 序列号
	unsigned int ack; // 确认号
	SOCKET senderSocket; // 发送方套接字
	SOCKADDR_IN senderAddr; // 发送方地址
	SOCKADDR_IN recvAddr; // 接收方地址

	unsigned int windowSize; // 窗口大小
	atomic<unsigned int> base; // 窗口基序号

	deque<DataPacket_t> window; // 窗口（发送缓冲区） [base, base+windowSize)
	deque<bool> ackWindow; // 对应窗口位置是否收到 ACK
	mutex windowMutex; // 用于保护 window 的互斥锁

	// 计时器
	deque<thread*> timerThreads; // 计时器线程双端队列

	// 接收数据包线程
	thread recvThread;
	atomic<bool> recvRunning; // 接收数据包线程是否运行

public:
	Sender();
	~Sender();
	void init(const char* serverIP, unsigned short serverPort, unsigned int windowSize = DEFAULT_WINDOW_SIZE); // 初始化
	void connect(); // 连接服务器
	void sendFile(const char* filePath); // 发送文件
	void close(); // 关闭发送端
	void sendPacket(DataPacket_t packet); // 发送数据包
	bool recvACK(); // 接收 ACK
	void timerThreadFunc(unsigned int packet_seq); // 计时器线程函数
	void recvThreadFunc(); // 接收数据包线程函数
};
