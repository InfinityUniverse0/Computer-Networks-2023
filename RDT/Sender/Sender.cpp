#include "Sender.h"

Sender::Sender() {
	// 初始化 WinSock
	WORD wVersionRequested = MAKEWORD(2, 2); // 请求 2.2 版本的 WinSock 库
	WSADATA wsaData;
	int err = WSAStartup(wVersionRequested, &wsaData);
	if (err) {
		log(LogType::LOG_TYPE_ERROR, std::format("WSAStartup failed with error: {}", err));
	}
	// 检查 WinSock 版本号
	if (wsaData.wVersion != wVersionRequested) {
		log(LogType::LOG_TYPE_ERROR, "WinSock Version Check Fail!");
		WSACleanup();
		exit(1);
	}
}

Sender::~Sender() {
	// close socket
	closesocket(senderSocket);
	log(LogType::LOG_TYPE_INFO, "Sender Socket Closed!");
	// close WinSock
	WSACleanup();
	log(LogType::LOG_TYPE_INFO, "WinSock Closed!");
}

void Sender::init(const char* serverIP, unsigned short serverPort, unsigned int windowSize) {
	this->serverIP = serverIP;
	this->serverPort = serverPort;
	this->windowSize = windowSize;

	// Create socket
	senderSocket = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
	if (senderSocket == INVALID_SOCKET) {
		log(LogType::LOG_TYPE_ERROR, std::format("socket() failed with error: {}", WSAGetLastError()));
		WSACleanup();
		exit(1);
	}

	recvAddr.sin_family = AF_INET;
	recvAddr.sin_port = htons(serverPort);
	inet_pton(AF_INET, serverIP, &recvAddr.sin_addr);
	// recvAddr.sin_addr.s_addr = inet_addr(serverIP);

	//// Set timeout
	//struct timeval tv;
	//tv.tv_sec = 0;
	//tv.tv_usec = 100000;
	//setsockopt(senderSocket, SOL_SOCKET, SO_RCVTIMEO, (const char*)&tv, sizeof tv);

	// Get senderAddr
	int addrLen = sizeof(senderAddr);
	getsockname(senderSocket, (SOCKADDR*)&senderAddr, &addrLen);

	// Log
	char ip[20];
	if (inet_ntop(AF_INET, &(senderAddr.sin_addr), ip, sizeof(ip))) {
		log(LogType::LOG_TYPE_INFO, std::format("Sender IP: {}", ip));
	}
	else {
		log(LogType::LOG_TYPE_ERROR, std::format("inet_ntop failed with error: {}", WSAGetLastError()));
	}
	// cout << "Sender IP: " << inet_ntoa(senderAddr.sin_addr) << endl;
	log(LogType::LOG_TYPE_INFO, std::format("Sender Port: {}", ntohs(senderAddr.sin_port)));
	if (inet_ntop(AF_INET, &(recvAddr.sin_addr), ip, sizeof(ip))) {
		log(LogType::LOG_TYPE_INFO, std::format("Receiver IP: {}", ip));
	}
	else {
		log(LogType::LOG_TYPE_ERROR, std::format("inet_ntop failed with error: {}", WSAGetLastError()));
	}
	// cout << "Receiver IP: " << inet_ntoa(recvAddr.sin_addr) << endl;
	log(LogType::LOG_TYPE_INFO, std::format("Receiver Port: {}", ntohs(recvAddr.sin_port)));

	log(LogType::LOG_TYPE_INFO, "Sliding Window Protocol");
	log(LogType::LOG_TYPE_INFO, std::format("Window Size: {}", windowSize));
}

