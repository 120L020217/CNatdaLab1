#include "stdafx.h"
#include <Windows.h>
#include <process.h>
#include <string.h>
#pragma comment(lib,"Ws2_32.lib")

#define MAXSIZE 655070 //发送数据报文的最大长度
#define HTTP_PORT 80 //http 服务器端口

//Http 重要头部数据
struct HttpHeader {
	char method[4]; // POST 或者 GET，注意有些为 CONNECT，本实验暂不考虑
	char url[1024]; // 请求的 url
	char host[1024]; // 目标主机

	char cookie[1024 * 10]; //cookie
	HttpHeader() { // 构造函数
		ZeroMemory(this, sizeof(HttpHeader));
	}
};

BOOL InitSocket();
void ParseHttpHead(char* buffer, HttpHeader* httpHeader);
BOOL ConnectToServer(SOCKET* serverSocket, char* host);
unsigned int __stdcall ProxyThread(LPVOID lpParameter);

// 参数是char* buffer的, 都需要注意是否需要保护buffer的不变性, 防止buffer被修改
void getFileName(char* url, char* filename);
void getDate(char* buffer, char* date);
void newPaper(char* buffer, char* date);
void savePaper(char* buffer, char* filename);
void ifSavePaper(char* buffer, char* filename);

//代理相关参数
SOCKET ProxyServer; // 代理服务器的套接字
sockaddr_in ProxyServerAddr; // 代理服务器端点(套接字)地址
const int ProxyPort = 10240; // 代理服务器监听端口

//由于新的连接都使用新线程进行处理，对线程的频繁的创建和销毁特别浪费资源
//可以使用线程池技术提高服务器效率
//const int ProxyThreadMaxNum = 20;
//HANDLE ProxyThreadHandle[ProxyThreadMaxNum] = {0};  // 一个成员是void*类型的数组
//DWORD ProxyThreadDW[ProxyThreadMaxNum] = {0}; // 一个成员是long(4字)类型的数组
struct ProxyParam { // 代理服务器的发送套接字和接受套接字
	SOCKET clientSocket;
	SOCKET serverSocket;
};

char invalid_website[25] = "http://info.cern.ch/";
char restrict_host[25] = "127.0.0.1";
char fishing_src[25] = "http://today.hit.edu.cn/"; //钓鱼网站原网址  
char fishing_dest[25] = "http://jwts.hit.edu.cn/"; //钓鱼网站目标网址  
char fishing_dest_host[25] = "jwts.hit.edu.cn"; //钓鱼目的地址主机名  

int _tmain(int argc, _TCHAR* argv[])
{
	printf("代理服务器正在启动\n");
	printf("初始化...\n");
	if (!InitSocket()) {
		printf("socket 初始化失败\n");
		return -1;
	}
	printf("代理服务器正在运行，监听端口 %d\n", ProxyPort);
	SOCKET acceptSocket = INVALID_SOCKET; // 初始化
	ProxyParam* lpProxyParam; // 代理服务器
	HANDLE hThread; // 子线程句柄
	DWORD dwThreadID = 0; // 子线程ID 初始化为0
	SOCKADDR_IN clientAddr; 
	int length = sizeof(SOCKADDR); 

	//代理服务器不断监听
	while (true) {
		acceptSocket = accept(ProxyServer, (SOCKADDR *) & clientAddr, &length); // 后两个参数用来记录客户端的地址和地址长度, 以存储用户地址
		// 屏蔽本机用户
		/*if (!strcmp(restrict_host, inet_ntoa(clientAddr.sin_addr))) {
		    printf("该用户访问受限\n");
			closesocket(acceptSocket);
            continue;
        }*/
		if (acceptSocket == INVALID_SOCKET) {
			printf("用于与客户端数据传送的套接字建立失败, 错误代码为: %d\n", WSAGetLastError());
			return 0;
		}
		lpProxyParam = new ProxyParam; // 在堆空间申请内存, 因为是new出来的对象. 如果定义结构体变量, 则在栈中分配内存
		if (lpProxyParam == NULL) { // 申请堆空间失败, 链接未建立
			printf("申请堆空间失败, 关闭接受套接字\n");
			closesocket(acceptSocket);
			continue;
		}
		lpProxyParam->clientSocket = acceptSocket;
		// 线程调用函数, 第三个参数表示线程开始的函数地址, 第四个参数表示调用参数列表
		// 第五个参数是线程初始状态, 0表示立即执行, 第六个参数用于记录线程ID地址
		hThread = (HANDLE)_beginthreadex(NULL, 0, &ProxyThread, (LPVOID)lpProxyParam, 0, (unsigned int *) & dwThreadID);
		if (hThread == NULL) {
			printf("新线程创建失败, 关闭接受套接字\n");
			closesocket(acceptSocket);
			continue;
		}
		// 实际上这个操作并没有关闭线程, 只是将线程hthead的内核对象引用计数减一(此时为1). 
		// 当线程结束时, 内核对象引用计数再减一(此时为0). 操作系统回收线程资源.
		CloseHandle(hThread); // 关闭线程句柄
		Sleep(200); // 阻塞0.2s
	}
	closesocket(ProxyServer); 
	WSACleanup();
	return 0;
}

