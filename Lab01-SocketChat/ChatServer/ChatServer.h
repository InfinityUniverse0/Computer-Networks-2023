#pragma once

#include <winsock2.h>
#pragma comment(lib, "ws2_32.lib") // 链接 ws2_32.lib 库文件到此项目中，这样才可以使用 WinSock 编程
#include <ws2tcpip.h>
#include <windows.h>

#include <string.h>
#include <string>
#include <list>
#include <ctime>
#include <iomanip>
#include <sstream>
#include <stdlib.h>
#include <iostream>
using namespace std;

#define DEFAULT_SERVER_IP "127.0.0.1"
#define DEFAULT_SERVER_PORT 8888

#define DEFAULT_BUFLEN 1024 // 缓冲区大小
#define IPADDR_STRLEN 25
#define WELCOME_MSG "Welcome to SocketChat!" // 欢迎消息


class ChatServer {
	const char* serverIP; // 服务器 IP
	unsigned short serverPort; // 服务器端口号
	SOCKADDR_IN serverAddr; // 服务器 Socket 地址
	SOCKET serverSocket; // 服务器 Socket

	static list<SOCKET> clientSocketList; // 客户端 Socket 列表
	static list<string> userNameList; // 客户端用户名称列表
	static list<unsigned int> clientIDList; // 客户端 ID 列表，唯一标识客户端
public:
	ChatServer(const char* ip = DEFAULT_SERVER_IP, unsigned short port = DEFAULT_SERVER_PORT);
	~ChatServer();
	void Start();
	void Close();
private:
	void Init();
	static bool isExit; // 服务器是否退出
	static DWORD WINAPI RecvMsgThread(LPVOID lpParameter); // 接收客户端消息线程
	static void SendMsgToAll(char* msg, int len, SOCKET senderClientSocket = INVALID_SOCKET); // 发送消息给所有客户端
	static DWORD WINAPI ServerExit(LPVOID lpParameter); // 服务器退出
};
