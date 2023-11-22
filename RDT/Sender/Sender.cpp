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

void Sender::init(const char* serverIP, unsigned short serverPort) {
	this->serverIP = serverIP;
	this->serverPort = serverPort;

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
}

void Sender::connect() {
	// 初始化序列号
	seq = 0;
	ack = 0;

	// 三次握手
	// 发送 SYN
	DataPacket_t packet = make_packet(
		senderAddr.sin_addr.s_addr, recvAddr.sin_addr.s_addr,
		senderAddr.sin_port, recvAddr.sin_port,
		seq, ack, SYN,
		"", 0
	);
	int ret = sendto(senderSocket, (char*)packet, PKT_HEADER_SIZE, 0, (SOCKADDR*)&recvAddr, sizeof(SOCKADDR));
	if (ret == SOCKET_ERROR) {
		log(LogType::LOG_TYPE_ERROR, std::format("sendto() failed with error: {}", WSAGetLastError()));
		log(LogType::LOG_TYPE_ERROR, "Handshake Failed!");
		closesocket(senderSocket);
		WSACleanup();
		exit(1);
	}
	seq++;
	log(LogType::LOG_TYPE_PKT, "[Send SYN]", packet);

	// 接收 SYN + ACK
	char recvBuf[sizeof(DataPacket)];
	int addrLen = sizeof(recvAddr);
	int recvLen = recvfrom(senderSocket, recvBuf, sizeof(DataPacket), 0, (SOCKADDR*)&recvAddr, &addrLen);
	if (recvLen == SOCKET_ERROR) {
		// Log
		log(LogType::LOG_TYPE_ERROR, std::format("recvfrom() failed with error: {}", WSAGetLastError()));
		log(LogType::LOG_TYPE_ERROR, "Handshake Failed!");
		closesocket(senderSocket);
		WSACleanup();
		exit(1);
	}
	delete packet; // 释放内存
	if (parse_packet(recvBuf, recvLen, packet)) {
		if (isSYN(packet) && isACK(packet)) {
			log(LogType::LOG_TYPE_PKT, "[Recv SYN + ACK]", packet);
			// 发送 ACK
			ack = packet->seq + 1;
			packet = make_packet(
				senderAddr.sin_addr.s_addr, recvAddr.sin_addr.s_addr,
				senderAddr.sin_port, recvAddr.sin_port,
				seq, ack, ACK,
				"", 0
			);
			sendto(senderSocket, (char*)packet, PKT_HEADER_SIZE, 0, (SOCKADDR*)&recvAddr, sizeof(SOCKADDR));
			seq++;
			log(LogType::LOG_TYPE_PKT, "[Send ACK]", packet);
			// Log
			log(LogType::LOG_TYPE_INFO, "Handshake Succeed!");
			delete packet; // 释放内存
		}
		else {
			// Log
			log(LogType::LOG_TYPE_ERROR, "Handshake Failed!");
			closesocket(senderSocket);
			WSACleanup();
			exit(1);
		}
	}
	else {
		// Log
		// Checksum Error
		log(LogType::LOG_TYPE_ERROR, "Handshake Failed!");
		closesocket(senderSocket);
		WSACleanup();
		exit(1);
	}
}

