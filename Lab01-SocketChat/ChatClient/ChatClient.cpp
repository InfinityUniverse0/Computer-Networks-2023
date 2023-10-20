// SocketChat 聊天程序：客户端

#include "ChatClient.h"

// 声明 ChatClient 类内的静态成员变量
bool ChatClient::isExit = false;

ChatClient::ChatClient(const char* ip, unsigned short port) : serverIP(ip), serverPort(port) {
	cout << "Creating ChatClient..." << endl;

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

	cout << "ChatClient Created!" << endl;
	cout << endl;
}

ChatClient::~ChatClient() {
	// 关闭 WinSock
	WSACleanup();
}

void ChatClient::Connect() {
	cout << "Connecting to SocketChat Server..." << endl;
	// 创建客户端 socket
	clientSocket = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
	if (clientSocket == INVALID_SOCKET) {
		cout << "\tCreate Socket Failed!\n";
		WSACleanup();
		exit(1);
	}
	cout << "\tCreate Socket Success!\n";

	// 设置服务器地址
	SOCKADDR_IN serverAddr;
	serverAddr.sin_family = AF_INET;
	serverAddr.sin_port = htons(serverPort);
	inet_pton(AF_INET, serverIP, &serverAddr.sin_addr);
	//serverAddr.sin_addr.S_un.S_addr = inet_addr(serverIP);

	// 连接服务器
	int recvLen = connect(clientSocket, (SOCKADDR*)&serverAddr, sizeof(SOCKADDR));
	if (recvLen == SOCKET_ERROR) {
		cout << "Connect Failed!\n";
		closesocket(clientSocket);
		WSACleanup();
		exit(1);
	}
	if (recvLen == 0)
		cout << "Connect Success!\n";

	cout << endl;

	char recvBuf[DEFAULT_BUFLEN + 1]; // 接收缓冲区

	// 接收服务器分配的 ID
	recvLen = recv(clientSocket, recvBuf, DEFAULT_BUFLEN, 0);
	if (recvLen == SOCKET_ERROR) {
		cout << "Recv Client ID Failed!\n";
		closesocket(clientSocket);
		WSACleanup();
		exit(1);
	}
	recvBuf[recvLen] = '\0';
	clientID = (unsigned int)atoi(recvBuf);

	// 输入用户名
	cout << "Please Input Your Name:\n";
	cin >> userName;
	while (userName == "") {
		cout << "User Name Can Not Be Empty!\n";
		cout << "Please Input Your Name Again:\n";
		cin >> userName;
	}

	// 发送用户名
	recvLen = send(clientSocket, userName.c_str(), userName.length(), 0);
	if (recvLen == SOCKET_ERROR) {
		cout << "Send User Name Failed!\n";
		closesocket(clientSocket);
		WSACleanup();
		exit(1);
	}

	cout << "Your User Name: " << userName << endl;
	cout << "Your Client ID: " << clientID << endl;
	cout << endl;
}

void ChatClient::Start() {
	Connect(); // 连接服务器

	cout << "Connected to SocketChat Server!" << endl;
	cout << "Input `exit` to close ChatClient" << endl;

	// 创建接收消息线程
	HANDLE hThread = CreateThread(NULL, 0, RecvMsgThread, (LPVOID)clientSocket, 0, NULL);
	if (hThread == NULL) {
		cout << "Create RecvMsgThread Failed!\n";
		closesocket(clientSocket);
		WSACleanup();
		exit(1);
	}
	CloseHandle(hThread);

	// 发送消息
	while (true) {
		string msg;
		getline(cin, msg);
		cout << endl;
		if (msg == "exit") {
			// 退出
			isExit = true;
			break;
		}
		else {
			// 发送消息
			send(clientSocket, msg.c_str(), msg.length(), 0);
		}
	}
	Close();
}

DWORD WINAPI ChatClient::RecvMsgThread(LPVOID lpParam) {
	SOCKET clientSocket = (SOCKET)lpParam;
	char recvBuf[DEFAULT_BUFLEN + 1]; // 接收缓冲区
	int recvLen;
	while (true) {
		// 接收消息
		recvLen = recv(clientSocket, recvBuf, DEFAULT_BUFLEN, 0);
		if (isExit) // 退出
			break;
		if (recvLen == SOCKET_ERROR) {
			cout << "--------- RecvMsgError ---------\n";
			cout << "Recv Socket Fail!\n";
			cout << "Error Code: " << WSAGetLastError() << endl;
			cout << "--------------------------------\n";
			cout << "服务器端异常关闭！" << endl;
			// 退出
			closesocket(clientSocket);
			WSACleanup();
			exit(1);
		}
		else if (recvLen == 0) {
			// 服务器关闭
			cout << "Server Closed!\n";
			closesocket(clientSocket);
			WSACleanup();
			exit(1);
		}
		else {
			recvBuf[recvLen] = '\0';
			// 显示消息
			cout << recvBuf << endl;
		}
	}
	return 0;
}

void ChatClient::Close() {
	// shutdown 关闭发送和接受服务
	shutdown(clientSocket, SD_BOTH);
	// 关闭客户端 socket
	closesocket(clientSocket);
	cout << "Client Exits!\n";
}
