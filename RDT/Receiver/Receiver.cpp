#include "Receiver.h"

Receiver::Receiver() {
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
		cout << "Fail!\n";
		WSACleanup();
		exit(1);
	}
}

Receiver::~Receiver() {
	// close socket
	closesocket(recvSocket);
	log(LogType::LOG_TYPE_INFO, "Receiver Socket Closed!");
	// close WinSock
	WSACleanup();
	log(LogType::LOG_TYPE_INFO, "WinSock Closed!");
}

void Receiver::init(const char* serverIP, unsigned short serverPort) {
	this->serverIP = serverIP;
	this->serverPort = serverPort;

	// Create socket
	recvSocket = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
	if (recvSocket == INVALID_SOCKET) {
		log(LogType::LOG_TYPE_ERROR, std::format("socket() failed with error: {}", WSAGetLastError()));
		WSACleanup();
		exit(1);
	}

	recvAddr.sin_family = AF_INET;
	recvAddr.sin_port = htons(serverPort);
	inet_pton(AF_INET, serverIP, &recvAddr.sin_addr);
	// recvAddr.sin_addr.s_addr = inet_addr(serverIP);

	// bind
	if (bind(recvSocket, (SOCKADDR*)&recvAddr, sizeof(SOCKADDR)) == SOCKET_ERROR) {
		log(LogType::LOG_TYPE_ERROR, std::format("bind() failed with error: {}", WSAGetLastError()));
		closesocket(recvSocket);
		WSACleanup();
		exit(1);
	}

	// Log
	log(LogType::LOG_TYPE_INFO, std::format("Receiver IP: {}", serverIP));
	log(LogType::LOG_TYPE_INFO, std::format("Receiver Port: {}", serverPort));
}

void Receiver::start() {
	// 初始化序列号
	seq = 0;
	ack = 0;

	// 三次握手
	char recvBuf[sizeof(DataPacket)];
	int recvLen;
	DataPacket_t recvPacket;
	bool has_syn = false;
	while (true) {
		// 接收 SYN
		int AddrLen = sizeof(senderAddr);
		recvLen = recvfrom(recvSocket, recvBuf, sizeof(DataPacket), 0, (SOCKADDR*)&senderAddr, &AddrLen);
		if (recvLen == SOCKET_ERROR) {
			log(LogType::LOG_TYPE_ERROR, std::format("recvfrom() failed with error: {}", WSAGetLastError()));
			closesocket(recvSocket);
			WSACleanup();
			exit(1);
		}

		if (parse_packet(recvBuf, recvLen, recvPacket)) {
			if (isSYN(recvPacket) && ack == recvPacket->seq) {
				has_syn = true;
				// Log
				log(LogType::LOG_TYPE_PKT, "[Recv SYN]", recvPacket);
				ack = recvPacket->seq + 1;
				// 发送 SYN + ACK
				DataPacket_t ackPacket = make_packet(
					recvAddr.sin_addr.s_addr, senderAddr.sin_addr.s_addr,
					recvAddr.sin_port, senderAddr.sin_port,
					seq, ack, SYN | ACK,
					"", 0
				);
				if (sendto(recvSocket, (char*)ackPacket, PKT_HEADER_SIZE, 0, (SOCKADDR*)&senderAddr, sizeof(SOCKADDR)) == SOCKET_ERROR) {
					log(LogType::LOG_TYPE_ERROR, std::format("sendto() failed with error: {}", WSAGetLastError()));
					closesocket(recvSocket);
					WSACleanup();
					exit(1);
				}
				seq++;
				log(LogType::LOG_TYPE_PKT, "[Send SYN + ACK]", ackPacket);
				delete ackPacket; // 释放内存
			}
			else if (isACK(recvPacket) && ack == recvPacket->seq) {
				log(LogType::LOG_TYPE_PKT, "[Recv ACK]", recvPacket);
				ack = recvPacket->seq + 1;
				if (has_syn) {
					break;
				}
			}
		}
	}
	
	// Log
	log(LogType::LOG_TYPE_INFO, "Handshake Succeed!");


	// 接收文件名
	// DataPacket_t recvPacket;
	while (true) {
		if (this->recvPacket(recvBuf, sizeof(DataPacket), recvPacket)) {
			if (isBEG(recvPacket)) {
				log(LogType::LOG_TYPE_INFO, "[Recv BEG]");
				log(LogType::LOG_TYPE_INFO, std::format("Start Receiving File `{}`!", recvPacket->data));
				// 发送 ACK
				DataPacket_t ackPacket = make_packet(
					recvAddr.sin_addr.s_addr, senderAddr.sin_addr.s_addr,
					recvAddr.sin_port, senderAddr.sin_port,
					seq, ack, ACK,
					"", 0
				);
				this->sendACK(ackPacket);
				delete ackPacket; // 回收内存
				break;
			}
		}
		else {
			// 重发上次 ACK
			DataPacket_t ackPacket = make_packet(
				recvAddr.sin_addr.s_addr, senderAddr.sin_addr.s_addr,
				recvAddr.sin_port, senderAddr.sin_port,
				--seq, ack, ACK,
				"", 0
			);
			this->sendACK(ackPacket);
			delete ackPacket; // 回收内存
		}
	}

	// 接收文件
	recvFile(recvPacket->data);
}