void Sender::connect() {
	// 初始化序列号
	seq = 0;
	ack = 0;
	this->base = 2; // base = 2，因为 0 和 1 已经被用于握手


	// 三次握手（实则只需两次握手）
	// 发送 SYN
	DataPacket_t packet = make_packet(
		senderAddr.sin_addr.s_addr, recvAddr.sin_addr.s_addr,
		senderAddr.sin_port, recvAddr.sin_port,
		seq, ack, SYN,
		"", 0
	);

	// 发送数据包
	send_packet(senderSocket, recvAddr, packet);
	seq++;
	log(LogType::LOG_TYPE_INFO, "[Send SYN]");
	// 超时重传 Timeout Retransmission
	while (true) {
		// 设置 fd_set 结构
		fd_set readfds;
		FD_ZERO(&readfds);
		FD_SET(senderSocket, &readfds);

		// 设置超时时间
		struct timeval timeout;
		timeout.tv_sec = TIME_OUT_SECS;
		timeout.tv_usec = TIME_OUT_USECS;

		// 使用 select 函数进行超时检测
		int ret = select(0, &readfds, NULL, NULL, &timeout);
		if (ret == 0) {
			// 超时，进行重传
			log(LogType::LOG_TYPE_INFO, "Time out! Resend packet!");
			send_packet(senderSocket, recvAddr, packet);
		} else if (ret == SOCKET_ERROR) {
			// Log
			log(LogType::LOG_TYPE_ERROR, std::format("select function failed with error: {}", WSAGetLastError()));
			closesocket(senderSocket);
			WSACleanup();
			exit(1);
		} else {
			// 有数据可读，进行读取
			char recvBuf[sizeof(DataPacket)];
			DataPacket_t recvPacket;
			int recvLen = recv_packet(senderSocket, recvAddr, recvBuf, sizeof(DataPacket), recvPacket);
			if (recvLen == -1) {
				log(LogType::LOG_TYPE_ERROR, "recv_packet() failed: checksum error");
				continue;
			}
			if (isSYN(recvPacket) && isACK(recvPacket) && recvPacket->seq == ack) {
				ack = recvPacket->seq + 1;
				log(LogType::LOG_TYPE_INFO, "[Recv SYN + ACK]");
				break;
			}
			log(LogType::LOG_TYPE_INFO, "[SYN + ACK recv failed]");
		}
	}
	// 回收内存
	delete packet;

	// 发送 ACK
	packet = make_packet(
		senderAddr.sin_addr.s_addr, recvAddr.sin_addr.s_addr,
		senderAddr.sin_port, recvAddr.sin_port,
		seq, ack, ACK,
		"", 0
	);
	// 发送数据包
	send_packet(senderSocket, recvAddr, packet);
	seq++;
	log(LogType::LOG_TYPE_INFO, "[Send ACK]");

	// Handshake Succeed
	log(LogType::LOG_TYPE_INFO, "Handshake Succeed!");
	delete packet; // 释放内存
}

void Sender::sendFile(const char* filePath) {
	// 初始化 recvThread
	recvRunning = true;
	recvThread = thread(std::bind(&Sender::recvThreadFunc, this));
	// recvThread = thread([&]() {
		
	// });

	log(LogType::LOG_TYPE_INFO, std::format("Start Sending File `{}`!", filePath));
	// 在发送文件之前获取当前时间
    auto start = std::chrono::high_resolution_clock::now();
	// 先发送文件名
	DataPacket_t packet = make_packet(
		senderAddr.sin_addr.s_addr, recvAddr.sin_addr.s_addr,
		senderAddr.sin_port, recvAddr.sin_port,
		seq, ack, BEG,
		filePath, strlen(filePath)+1
	);
	sendPacket(packet);
	log(LogType::LOG_TYPE_INFO, "[BEG] FileName Sent!");

	// 读取文件
	ifstream fin(filePath, ios::binary);
	if (!fin) {
		log(LogType::LOG_TYPE_ERROR, std::format("File `{}` Not Found!", filePath));
		return;
	}

	// 获取文件长度
	fin.seekg(0, ios::end);
	unsigned long long fileLen = fin.tellg();
	fin.seekg(0, ios::beg);

	char fileBuf[MAX_DATA_LENGTH]; // 文件缓冲区
	int dataLen; // 读取到的数据长度

	// 发送文件
	while (true) {
		if (in_window_interval(seq, this->base, windowSize)) {
			// 读取文件
			fin.read(fileBuf, MAX_DATA_LENGTH);
			dataLen = fin.gcount();
			if (dataLen == 0) {
				// 文件读取完毕
				break;
			}
			// 发送数据
			packet = make_packet(
				senderAddr.sin_addr.s_addr, recvAddr.sin_addr.s_addr,
				senderAddr.sin_port, recvAddr.sin_port,
				seq, ack, 0,
				fileBuf, dataLen
			);
			sendPacket(packet);
		}
	}

	// 发送 END
	packet = make_packet(
		senderAddr.sin_addr.s_addr, recvAddr.sin_addr.s_addr,
		senderAddr.sin_port, recvAddr.sin_port,
		seq, ack, END,
		"", 0
	);
	sendPacket(packet);
	log(LogType::LOG_TYPE_INFO, "[END] File Sent!");

	// 文件传输完成
	recvRunning = false;

	// 关闭文件
	fin.close();

	// 接收数据包线程退出
	if (recvThread.joinable()) {
		recvThread.join();
	}
	
	log(LogType::LOG_TYPE_INFO, std::format("File `{}` Sent Successfully!", filePath));
	log(LogType::LOG_TYPE_INFO, std::format("File Length: {} Bytes", fileLen));

	// 在发送文件之后获取当前时间
    auto end = std::chrono::high_resolution_clock::now();
	// 计算并打印传输时间
    std::chrono::duration<double> diff = end - start;
	log(LogType::LOG_TYPE_INFO, std::format("File transfer time: {} s", diff.count()));
	// 平均吞吐量
	log(LogType::LOG_TYPE_INFO, std::format("Average throughput: {} B/s\n", fileLen / diff.count()));
}

