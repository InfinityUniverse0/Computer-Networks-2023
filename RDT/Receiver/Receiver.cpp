#include "Receiver.h"
#include <assert.h>

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

void Receiver::init(const char* serverIP, unsigned short serverPort, unsigned int windowSize) {
	this->serverIP = serverIP;
	this->serverPort = serverPort;
	this->windowSize = windowSize;

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

	log(LogType::LOG_TYPE_INFO, std::format("Window Size: {}", windowSize));
}

void Receiver::start() {
	// 初始化序列号
	seq = 0;
	ack = 0;
	this->base = 3; // base = 3，因为 0 和 1 已经被用于握手，2 已经用于接收 BEG

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
	while (true) {
		if (recv_packet(recvSocket, senderAddr, recvBuf, sizeof(DataPacket), recvPacket) != -1) {
			if (isBEG(recvPacket)) {
				log(LogType::LOG_TYPE_INFO, "[Recv BEG]");
				log(LogType::LOG_TYPE_INFO, std::format("Start Receiving File `{}`!", recvPacket->data));
				// 发送 ACK
				DataPacket_t ackPacket = make_packet(
					recvAddr.sin_addr.s_addr, senderAddr.sin_addr.s_addr,
					recvAddr.sin_port, senderAddr.sin_port,
					seq, (recvPacket->seq + 1), ACK,
					"", 0
				);
				this->sendACK(ackPacket);
				delete ackPacket; // 回收内存

				// 更新 ack
				this->ack = recvPacket->seq + 1;
				break;
			}
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
	bool recvEND = false;
	while (true) {
		if (recvEND) {
			fout.close(); // 关闭文件
			log(LogType::LOG_TYPE_INFO, std::format("Receive File `{}` Succeed!", filePath));
			assert(window.empty());
			break;
		}
		if (this->recvPacket()) { // 准备写入文件
			unsigned int count = 0;
			auto it = window.begin();
			while (!window.empty() && (it != window.end()) && (*it != nullptr)) {
				if (isEND(*it)) {
					log(LogType::LOG_TYPE_INFO, "[Recv END]");
					recvEND = true;
					delete window.front(); // 回收内存
					window.pop_front();
					// 更新 base
					(this->base)++;
					break;
				}
				else {
					// 写入文件
					fout.write((*it)->data, (*it)->dataLength);
					if (!fout) {
						log(LogType::LOG_TYPE_ERROR, "Failed to write to file");
						// Need to close socket?
						closesocket(recvSocket);
						WSACleanup();
						exit(1);
					}
				}

				// 前移滑动窗口
				it++;
				count++;
				// 更新 base
				(this->base)++;
			}

			for (unsigned int i = 0; i < count; i++) {
				delete window.front(); // 回收内存
				window.pop_front();
			}
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

bool Receiver::recvPacket() {
	/*
	 * 接收数据包
	 * Return:
	 * 		当可以写入文件时，return true
	 */
	DataPacket_t packet = new DataPacket;
	char* buf = (char*)packet;
	int recvLen = recv_packet(recvSocket, senderAddr, buf, sizeof(DataPacket), packet);
	if (recvLen == -1) {
		log(LogType::LOG_TYPE_ERROR, "recv_packet() failed: checksum error");
		return false;
	}

	// 判断是否位于窗口区间
	if (in_window_interval(packet->seq, this->base, windowSize)) {
		// 发送 ACK
		DataPacket_t ackPacket = make_packet(
			recvAddr.sin_addr.s_addr, senderAddr.sin_addr.s_addr,
			recvAddr.sin_port, senderAddr.sin_port,
			seq, (packet->seq + 1), ACK,
			"", 0
		);
		this->sendACK(ackPacket);
		delete ackPacket; // 回收内存

		// 更新 ack
		if (in_window_interval(packet->seq + 1, this->ack, windowSize)) {
			this->ack = packet->seq + 1;
		}

		if (packet->seq == this->base) {
			if (window.empty()) {
				window.push_back(packet);
			}
			else {
				DataPacket_t winPkt = window.front();
				if (winPkt == nullptr) {
					window.front() = packet;
				}
			}
			// 可以写入文件
			return true;
		}
		else { // 写入窗口
			if (window.empty()) { // window 为空
				window.push_back(nullptr);
			}
			unsigned int index = get_window_index(packet->seq, this->base);
			while (window.size() < index) {
				window.push_back(nullptr);
			}
			if (window.size() == index) {
				window.push_back(packet);
			}
			else {
				DataPacket_t winPkt = window.at(index);
				if (winPkt == nullptr) { // 之前未收到
					window.at(index) = packet; // 将 nullptr 改为 packet
				}
				else {
					this->seq--; // 重复收到，seq 不变
				}
			}
		}
	}
	else { // 位于 [base-windowSize, base) 区间
		// 发送 ACK
		DataPacket_t ackPacket = make_packet(
			recvAddr.sin_addr.s_addr, senderAddr.sin_addr.s_addr,
			recvAddr.sin_port, senderAddr.sin_port,
			packet->ack, (packet->seq + 1), ACK,
			"", 0
		);
		this->sendACK(ackPacket);
		seq--;
		delete ackPacket; // 回收内存
	}

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