//************************************
// Method: InitSocket
// FullName: InitSocket
// Access: public 
// Returns: BOOL
// Qualifier: 初始化套接字
//***********************************
BOOL InitSocket() {
	//加载套接字库（必须）
	WORD wVersionRequested; // WORD是无符2字节short类型
	WSADATA wsaData;
	//套接字加载时错误提示
	int err;
	//版本 2.2
	wVersionRequested = MAKEWORD(2, 2);
	//加载 dll 文件 Scoket 库
	err = WSAStartup(wVersionRequested, &wsaData);
	if (err != 0) {
		//找不到 winsock.dll
		printf("加载 winsock 失败，错误代码为: %d\n", WSAGetLastError());
		return FALSE;
	}
	if (LOBYTE(wsaData.wVersion) != 2 || HIBYTE(wsaData.wVersion) != 2)
	{
		printf("不能找到正确的 winsock 版本\n");
		WSACleanup();
		return FALSE;
	}
	// windows系统中,af_inet和pf_inet完全一样
	// 更改第三个参数为TCP协议号
	struct protoent* p;
	p = getprotobyname("tcp");
	ProxyServer = socket(AF_INET, SOCK_STREAM, p->p_proto); 
	if (INVALID_SOCKET == ProxyServer) {
		printf("创建监听套接字失败，错误代码为：%d\n", WSAGetLastError());
		return FALSE;
	}
	ProxyServerAddr.sin_family = AF_INET;
	ProxyServerAddr.sin_port = htons(ProxyPort); // htons()用来转换网络字节顺序
	ProxyServerAddr.sin_addr.S_un.S_addr = INADDR_ANY;
	if (bind(ProxyServer, (SOCKADDR*)&ProxyServerAddr, sizeof(SOCKADDR)) == SOCKET_ERROR) {
		printf("绑定监听套接字失败\n");
		return FALSE;
	}
	if (listen(ProxyServer, SOMAXCONN) == SOCKET_ERROR) { // 请求队列大小为5
		printf("监听端口%d 失败", ProxyPort);
		return FALSE;
	}
	return TRUE;
}