void Receiver::recvFile(const char* filePath) {
	// 打开文件
	filesystem::path p(filePath);
	p.replace_filename(string("recv/") + p.filename().string());
	filesystem::create_directories(p.parent_path());
	ofstream fout(p, ios::binary);
	if (!fout) {
		log(LogType::LOG_TYPE_ERROR, std::format("Failed to open file `{}`", filePath));
		return;
	}
	// 接收数据
	char recvBuf[sizeof(DataPacket)];
	DataPacket_t recvPacket;
	while (true) {
		if (this->recvPacket(recvBuf, sizeof(DataPacket), recvPacket)) {
			if (isEND(recvPacket)) {
				log(LogType::LOG_TYPE_INFO, "[Recv END]");
				// 发送 ACK
				DataPacket_t ackPacket = make_packet(
					recvAddr.sin_addr.s_addr, senderAddr.sin_addr.s_addr,
					recvAddr.sin_port, senderAddr.sin_port,
					seq, ack, ACK,
					"", 0
				);
				this->sendACK(ackPacket);
				delete ackPacket; // 回收内存
				// 关闭文件
				fout.close();
				// Log
				log(LogType::LOG_TYPE_INFO, std::format("Receive File `{}` Succeed!", filePath));
				break;
			}
			else {
				// 发送 ACK
				DataPacket_t ackPacket = make_packet(
					recvAddr.sin_addr.s_addr, senderAddr.sin_addr.s_addr,
					recvAddr.sin_port, senderAddr.sin_port,
					seq, ack, ACK,
					"", 0
				);
				this->sendACK(ackPacket);
				delete ackPacket; // 回收内存
				// 写入文件
				fout.write(recvPacket->data, recvPacket->dataLength);
				if (!fout) {
					log(LogType::LOG_TYPE_ERROR, "Failed to write to file");
					// Need to close socket?
					closesocket(recvSocket);
					WSACleanup();
					exit(1);
				}
			}
		}
		else {
			// 重发上次 ACK
			DataPacket_t ackPacket = make_packet(
				recvAddr.sin_addr.s_addr, senderAddr.sin_addr.s_addr,
				recvAddr.sin_port, senderAddr.sin_port,
				--seq, ack, ACK,
				"", 0
			);
			this->sendACK(ackPacket);
			delete ackPacket; // 回收内存
		}
	}
}

