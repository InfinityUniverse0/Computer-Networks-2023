// SocketChat 聊天程序：服务器端

#include "ChatServer.h"

// 声明 ChatServer 类内的静态成员变量
list<SOCKET> ChatServer::clientSocketList;
list<string> ChatServer::userNameList;
list<unsigned int> ChatServer::clientIDList;
bool ChatServer::isExit = false;

ChatServer::ChatServer(const char* ip, unsigned short port) : serverIP(ip), serverPort(port) {
	cout << "Creating ChatServer..." << endl;

	// 初始化 WinSock
	WORD wVersionRequested = MAKEWORD(2, 2); // 请求 2.2 版本的 WinSock 库
	WSADATA wsaData;
	int err = WSAStartup(wVersionRequested, &wsaData);
	if (!err) {
		// WSAStartup 成功
		cout << "\tWSAStartup Success!\n";
	}
	else {
		// WSAStartup 失败
		cout << "\tWSAStartup Fail!\n";
		cout << "\tError Code: " << err << endl;
		exit(1);
	}
	// 检查 WinSock 版本号
	cout << "\tCheck WinSock Version: ";
	if (wsaData.wVersion == wVersionRequested)
		cout << "Success!\n";
	else {
		cout << "Fail!\n";
		WSACleanup();
		exit(1);
	}

	serverAddr.sin_family = AF_INET; // IPv4 协议
	inet_pton(AF_INET, serverIP, &serverAddr.sin_addr); // 服务器 IP
	//serverAddr.sin_addr.S_un.S_addr = inet_addr(serverIP); // 服务器 IP
	serverAddr.sin_port = htons(serverPort); // 服务器端口号

	//char ipAddrStr[IPADDR_STRLEN];
	cout << "ChatServer Created!" << endl;
	cout << "ChatServer Info:\n";
	//cout << "\tServer IP: " << inet_ntop(AF_INET, &(serverAddr.sin_addr), ipAddrStr, IPADDR_STRLEN) << endl;
	cout << "\tServer IP: " << serverIP << endl;
	cout << "\tServer Port: " << serverPort << endl;
	cout << endl;
}

ChatServer::~ChatServer() {
	// 关闭 WinSock
	WSACleanup();
}

void ChatServer::Init() {
	cout << "ChatServer Init..." << endl;

	// 创建 Socket
	serverSocket = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
	if (serverSocket == INVALID_SOCKET) {
		cout << "\tCreate Socket Fail!\n";
		cout << "\tError Code: " << WSAGetLastError() << endl;
		WSACleanup();
		exit(1);
	}
	else
		cout << "\tCreate Socket Success!\n";

	// 绑定 Socket
	if (bind(serverSocket, (SOCKADDR*)&serverAddr, sizeof(SOCKADDR)) == SOCKET_ERROR) {
		cout << "\tBind Socket Fail!\n";
		cout << "\tError Code: " << WSAGetLastError() << endl;
		closesocket(serverSocket);
		WSACleanup();
		exit(1);
	}
	else
		cout << "\tBind Socket Success!\n";

	// 监听 Socket
	if (listen(serverSocket, SOMAXCONN) == SOCKET_ERROR) {
		cout << "\tListen Socket Fail!\n";
		cout << "\tError Code: " << WSAGetLastError() << endl;
		closesocket(serverSocket);
		WSACleanup();
		exit(1);
	}
	else
		cout << "\tListen Socket Success!\n";	

	cout << "ChatServer Init: Done!" << endl;
	cout << endl;
}