//************************************
// Method: ProxyThread
// FullName: ProxyThread
// Access: public 
// Returns: unsigned int __stdcall
// Qualifier: 线程执行函数
// Parameter: LPVOID lpParameter
//************************************
unsigned int __stdcall ProxyThread(LPVOID lpParameter) { // warning: 使用栈区空间65560字节, 栈溢出, 设置编译器栈区大小
	/*在堆分配空间, 防止栈占用过大
	但是堆处理效率太低(一方面很可能被操作系统分配给外存另一方面一次读写两次访存)*/

	char Buffer[MAXSIZE]; // 用于接受报文
	char socketBuffer[50]; // 用于socket缓存
	char* CacheBuffer; // 用于解析报文
	char FileBuffer[MAXSIZE]; // 用于本地文件缓存
	char filename[250]; // 本地文件名
	char date[30]; // 缓存日期
	ZeroMemory(Buffer, MAXSIZE); // 内存用二进制0填充, 对字符数组相当于所有字符'\0'
	ZeroMemory(socketBuffer, 50);
	ZeroMemory(FileBuffer, MAXSIZE);
	ZeroMemory(filename, 250);
	ZeroMemory(date, 30);
	SOCKADDR_IN clientAddr; // 没什么用处
	int length = sizeof(SOCKADDR_IN); // 没有什么用处
	int recvSize = 0;
	int ret;
	int curindex = 0;
	bool hascache = FALSE;
	FILE* fp;

	//recvSize = recv(((ProxyParam*)lpParameter)->clientSocket, Buffer, MAXSIZE, 0);
	//HttpHeader* httpHeader = new HttpHeader();
	//CacheBuffer = new char[recvSize + 1]; // 缓存报文副本, 多一个字节为'\0'相当于一个大字符串
	//ZeroMemory(CacheBuffer, recvSize + 1);
	//memcpy(CacheBuffer, Buffer, recvSize);
	//ParseHttpHead(CacheBuffer, httpHeader); // 解析url
	//delete[] CacheBuffer;
	recvSize = recv(((ProxyParam*)lpParameter)->clientSocket, socketBuffer, 50, 0); // 第一个参数为接收端socket描述符
	while (recvSize == 50) { // TCP流式传输
		memcpy(&Buffer[curindex], socketBuffer, recvSize);
		curindex += recvSize;
		ZeroMemory(socketBuffer, 50);
		recvSize = recv(((ProxyParam*)lpParameter)->clientSocket, socketBuffer, 50, 0);
	}
	memcpy(&Buffer[curindex], socketBuffer, recvSize);
	curindex += recvSize;
	ZeroMemory(socketBuffer, 50);
	
	//if (recvSize < 0) { // 错误判断
	//	printf("接收客户端数据时出错, 线程关闭\n");
	//	goto error;
	//}

	HttpHeader* httpHeader = new HttpHeader();
	CacheBuffer = new char[curindex + 1]; // 缓存报文副本, 多一个字节为'\0'相当于一个大字符串
	ZeroMemory(CacheBuffer, curindex + 1);
	memcpy(CacheBuffer, Buffer, curindex);
	ParseHttpHead(CacheBuffer, httpHeader); // 解析url
	delete[] CacheBuffer;

	if (httpHeader->method[0] == '\0') { // 对应CONNECT报文
		goto error;
	}

	if (strstr(httpHeader->url, invalid_website) != NULL) {
		printf("\n=====================\n");
	    printf("--------该网站已被屏蔽!----------\n");
	    goto error;
	}

	if (strstr(httpHeader->url, fishing_src) != NULL) {
	    printf("\n=====================\n");
		printf("-------------已从源网址：%s 转到 目的网址 ：%s ----------------\n", fishing_src, fishing_dest);
		//修改HTTP报文  
		memcpy(httpHeader->host, fishing_dest_host, strlen(fishing_dest_host) + 1);
		memcpy(httpHeader->url, fishing_dest, strlen(fishing_dest));
	}

	// printf("%s\n", getFileName(httpHeader->url, filename));
	getFileName(httpHeader->url, filename);
	if (!fopen_s(&fp, filename, "rb")) { // 文件缓存存在
		fread(FileBuffer, sizeof(char), MAXSIZE, fp);
		fclose(fp);
		getDate(FileBuffer, date);
		newPaper(Buffer, date);
		hascache = TRUE;
	}

	if (!ConnectToServer(&((ProxyParam*)lpParameter)->serverSocket, httpHeader->host)) {
		printf("代理未连接主机, 线程关闭\n");
		goto error;
	}
	printf("代理连接主机 %s 成功\n", httpHeader->host);

	//将客户端发送的 HTTP 数据报文直接转发给目标服务器
	// 第一个参数为发送数据的套接字描述符
	// 最后一个参数为设置为阻塞式
	ret = send(((ProxyParam*)lpParameter)->serverSocket, Buffer, sizeof(Buffer), 0); 
	if (ret == -1) {
		printf("代理转发报文给服务器失败, 线程关闭\n");
		goto error;
	}

	ZeroMemory(Buffer, MAXSIZE);
	curindex = 0;

	recvSize = recv(((ProxyParam*)lpParameter)->serverSocket, Buffer, MAXSIZE, 0);
	if (recvSize <= 0) {
		printf("接收服务器数据时出错, 线程关闭\n");
		goto error;
	}
	
	if (!hascache) {
		savePaper(Buffer, filename);
	}
	else {
		ifSavePaper(Buffer, filename);
	}

	//将目标服务器返回的数据直接转发给客户端
	ret = send(((ProxyParam*)lpParameter)->clientSocket, Buffer, sizeof(Buffer), 0);
	if (ret == -1) {
		printf("代理转发报文给客户端失败, 线程关闭\n");
		goto error;
	}
// 错误处理 同时也是线程结束的处理, 关闭套接字, 意味着非持续链接
error:
	Sleep(200);
	closesocket(((ProxyParam*)lpParameter)->serverSocket); 
	closesocket(((ProxyParam*)lpParameter)->clientSocket);
	delete lpParameter;
	delete httpHeader;
	_endthreadex(0); // 相比于closehandle(), 这个函数会直接关闭线程, 释放占用的内存
	return 0;
}

