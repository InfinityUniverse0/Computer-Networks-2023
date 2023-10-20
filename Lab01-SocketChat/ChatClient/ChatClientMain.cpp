// SocketChat 聊天程序：客户端 main 文件

#include "ChatClient.h"

int main() {
	cout << "--------- SocketChat Client ---------\n" << endl;
	ChatClient client; // 默认使用本地服务器
	//ChatClient client(REMOTE_SERVER_IP); // 使用远程服务器
	client.Start();
	return 0;
}
