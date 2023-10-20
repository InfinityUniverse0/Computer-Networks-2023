// SocketChat 聊天程序：服务器端 main 文件

#include "ChatServer.h"

int main() {
	cout << "--------- SocketChat Server ---------\n" << endl;
	ChatServer server;
	server.Start();
	return 0;
}