void Receiver::close() {
	// 四次挥手
	
	// 接收 FIN + ACK
	char recvBuf[sizeof(DataPacket)];
	int addrLen = sizeof(senderAddr);
	DataPacket_t recvPacket;
	int recvLen = recvfrom(recvSocket, recvBuf, sizeof(DataPacket), 0, (SOCKADDR*)&senderAddr, &addrLen);
	if (recvLen == SOCKET_ERROR) {
		log(LogType::LOG_TYPE_ERROR, std::format("recvfrom() failed with error: {}", WSAGetLastError()));
		closesocket(recvSocket);
		WSACleanup();
		exit(1);
	}
	if (parse_packet(recvBuf, recvLen, recvPacket)) {
		if (isFIN(recvPacket) && isACK(recvPacket) && ack == recvPacket->seq) {
			ack = recvPacket->seq + 1;
			// Log
			log(LogType::LOG_TYPE_PKT, "[Recv FIN + ACK]", recvPacket);
			// 发送 ACK
			DataPacket_t ackPacket = make_packet(
				recvAddr.sin_addr.s_addr, senderAddr.sin_addr.s_addr,
				recvAddr.sin_port, senderAddr.sin_port,
				seq, ack, ACK,
				"", 0
			);
			if (sendto(recvSocket, (char*)ackPacket, PKT_HEADER_SIZE, 0, (SOCKADDR*)&senderAddr, sizeof(SOCKADDR)) == SOCKET_ERROR) {
				cout << "sendto() failed with error: " << WSAGetLastError() << endl;
				closesocket(recvSocket);
				WSACleanup();
				exit(1);
			}
			// seq++; // 不需要更新 seq
			log(LogType::LOG_TYPE_PKT, "[Send ACK]", ackPacket);
			delete ackPacket; // 释放内存

			// 发送 FIN + ACK
			DataPacket_t finPacket = make_packet(
				recvAddr.sin_addr.s_addr, senderAddr.sin_addr.s_addr,
				recvAddr.sin_port, senderAddr.sin_port,
				seq, ack, FIN | ACK,
				"", 0
			);
			if (sendto(recvSocket, (char*)finPacket, PKT_HEADER_SIZE, 0, (SOCKADDR*)&senderAddr, sizeof(SOCKADDR)) == SOCKET_ERROR) {
				cout << "sendto() failed with error: " << WSAGetLastError() << endl;
				closesocket(recvSocket);
				WSACleanup();
				exit(1);
			}
			seq++;
			log(LogType::LOG_TYPE_PKT, "[Send FIN + ACK]", finPacket);
			delete finPacket; // 释放内存
		}
	}

	// 接收 ACK
	recvLen = recvfrom(recvSocket, recvBuf, sizeof(DataPacket), 0, (SOCKADDR*)&senderAddr, &addrLen);
	if (recvLen == SOCKET_ERROR) {
		cout << "recvfrom() failed with error: " << WSAGetLastError() << endl;
		closesocket(recvSocket);
		WSACleanup();
		exit(1);
	}
	if (parse_packet(recvBuf, recvLen, recvPacket)) {
		if (isACK(recvPacket) && ack == recvPacket->seq) {
			log(LogType::LOG_TYPE_PKT, "[Recv ACK]", recvPacket);
			log(LogType::LOG_TYPE_INFO, "Close Receiver");
			log(LogType::LOG_TYPE_INFO, "Wavehand Succeed!");
		}
	}
}

void Receiver::sendACK(DataPacket_t packet) {
	send_packet(recvSocket, senderAddr, packet);
	seq++;
}

bool Receiver::recvPacket(char* buf, int bufLen, DataPacket_t& packet) {
	int recvLen = recv_packet(recvSocket, senderAddr, buf, bufLen, packet);
	if (recvLen == -1) {
		log(LogType::LOG_TYPE_ERROR, "recv_packet() failed: checksum error");
		return false;
	}
	if (packet->seq == ack) {
		ack += packet->dataLength;
		if (packet->dataLength == 0) {
			ack++; // END, SYN, FIN, ACK 等 `空`包需增加 ack
		}
		return true;
	}
	log(LogType::LOG_TYPE_INFO, "ACK not matched!");
	return false;
}


int main() {
	Receiver receiver;
	receiver.init(DEFAULT_IP, DEFAULT_PORT);
	while (true) {
		receiver.start();
		receiver.close();
	}
	return 0;
}
