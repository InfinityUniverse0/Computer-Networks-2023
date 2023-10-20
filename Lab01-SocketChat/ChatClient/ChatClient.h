#pragma once

#include <winsock2.h>
#pragma comment(lib, "ws2_32.lib") // 链接 ws2_32.lib 库文件到此项目中，这样才可以使用 WinSock 编程
#include <ws2tcpip.h>
#include <windows.h>

#include <string.h>
#include <string>
#include <stdlib.h>
#include <iostream>
using namespace std;

#define DEFAULT_SERVER_IP "127.0.0.1"
#define DEFAULT_SERVER_PORT 8888

#define DEFAULT_BUFLEN 1024 // 缓冲区大小


class ChatClient {
	const char* serverIP; // 服务器 IP
	unsigned short serverPort; // 服务器端口号
	SOCKET clientSocket; // 客户端的 socket
	string userName; // 用户名
	unsigned int clientID; // 客户端 ID
public:
	ChatClient(const char* ip = DEFAULT_SERVER_IP, unsigned short port = DEFAULT_SERVER_PORT);
	~ChatClient();
	void Start();
private:
	void Connect();
	void Close();
	static bool isExit;
	static DWORD WINAPI RecvMsgThread(LPVOID lpParam);
};