void Sender::close() {
	// 四次挥手
	// 发送 FIN + ACK
	DataPacket_t packet = make_packet(
		senderAddr.sin_addr.s_addr, recvAddr.sin_addr.s_addr,
		senderAddr.sin_port, recvAddr.sin_port,
		seq, ack, FIN | ACK,
		"", 0
	);
	// 发送数据包
	send_packet(senderSocket, recvAddr, packet);
	seq++;
	log(LogType::LOG_TYPE_INFO, "[Send FIN + ACK]");
	// 超时重传 Timeout Retransmission
	while (true) {
		// 设置 fd_set 结构
		fd_set readfds;
		FD_ZERO(&readfds);
		FD_SET(senderSocket, &readfds);

		// 设置超时时间
		struct timeval timeout;
		timeout.tv_sec = TIME_OUT_SECS;
		timeout.tv_usec = TIME_OUT_USECS;

		// 使用 select 函数进行超时检测
		int ret = select(0, &readfds, NULL, NULL, &timeout);
		if (ret == 0) {
			// 超时，进行重传
			log(LogType::LOG_TYPE_INFO, "Time out! Resend packet!");
			send_packet(senderSocket, recvAddr, packet);
			log(LogType::LOG_TYPE_INFO, "[Resend FIN + ACK]");
		} else if (ret == SOCKET_ERROR) {
			// Log
			log(LogType::LOG_TYPE_ERROR, std::format("select function failed with error: {}", WSAGetLastError()));
			closesocket(senderSocket);
			WSACleanup();
			exit(1);
		} else {
			// 接收 ACK
			if (recvACK()) {
				ack--; // 无需增加 ack
				log(LogType::LOG_TYPE_INFO, "[Recv ACK]");
				break;
			}
		}
	}
	// 回收内存
	delete packet;


	// 接收 FIN + ACK
	char recvBuf[sizeof(DataPacket)];
	DataPacket_t recvPacket;
	int recvLen = recv_packet(senderSocket, recvAddr, recvBuf, sizeof(DataPacket), recvPacket);
	
	if (recvLen == -1) {
		log(LogType::LOG_TYPE_ERROR, "recv_packet() failed: checksum error");
	}
	if (isFIN(recvPacket) && isACK(recvPacket) && recvPacket->seq == ack) {
		ack = recvPacket->seq + 1;
		log(LogType::LOG_TYPE_INFO, "[Recv FIN + ACK]");
	}
	else {
		log(LogType::LOG_TYPE_INFO, "[FIN + ACK recv failed]");
	}


	// 发送 ACK
	packet = make_packet(
		senderAddr.sin_addr.s_addr, recvAddr.sin_addr.s_addr,
		senderAddr.sin_port, recvAddr.sin_port,
		seq, ack, ACK,
		"", 0
	);
	send_packet(senderSocket, recvAddr, packet);
	log(LogType::LOG_TYPE_INFO, "[Send ACK]");


	// Wavehand Succeed
	log(LogType::LOG_TYPE_INFO, "Close Sender");
	log(LogType::LOG_TYPE_INFO, "Wavehand Succeed!");
}