void ChatServer::Start() {
	Init(); // 初始化

	cout << "ChatServer Start!" << endl;
	cout << "Input `exit` to close ChatServer" << endl;

	CloseHandle(CreateThread(NULL, 0, ServerExit, (LPVOID)this, 0, NULL)); // 创建线程监听服务器退出

	// 采用多线程的方式接收客户端连接
	while (true) {
		// 接收客户端连接
		SOCKADDR_IN clientAddr;
		int clientAddrLen = sizeof(SOCKADDR); // 不赋值会导致 accept() 函数不阻塞
		SOCKET clientSocket = accept(serverSocket, (SOCKADDR*)&clientAddr, &clientAddrLen);
		if (isExit) // 服务器退出
			break;
		if (clientSocket == INVALID_SOCKET) {
			cout << "--------- AcceptSocketError ---------\n";
			continue;
		}

		// 创建线程前的客户端初始化操作：会引起阻塞
		char recvBuf[DEFAULT_BUFLEN + 1];
		int recvLen;

		// 记录客户端 Socket
		clientSocketList.push_back(clientSocket);

		// 记录客户端序号
		if (clientIDList.size() == 0) {
			clientIDList.push_back(1); // 第一个客户端序号为 1
		}
		else {
			clientIDList.push_back(clientIDList.back() + 1); // 客户端序号递增
		}

		/*
		 * 为防止前面的客户端输入用户名时发生阻塞，导致后面的客户端无法连接
		 * 这里首先由服务器为客户端分配一个默认的用户名（客户端 ID）
		 */

		// 发送客户端 ID
		string id = to_string(clientIDList.back());
		send(clientSocket, (char*)id.c_str(), id.length(), 0);

		// 为客户端分配默认用户名
		string defaultUserName = "User" + id;
		userNameList.push_back(defaultUserName);

		// 创建线程接收客户端消息
		HANDLE hThread = CreateThread(
			NULL, 0, (LPTHREAD_START_ROUTINE)RecvMsgThread, (LPVOID)clientSocket, 0, NULL
		);
		if (hThread == NULL) {
			cout << "Create RecvMsgThread Fail!\n";
			cout << "Error Code: " << GetLastError() << endl;
			closesocket(clientSocket);
		}
		else
			CloseHandle(hThread);
	}

	//Close(); // 关闭
}

DWORD WINAPI ChatServer::RecvMsgThread(LPVOID lpParameter) {
	SOCKET clientSocket = (SOCKET)lpParameter;
	char recvBuf[DEFAULT_BUFLEN + 1];
	int recvLen;

	// 接收客户端用户名
	recvLen = recv(clientSocket, recvBuf, DEFAULT_BUFLEN, 0);
	char* userName = new char[recvLen + 1];
	strncpy_s(userName, recvLen + 1, recvBuf, recvLen);
	userName[recvLen] = '\0';

	// 更新客户端用户名
	unsigned int clientID;
	list<SOCKET>::iterator it_clientSocket = clientSocketList.begin();
	list<string>::iterator it_userName = userNameList.begin();
	list<unsigned int>::iterator it_clientID = clientIDList.begin();
	for (int i = 0; i < clientIDList.size(); i++) {
		if (*it_clientSocket == clientSocket) {
			*it_userName = userName;
			clientID = *it_clientID;
			break;
		}
		it_clientSocket++;
		it_userName++;
		it_clientID++;
	}

	// 发送欢迎消息
	send(clientSocket, WELCOME_MSG, strlen(WELCOME_MSG), 0);

	// 广播消息
	string msg = "[ID:" + to_string(clientID) + "@" + userName + "] has joined the SocketChat Room!\n";
	msg += "# Current Online in this SocketChat Room: " + to_string(clientIDList.size());
	SendMsgToAll((char*)msg.c_str(), msg.length());

	while (true) {
		// 接收客户端消息
		recvLen = recv(clientSocket, recvBuf, DEFAULT_BUFLEN, 0);
		if (isExit) // 服务器退出
			break;
		if (recvLen == SOCKET_ERROR) {
			cout << "--------- RecvMsgError ---------\n";
			cout << "Recv Socket Fail!\n";
			cout << "Error Code: " << WSAGetLastError() << endl;
			cout << "Client Socket: " << clientSocket << endl;
			cout << "Client ID: " << clientIDList.back() << endl;
			cout << "User Name: " << userNameList.back() << endl;
			cout << "The Client has exited unexpectedly!\n";
			cout << "--------------------------------\n";
		}
		else if (recvLen == 0) {
			// 客户端关闭
		}
		else {
			recvBuf[recvLen] = '\0';
			// 广播消息
			SendMsgToAll(recvBuf, recvLen, clientSocket);
			continue;
		}

		// 退出信息记录
		unsigned int quitClientID;
		string quitUserName;

		// 清除客户端信息
		list<SOCKET>::iterator it_clientSocket = clientSocketList.begin();
		list<string>::iterator it_userName = userNameList.begin();
		list<unsigned int>::iterator it_clientID = clientIDList.begin();
		for (int i = 0; i < clientIDList.size(); i++) {
			if (*it_clientSocket == clientSocket) {
				quitClientID = *it_clientID;
				quitUserName = *it_userName;
				clientSocketList.erase(it_clientSocket);
				userNameList.erase(it_userName);
				clientIDList.erase(it_clientID);
				break;
			}
			it_clientSocket++;
			it_userName++;
			it_clientID++;
		}

		// 退出信息
		msg = "[ID:" + to_string(quitClientID) + "@" + quitUserName + "] has left the SocketChat Room!\n";
		msg += "# Current Online in this SocketChat Room: " + to_string(clientIDList.size());

		// 广播消息
		SendMsgToAll((char*)msg.c_str(), msg.length(), clientSocket);
		closesocket(clientSocket); // 关闭客户端 Socket
		break;
	}
}