//************************************
// Method: ParseHttpHead
// FullName: ParseHttpHead
// Access: public 
// Returns: void
// Qualifier: 解析 TCP 报文中的 HTTP 头部
// Parameter: char * buffer
// Parameter: HttpHeader * httpHeader
//************************************
void ParseHttpHead(char* buffer, HttpHeader* httpHeader) {
	char* p;
	char* ptr = NULL;
	const char* delim = "\r\n";
	p = strtok_s(buffer, delim, &ptr);//提取第一行
	if (p[0] == 'G') {//GET 方式
		printf("%s\n", p);
		memcpy(httpHeader->method, "GET", 3);
		memcpy(httpHeader->url, &p[4], strlen(p) - 13); // 13 = "GET  http/1.1"
	}
	else if (p[0] == 'P') {//POST 方式
		printf("%s\n", p);
		memcpy(httpHeader->method, "POST", 4);
		memcpy(httpHeader->url, &p[5], strlen(p) - 14); // 
	}
	else {
		return;
	}
	//printf("%s\n", httpHeader->url);
	p = strtok_s(NULL, delim, &ptr);
	while (p) {
		switch (p[0]) {
		case 'H'://Host
			memcpy(httpHeader->host, &p[6], strlen(p) - 6); // p[6]是抛去"host: "的6个字符
			break;
		case 'C'://Cookie
			if (strlen(p) > 8) {
				char header[8];
				ZeroMemory(header, sizeof(header));
				memcpy(header, p, 6);
				if (!strncmp(header, "Cookie", 6)) { // 确认是cookie首部行
					memcpy(httpHeader->cookie, &p[8], strlen(p) - 8);
				}
			}
			break;
		default:
			break;
		}
		p = strtok_s(NULL, delim, &ptr);
	}
}

//************************************
// Method: ConnectToServer
// FullName: ConnectToServer
// Access: public 
// Returns: BOOL
// Qualifier: 根据主机创建目标服务器套接字，并连接
// Parameter: SOCKET * serverSocket
// Parameter: char * host
//************************************
BOOL ConnectToServer(SOCKET* serverSocket, char* host) { // 相当于客户端, 所以省略了bind配置本地ip和端口
	sockaddr_in serverAddr; // 配置原服务器的地址
	serverAddr.sin_family = AF_INET;
	serverAddr.sin_port = htons(HTTP_PORT);
	HOSTENT* hostent = gethostbyname(host); // 域名->32位IP地址
	if (!hostent) {
		printf("通过域名获取ip地址失败\n");
		return FALSE;
	}
	in_addr Inaddr = *((in_addr*)*hostent->h_addr_list);
	serverAddr.sin_addr.s_addr = inet_addr(inet_ntoa(Inaddr));

	struct protoent* p;
	p = getprotobyname("tcp");
	*serverSocket = socket(AF_INET, SOCK_STREAM, p->p_proto);
	if (*serverSocket == INVALID_SOCKET) {
		printf("创建代理发送套接字失败，错误代码为：%d\n", WSAGetLastError());
		return FALSE;
	}
	if (connect(*serverSocket, (SOCKADDR*)&serverAddr, sizeof(serverAddr)) == SOCKET_ERROR) {
		printf("代理连接失败, 关闭代理发送套接字\n");
		closesocket(*serverSocket);
		return FALSE;
	}
	return TRUE;
}