void Sender::sendPacket(DataPacket_t packet) {
	while (true) { // 阻塞直到发送成功
		if (in_window_interval(packet->seq, this->base, windowSize)) {
			// 发送数据包
			send_packet(senderSocket, recvAddr, packet);
			seq++; // 序列号+1，而不是增加数据长度
			ack++; // 增加 ack
			{
				// 添加到窗口
				lock_guard<mutex> lock(windowMutex); // 加锁，生命周期结束后自动解锁
				window.push_back(packet);
				ackWindow.push_back(false);
			}

			/* 计时器线程启动 */
			std::thread* timer = new std::thread(std::bind(&Sender::timerThreadFunc, this, packet->seq));
			this->timerThreads.push_back(timer);

			// Debug
			static unsigned int a = 0;
			a++;
			log(LogType::LOG_TYPE_ERROR, std::format("a = {}", a));

			break;
		}
		// else { // 接收数据包
		// 	// Debug
		// 	log(LogType::LOG_TYPE_ERROR, "Start Receiving Packet!");

		// 	char recvBuf[sizeof(DataPacket)];
		// 	DataPacket_t recvPacket;
		// 	int recvLen = recv_packet(senderSocket, recvAddr, recvBuf, sizeof(DataPacket), recvPacket);
		// 	if (recvLen == -1) {
		// 		log(LogType::LOG_TYPE_ERROR, "recv_packet() failed: checksum error");
		// 		continue;
		// 	}

		// 	// Debug
		// 	// log(LogType::LOG_TYPE_ERROR, std::format("Recv Thread Still Running!"));

		// 	// 用于计时器线程防止死锁
		// 	unsigned int count = 0;
		// 	if (isACK(recvPacket)) {
		// 		lock_guard<mutex> lock(windowMutex); // 加锁，生命周期结束后自动解锁
		// 		if (in_window_interval(recvPacket->ack, this->base + 1, windowSize)) {
		// 			// ack = recvPacket->seq + 1;
		// 			log(LogType::LOG_TYPE_INFO, "[ACK recv succeed]");

		// 			// 是否是窗口的开头
		// 			if (recvPacket->ack == this->base + 1) { // 是，则前移滑动窗口
		// 				ackWindow.front() = true;
		// 				while (!window.empty() && (ackWindow.front() == true)) {
		// 					delete window.front(); // 回收内存
		// 					window.pop_front();
		// 					ackWindow.pop_front();
		// 					// 更新 base
		// 					(this->base)++;
		// 					// 增加 count 计数
		// 					count++;

		// 					// Debug
		// 					// log(LogType::LOG_TYPE_ERROR, std::format("count: {}", count));
		// 				}
		// 			}
		// 			else {
		// 				// 修改对应的 ackWindow
		// 				ackWindow.at(get_window_index(recvPacket->ack, this->base + 1)) = true;
		// 			}
		// 		}
		// 		else {
		// 			// Debug
		// 			// log(LogType::LOG_TYPE_ERROR, std::format("recvPacket->ack: {}", recvPacket->ack));
		// 		}
		// 	}
		// 	else {
		// 		log(LogType::LOG_TYPE_INFO, "[ACK recv failed]");
		// 	}
		// 	// 互斥锁已释放，从而避免死锁
		// 	for (unsigned int i = 0; i < count; i++) {
		// 		std::thread* timer = this->timerThreads.front();
		// 		if (timer->joinable()) {
		// 			timer->join(); // 等待释放
		// 		}
		// 		delete timer; // 回收内存
		// 		this->timerThreads.pop_front();
				
		// 		// Debug
		// 		log(LogType::LOG_TYPE_ERROR, std::format("Release timer thread!  count: {}", count));
		// 	}
		// }
	}
}

bool Sender::recvACK() { // 接收 ACK  /* 目前仅用于挥手 */
	char recvBuf[sizeof(DataPacket)];
	DataPacket_t recvPacket;
	int recvLen = recv_packet(senderSocket, recvAddr, recvBuf, sizeof(DataPacket), recvPacket);
	if (recvLen == -1) {
		log(LogType::LOG_TYPE_ERROR, "recv_packet() failed: checksum error");
		return false;
	}
	if (isACK(recvPacket) && recvPacket->seq == ack) {
		ack = recvPacket->seq + 1;
		log(LogType::LOG_TYPE_INFO, "[ACK recv succeed]");
		return true;
	}
	log(LogType::LOG_TYPE_INFO, "[ACK recv failed]");
	return false;
}