void ChatServer::SendMsgToAll(char* msg, int len, SOCKET senderClientSocket) {
	// 获取当前系统时间
	time_t now = time(nullptr);
	tm localTime;
	localtime_s(&localTime, &now);
	// 格式化输出时间，并转为 string
	string timeStr;
	stringstream timeSStr;
	timeSStr << put_time(&localTime, "%Y-%m-%d %H:%M:%S");
	timeStr = timeSStr.str();

	unsigned int senderIdx; // 发送者序号
	string senderName; // 发送者用户名
	list<SOCKET>::iterator it_clientSocket = clientSocketList.begin();
	list<string>::iterator it_userName = userNameList.begin();
	list<unsigned int>::iterator it_clientID = clientIDList.begin();
	// 寻找发送者
	bool found = false;
	for (int i = 0; i < clientIDList.size(); i++) {
		if (*it_clientSocket == senderClientSocket) {
			senderIdx = *it_clientID;
			senderName = *it_userName;
			found = true;
			break;
		}
		it_clientSocket++;
		it_userName++;
		it_clientID++;
	}

	string sendMsg = timeStr + "  ";
	if (found) {
		sendMsg += "[ID:" + to_string(senderIdx) + "@" + senderName + "]:\n" + msg + "\n";
	}
	else {
		sendMsg += "[SocketChat Server]:\n" + string(msg) + "\n";
	}

	// Log
	cout << sendMsg << endl;

	// 发送消息给所有客户端
	for (it_clientSocket = clientSocketList.begin(); it_clientSocket != clientSocketList.end(); it_clientSocket++) {
		if (*it_clientSocket != senderClientSocket) {
			send(*it_clientSocket, (char*)sendMsg.c_str(), sendMsg.length(), 0);
		}
	}
}

void ChatServer::Close() {
	cout << "ChatServer Close..." << endl;

	// 清空客户端
	list<SOCKET>::iterator it_clientSocket = clientSocketList.begin();
	list<string>::iterator it_userName = userNameList.begin();
	list<unsigned int>::iterator it_clientID = clientIDList.begin();
	for (int i = 0; i < clientIDList.size(); i++) {
		// shutdown
		shutdown(*it_clientSocket, SD_BOTH);
		// closesocket
		closesocket(*it_clientSocket);

		it_clientSocket++;
		it_userName++;
		it_clientID++;
	}

	clientSocketList.clear();
	userNameList.clear();
	clientIDList.clear();

	cout << "\tClient List Cleared!" << endl;

	// 关闭服务器 Socket
	int err = closesocket(serverSocket);
	if (err == SOCKET_ERROR) {
		cout << "\tClose Socket Fail!\n";
		cout << "\tError Code: " << WSAGetLastError() << endl;
		WSACleanup();
		exit(1);
	}
	else
		cout << "\tClose Socket Success!\n";

	cout << "ChatServer Closed!" << endl;
	cout << endl;
}

DWORD WINAPI ChatServer::ServerExit(LPVOID lpParameter) {
	ChatServer* server = (ChatServer*)lpParameter;
	string exit_str;
	while (true) {
		cin >> exit_str;
		if (exit_str == "exit") {
			isExit = true; // 服务器退出
			server->Close();
			WSACleanup();
			exit(0);
			//break;
		}
	}
	return 0;
}