void Sender::sendFile(const char* filePath) {
	char recvBuf[sizeof(DataPacket)]; // 缓冲区
	int recvLen; // 接收到的数据长度
	DataPacket_t recvPacket = nullptr; // 接收到的数据包
	unsigned long long fileLen = 0; // 文件长度
	// 在发送文件之前获取当前时间
    auto start = std::chrono::high_resolution_clock::now();
	log(LogType::LOG_TYPE_INFO, std::format("Start Sending File {}!", filePath));
	// 先发送文件名
	DataPacket_t packet = make_packet(
		senderAddr.sin_addr.s_addr, recvAddr.sin_addr.s_addr,
		senderAddr.sin_port, recvAddr.sin_port,
		seq, ack, BEG,
		filePath, strlen(filePath)+1
	);
	sendto(senderSocket, (char*)packet, PKT_HEADER_SIZE + packet->dataLength, 0, (SOCKADDR*)&recvAddr, sizeof(SOCKADDR));
	seq += packet->dataLength;
	log(LogType::LOG_TYPE_PKT, "[Send] Start of file", packet);
	// Log
	log(LogType::LOG_TYPE_INFO, "File Name Sent!");

	// 超时重传
	while (true) {
		// 设置 fd_set 结构
		fd_set readfds;
		FD_ZERO(&readfds);
		FD_SET(senderSocket, &readfds);

		// 设置超时时间
		struct timeval timeout;
		timeout.tv_sec = TIME_OUT_SECS;
		timeout.tv_usec = 0;

		// 使用 select 函数进行超时检测
		int ret = select(0, &readfds, NULL, NULL, &timeout);
		if (ret == 0) {
			// 超时，进行重传
			log(LogType::LOG_TYPE_INFO, "Time out! Resend packet!");
			sendto(senderSocket, (char*)packet, PKT_HEADER_SIZE + packet->dataLength, 0, (SOCKADDR*)&recvAddr, sizeof(SOCKADDR));
			log(LogType::LOG_TYPE_PKT, "[Resend]", packet);
		} else if (ret == SOCKET_ERROR) {
			// Log
			log(LogType::LOG_TYPE_ERROR, std::format("select function failed with error: {}", WSAGetLastError()));
			break;
		} else {
			// 有数据可读，进行读取
			int addrLen = sizeof(recvAddr);
			recvLen = recvfrom(senderSocket, recvBuf, sizeof(DataPacket), 0, (SOCKADDR*)&recvAddr, &addrLen);
			if (parse_packet(recvBuf, recvLen, recvPacket)) {
				if (isACK(recvPacket) && recvPacket->ack == seq) {
					ack = recvPacket->seq + 1;
					// Log
					log(LogType::LOG_TYPE_PKT, "[recv]", recvPacket);
					delete packet; // 释放内存
					break;
				}
				else {
					// Log
					log(LogType::LOG_TYPE_ERROR, "File Name Sent Failed!");
					log(LogType::LOG_TYPE_PKT, "[recv]", recvPacket);
				}
			}
			else {
				// Log
				// Checksum Error
				log(LogType::LOG_TYPE_ERROR, "Checksum Error");
				log(LogType::LOG_TYPE_PKT, "[recv]", recvPacket);
			}
		}
	}

	// 读取文件
	ifstream fin(filePath, ios::binary);
	if (!fin) {
		// Log
		log(LogType::LOG_TYPE_ERROR, std::format("File {} Not Found!"));
		closesocket(senderSocket);
		WSACleanup();
		exit(1);
	}

	// 获取文件长度
	fin.seekg(0, ios::end);
	fileLen = fin.tellg();
	fin.seekg(0, ios::beg);

	char fileBuf[MAX_DATA_LENGTH]; // 文件缓冲区
	int dataLen; // 读取到的数据长度

	// 发送文件
	while (true) {
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
		sendto(senderSocket, (char*)packet, PKT_HEADER_SIZE + packet->dataLength, 0, (SOCKADDR*)&recvAddr, sizeof(SOCKADDR));
		seq += packet->dataLength;
		// Log
		log(LogType::LOG_TYPE_PKT, "[Send]", packet);
		// cout << "File " << filePath << " Sent " << seq << " Bytes!\n";

		// 超时重传
		while (true) {
			// 设置 fd_set 结构
			fd_set readfds;
			FD_ZERO(&readfds);
			FD_SET(senderSocket, &readfds);

			// 设置超时时间
			struct timeval timeout;
			timeout.tv_sec = TIME_OUT_SECS;
			timeout.tv_usec = 0;

			// 使用 select 函数进行超时检测
			int ret = select(0, &readfds, NULL, NULL, &timeout);
			if (ret == 0) {
				// 超时，进行重传
				log(LogType::LOG_TYPE_INFO, "Time out! Resend packet!");
				sendto(senderSocket, (char*)packet, PKT_HEADER_SIZE + packet->dataLength, 0, (SOCKADDR*)&recvAddr, sizeof(SOCKADDR));
				log(LogType::LOG_TYPE_PKT, "[Resend]", packet);
			} else if (ret == SOCKET_ERROR) {
				// Log
				log(LogType::LOG_TYPE_ERROR, std::format("select function failed with error: {}", WSAGetLastError()));
				break;
			} else {
				// 有数据可读，进行读取
				int addrLen = sizeof(recvAddr);
				recvLen = recvfrom(senderSocket, recvBuf, sizeof(DataPacket), 0, (SOCKADDR*)&recvAddr, &addrLen);
				if (parse_packet(recvBuf, recvLen, recvPacket)) {
					if (isACK(recvPacket) && recvPacket->ack == seq) {
						ack = recvPacket->seq + 1;
						// Log
						log(LogType::LOG_TYPE_PKT, "[recv]", recvPacket);
						delete packet; // 释放内存
						break;
					}
					else {
						// Log
						log(LogType::LOG_TYPE_ERROR, "File Sent Error");
						log(LogType::LOG_TYPE_PKT, "[recv]", recvPacket);
					}
				}
				else {
					// Log
					// Checksum Error
					log(LogType::LOG_TYPE_ERROR, "Checksum Error");
					log(LogType::LOG_TYPE_PKT, "[recv]", recvPacket);
				}
			}
		}
	}

	// 发送 END
	packet = make_packet(
		senderAddr.sin_addr.s_addr, recvAddr.sin_addr.s_addr,
		senderAddr.sin_port, recvAddr.sin_port,
		seq, ack, END,
		"", 0
	);
	sendto(senderSocket, (char*)packet, PKT_HEADER_SIZE, 0, (SOCKADDR*)&recvAddr, sizeof(SOCKADDR));
	log(LogType::LOG_TYPE_PKT, "[Send] End of file", packet);
	seq++;
	// 超时重传
	while (true) {
		// 设置 fd_set 结构
		fd_set readfds;
		FD_ZERO(&readfds);
		FD_SET(senderSocket, &readfds);

		// 设置超时时间
		struct timeval timeout;
		timeout.tv_sec = TIME_OUT_SECS;
		timeout.tv_usec = 0;

		// 使用 select 函数进行超时检测
		int ret = select(0, &readfds, NULL, NULL, &timeout);
		if (ret == 0) {
			// 超时，进行重传
			log(LogType::LOG_TYPE_INFO, "Time out! Resend packet!");
			sendto(senderSocket, (char*)packet, PKT_HEADER_SIZE + packet->dataLength, 0, (SOCKADDR*)&recvAddr, sizeof(SOCKADDR));
			log(LogType::LOG_TYPE_PKT, "[Resend]", packet);
		} else if (ret == SOCKET_ERROR) {
			// Log
			log(LogType::LOG_TYPE_ERROR, std::format("select function failed with error: {}", WSAGetLastError()));
			break;
		} else {
			// 有数据可读，进行读取
			int addrLen = sizeof(recvAddr);
			recvLen = recvfrom(senderSocket, recvBuf, sizeof(DataPacket), 0, (SOCKADDR*)&recvAddr, &addrLen);
			if (parse_packet(recvBuf, recvLen, recvPacket)) {
				if (isACK(recvPacket) && recvPacket->ack == seq) {
					ack = recvPacket->seq + 1;
					// Log
					log(LogType::LOG_TYPE_PKT, "[recv]", recvPacket);
					delete packet; // 释放内存
					break;
				}
				else {
					// Log
					log(LogType::LOG_TYPE_ERROR, "File Sent Error");
					log(LogType::LOG_TYPE_PKT, "[recv]", recvPacket);
				}
			}
			else {
				// Log
				// Checksum Error
				log(LogType::LOG_TYPE_ERROR, "Checksum Error");
				log(LogType::LOG_TYPE_PKT, "[recv]", recvPacket);
			}
		}
	}

	// 文件传输完成
	fin.close();
	log(LogType::LOG_TYPE_INFO, std::format("File {} Sent Successfully!", filePath));
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
	sendto(senderSocket, (char*)packet, PKT_HEADER_SIZE, 0, (SOCKADDR*)&recvAddr, sizeof(SOCKADDR));
	seq++;
	// Log
	log(LogType::LOG_TYPE_PKT, "[Send FIN + ACK]", packet);

	// 接收 ACK
	char recvBuf[sizeof(DataPacket)];
	DataPacket_t recvPacket;
	int addrLen = sizeof(recvAddr);
	int recvLen = recvfrom(senderSocket, recvBuf, sizeof(DataPacket), 0, (SOCKADDR*)&recvAddr, &addrLen);
	if (recvLen == SOCKET_ERROR) {
		// Log
		log(LogType::LOG_TYPE_ERROR, std::format("recvfrom() failed with error: {}", WSAGetLastError()));
		log(LogType::LOG_TYPE_ERROR, "Close Failed!");
		closesocket(senderSocket);
		WSACleanup();
		exit(1);
	}
	if (parse_packet(recvBuf, recvLen, recvPacket)) {
		if (isACK(recvPacket) && ack == recvPacket->seq) {
			// ack = recvPacket->seq + 1; 无需增加 ack
			// Log
			log(LogType::LOG_TYPE_PKT, "[Recv ACK]", recvPacket);
		}
		else {
			// Log
			log(LogType::LOG_TYPE_ERROR, "Close Failed!");
			closesocket(senderSocket);
			WSACleanup();
			exit(1);
		}
	}
	else {
		// Log
		// Checksum Error
		log(LogType::LOG_TYPE_ERROR, "Close Failed!");
		closesocket(senderSocket);
		WSACleanup();
		exit(1);
	}

	// 接收 FIN + ACK
	recvLen = recvfrom(senderSocket, recvBuf, sizeof(DataPacket), 0, (SOCKADDR*)&recvAddr, &addrLen);
	if (recvLen == SOCKET_ERROR) {
		// Log
		log(LogType::LOG_TYPE_ERROR, std::format("recvfrom() failed with error: {}", WSAGetLastError()));
		log(LogType::LOG_TYPE_ERROR, "Close Failed!");
		closesocket(senderSocket);
		WSACleanup();
		exit(1);
	}
	if (parse_packet(recvBuf, recvLen, recvPacket)) {
		if (isFIN(recvPacket) && isACK(recvPacket) && ack == recvPacket->seq) {
			ack = recvPacket->seq + 1;
			log(LogType::LOG_TYPE_PKT, "[Recv FIN + ACK]", recvPacket);
		}
		else {
			// Log
			log(LogType::LOG_TYPE_ERROR, "ERR");
			// cout << "Close Failed!\n";
			// closesocket(senderSocket);
			// WSACleanup();
			// exit(1);
		}
		// log(LogType::LOG_TYPE_PKT, "[Recv FIN + ACK]", recvPacket);
	}
	else {
		// Log
		// Checksum Error
		log(LogType::LOG_TYPE_ERROR, "Close Failed!");
		// cout << "checksum error!\n";
		closesocket(senderSocket);
		WSACleanup();
		exit(1);
	}

	// 发送 ACK
	delete packet; // 释放内存
	packet = make_packet(
		senderAddr.sin_addr.s_addr, recvAddr.sin_addr.s_addr,
		senderAddr.sin_port, recvAddr.sin_port,
		seq, ack, ACK,
		"", 0
	);
	sendto(senderSocket, (char*)packet, PKT_HEADER_SIZE, 0, (SOCKADDR*)&recvAddr, sizeof(SOCKADDR));
	seq++;
	// Log
	log(LogType::LOG_TYPE_PKT, "[Send ACK]", packet);

	// Log
	log(LogType::LOG_TYPE_INFO, "Close Sender");
	log(LogType::LOG_TYPE_INFO, "Wavehand Succeed!");
}

// DWORD WINAPI TimeoutThread(LPVOID lpParam) {
// 	Sender* sender = (Sender*)lpParam;
// 	// 超时重传
// 	return 0;
// }

int main() {
	Sender sender;
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