void Sender::timerThreadFunc(unsigned int packet_seq) {
	while (true) {
		// sleep
		std::this_thread::sleep_for(std::chrono::seconds(TIME_OUT_SECS));
		std::this_thread::sleep_for(std::chrono::microseconds(TIME_OUT_USECS));

		// check if received ACK
		bool hasACKed = true;
		{
			lock_guard<mutex> lock(windowMutex); // 加锁，生命周期结束后自动解锁
			auto ack_it = ackWindow.begin();
			for (auto it = window.begin(); it != window.end(); it++) {
				// Debug
				// log(LogType::LOG_TYPE_ERROR, std::format("packet->seq: {}", packet->seq));
				if ((*it)->seq == packet_seq) { // found
					if (*ack_it == false) { // ack NOT received
						hasACKed = false;
						// 超时重传
						log(LogType::LOG_TYPE_INFO, "Time out! Resend packet!");
						// 重发数据包
						send_packet(senderSocket, recvAddr, (*it));
					}
					break;
				}
				ack_it++;
			}
		}
		if (hasACKed)
			break; // 结束线程
	}
}

void Sender::recvThreadFunc() {
	while (true) {
		if ((recvRunning == false) && window.empty())
			break; // 结束线程
		char recvBuf[sizeof(DataPacket)];
		DataPacket_t recvPacket;

		// Debug
		log(LogType::LOG_TYPE_ERROR, "Start Receiving Packet!");
		
		int recvLen = recv_packet(senderSocket, recvAddr, recvBuf, sizeof(DataPacket), recvPacket);
		if (recvLen == -1) {
			log(LogType::LOG_TYPE_ERROR, "recv_packet() failed: checksum error");
			continue;
		}

		// Debug
		log(LogType::LOG_TYPE_ERROR, std::format("Recv Thread Still Running!"));

		// 用于计时器线程防止死锁
		unsigned int count = 0;
		if (isACK(recvPacket)) {
			lock_guard<mutex> lock(windowMutex); // 加锁，生命周期结束后自动解锁
			if (in_window_interval(recvPacket->ack, this->base + 1, windowSize)) {
				// ack = recvPacket->seq + 1;
				log(LogType::LOG_TYPE_INFO, "[ACK recv succeed]");

				// 是否是窗口的开头
				if (recvPacket->ack == this->base + 1) { // 是，则前移滑动窗口
					ackWindow.front() = true;
					while (!window.empty() && (ackWindow.front() == true)) {
						delete window.front(); // 回收内存
						window.pop_front();
						ackWindow.pop_front();
						// 更新 base
						(this->base)++;
						// 增加 count 计数
						count++;

						// Debug
						log(LogType::LOG_TYPE_ERROR, std::format("count: {}", count));
					}
				}
				else {
					// 修改对应的 ackWindow
					ackWindow.at(get_window_index(recvPacket->ack, this->base + 1)) = true;
				}
			}
			else {
				// Debug
				log(LogType::LOG_TYPE_ERROR, std::format("recvPacket->ack: {}", recvPacket->ack));
			}
		}
		else {
			log(LogType::LOG_TYPE_INFO, "[ACK recv failed]");
		}
		// 互斥锁已释放，从而避免死锁
		for (unsigned int i = 0; i < count; i++) {
			std::thread* timer = this->timerThreads.front();
			if (timer->joinable()) {
				timer->join(); // 等待释放
			}
			delete timer; // 回收内存
			this->timerThreads.pop_front();
			
			// Debug
			log(LogType::LOG_TYPE_ERROR, std::format("Release timer thread!  count: {}", count));
		}
	}

	// Debug
	log(LogType::LOG_TYPE_ERROR, std::format("Recv Thread Exit!"));
}


int main() {
	Sender sender;
	// unsigned int windowSize; // 窗口大小
	// cout << "Input window size: ";
	// cin >> windowSize;
	// sender.init(DEFAULT_SERVER_IP, DEFAULT_SERVER_PORT, windowSize);
	sender.init(DEFAULT_SERVER_IP, DEFAULT_SERVER_PORT);
	string filePath;
	cout << "Input file path: ";
	while (cin >> filePath) {
		if (filePath == "exit") {
			break;
		}
		sender.connect();
		sender.sendFile(filePath.c_str());
		sender.close();
		
		cout << "Input file path: ";
	}
	return 0;
}