void getFileName(char* url, char* filename) {
	int i = 7; // 抛去http://
	while (url[i] != '\0') {
		if (url[i] == '/') {
			filename[i - 7] = ' ';
			i++;
		}
		else if (url[i] == '.') {
			filename[i - 7] = ' ';
			i++;
		}
		else {
			filename[i-7] = url[i];
			i++;
		}
	}
	filename[i - 8] = '\0';
	/*filename[i - 7] = '.';
	filename[i - 6] = 't';
	filename[i - 5] = 'x';
	filename[i - 4] = 't';*/
}

void getDate(char* buffer, char* date) {
	char* p;
	char* ptr = NULL;
	const char* delim = "\r\n";
	const char* field = "Date";
	// const char* field = "Last-Modified"; // 很多相应报文没有lastmodified字段, 所以为方便验收, 改成取date字段
	p = strtok_s(buffer, delim, &ptr);
	while (p) {
		if (strstr(p, field) != NULL) {
			memcpy(date, &p[6], strlen(p) - 6);
			break;
		}
		p = strtok_s(NULL, delim, &ptr);
	}
}

void newPaper(char* buffer, char* date) {
	printf("buffer: %s\n", buffer);
	printf("date: %s\n", date);
	char* p;
	char* ptr = NULL;
	const char* delim = "\r\n";
	const char* field = "Host: ";
	const char* newfield = "If-Modified-Since: ";
	
	p = strstr(buffer, field);
	
	if (p) {
		while (*p != '\n') {
			p++;
		}
		p++;
	}
	else {
		printf("没有Host首部行, 程序关闭\n");
		exit(0);
	}
	printf("找到插入位置\n");

	char temp[MAXSIZE];
	ZeroMemory(temp, MAXSIZE);
	int i = 0;
	while (p[i] != '\0') {
		temp[i] = p[i];
		i++;
	}
	printf("缓存临时变量\n");
	printf("temp = %s\n", temp);
	while (*newfield != '\0') {
		*(p++) = *(newfield++);
	}
	printf("插入字段\n");
	while (*date != '\0') {
		*(p++) = *(date++);
	}
	printf("插入日期\n");
	*(p++) = '\r';
	*(p++) = '\n';
	i = 0;
	while (temp[i] != '\0') {
		*(p++) = temp[i];
		i++;
	}
	printf("缓存写入\n");
	printf("buffer: %s\n", buffer);
}

void savePaper(char* buffer, char* filename) {
	char* p;
	char* ptr = NULL;
	const char* delim = "\r\n";
	char temp[MAXSIZE];
	ZeroMemory(temp, MAXSIZE);
	memcpy(temp, buffer, MAXSIZE);
	p = strtok_s(temp, delim, &ptr);
	char state[4];
	state[3] = '\0';
	memcpy(state, &p[9], 3); // p[9]是抛去"http/1.1 "
	
	if (!strcmp(state, "200")) { //状态码正常时缓存
		printf("代理服务器缓存完毕\n");
		FILE* fp;
		if (!fopen_s(&fp, filename, "wb")) { // 成功打开文件
			fwrite(buffer, sizeof(char), MAXSIZE, fp); // 按照最大格式写报文
			fclose(fp);
		}
		else {
			printf("文件打开失败\n");
		}
	}
	printf("state: %s\n", state);
}

void ifSavePaper(char* buffer, char* filename) {
	char* p;
	char* ptr = NULL;
	const char* delim = "\r\n";
	char temp[MAXSIZE];
	ZeroMemory(temp, MAXSIZE);
	memcpy(temp, buffer, MAXSIZE);
	p = strtok_s(temp, delim, &ptr);
	char state[4];
	state[3] = '\0';
	memcpy(state, &p[9], 3); // p[9]是抛去"http/1.1 "
	if (!strcmp(state, "304")) { // 状态码304, 无需缓存
		printf("代理服务器已经缓存\n");
		ZeroMemory(buffer, MAXSIZE);
		FILE* fp;
		if (!fopen_s(&fp, filename, "rb")) { // 成功打开文件
			fread(buffer, sizeof(char), MAXSIZE, fp); 
			fclose(fp);
		}
	}
	else {
		printf("代理服务器重新缓存\n");
		savePaper(buffer, filename);
	}
}