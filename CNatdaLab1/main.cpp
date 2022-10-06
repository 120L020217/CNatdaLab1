#include "stdafx.h"
#include <Windows.h>
#include <process.h>
#include <string.h>
#pragma comment(lib,"Ws2_32.lib")

#define MAXSIZE 65507 //�������ݱ��ĵ���󳤶�
#define HTTP_PORT 80 //http �������˿�
//Http ��Ҫͷ������
struct HttpHeader {
	char method[4]; // POST ���� GET��ע����ЩΪ CONNECT����ʵ���ݲ�����
	char url[1024]; // ����� url
	char host[1024]; // Ŀ������

	char cookie[1024 * 10]; //cookie
	HttpHeader() { // ���캯��
		ZeroMemory(this, sizeof(HttpHeader));
	}
};

BOOL InitSocket();
void ParseHttpHead(char* buffer, HttpHeader* httpHeader);
BOOL ConnectToServer(SOCKET* serverSocket, char* host);
unsigned int __stdcall ProxyThread(LPVOID lpParameter);

//������ز���
SOCKET ProxyServer; // �������������׽���
sockaddr_in ProxyServerAddr; // �����������˵�(�׽���)��ַ
const int ProxyPort = 10240; // ���������������˿�

//�����µ����Ӷ�ʹ�����߳̽��д��������̵߳�Ƶ���Ĵ����������ر��˷���Դ
//����ʹ���̳߳ؼ�����߷�����Ч��
//const int ProxyThreadMaxNum = 20;
//HANDLE ProxyThreadHandle[ProxyThreadMaxNum] = {0};  // һ����Ա��void*���͵�����
//DWORD ProxyThreadDW[ProxyThreadMaxNum] = {0}; // һ����Ա��long(4��)���͵�����
struct ProxyParam { // �����������ķ����׽��ֺͽ����׽���
	SOCKET clientSocket;
	SOCKET serverSocket;
};

int _tmain(int argc, _TCHAR* argv[])
{
	//int count = 0;
	printf("������������������\n");
	printf("��ʼ��...\n");
	if (!InitSocket()) {
		printf("socket ��ʼ��ʧ��\n");
		return -1;
	}
	printf("�����������������У������˿� %d\n", ProxyPort);
	SOCKET acceptSocket = INVALID_SOCKET; // ��ʼ��
	ProxyParam* lpProxyParam; // ����������
	HANDLE hThread; // ���߳̾��
	DWORD dwThreadID = 0; // ���߳�ID ��ʼ��Ϊ0
	//�������������ϼ���
	while (true) {
		acceptSocket = accept(ProxyServer, NULL, NULL); // ����������������¼�ͻ��˵ĵ�ַ�͵�ַ����, �Դ洢�û���ַ
		//printf("�����׽���: %d\n", count++);
		if (acceptSocket == INVALID_SOCKET) {
			printf("������ͻ������ݴ��͵��׽��ֽ���ʧ��, �������Ϊ: %d\n", WSAGetLastError());
			return 0;
		}
		lpProxyParam = new ProxyParam; // �ڶѿռ������ڴ�, ��Ϊ��new�����Ķ���. �������ṹ�����, ����ջ�з����ڴ�
		if (lpProxyParam == NULL) { // ����ѿռ�ʧ��, ����δ����
			printf("����ѿռ�ʧ��, �رս����׽���\n");
			closesocket(acceptSocket);
			continue;
		}
		lpProxyParam->clientSocket = acceptSocket;
		// �̵߳��ú���, ������������ʾ�߳̿�ʼ�ĺ�����ַ, ���ĸ�������ʾ���ò����б�
		// ������������̳߳�ʼ״̬, 0��ʾ����ִ��, �������������ڼ�¼�߳�ID��ַ
		hThread = (HANDLE)_beginthreadex(NULL, 0, &ProxyThread, (LPVOID)lpProxyParam, 0, (unsigned int *) & dwThreadID);
		if (hThread == NULL) {
			printf("���̴߳���ʧ��, �رս����׽���\n");
			closesocket(acceptSocket);
			continue;
		}
		// ʵ�������������û�йر��߳�, ֻ�ǽ��߳�hthead���ں˶������ü�����һ(��ʱΪ1). 
		// ���߳̽���ʱ, �ں˶������ü����ټ�һ(��ʱΪ0). ����ϵͳ�����߳���Դ.
		CloseHandle(hThread); // �ر��߳̾��
		Sleep(200); // ����0.2s
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
// Qualifier: ��ʼ���׽���
//***********************************
BOOL InitSocket() {
	//�����׽��ֿ⣨���룩
	WORD wVersionRequested; // WORD���޷�2�ֽ�short����
	WSADATA wsaData;
	//�׽��ּ���ʱ������ʾ
	int err;
	//�汾 2.2
	wVersionRequested = MAKEWORD(2, 2);
	//���� dll �ļ� Scoket ��
	err = WSAStartup(wVersionRequested, &wsaData);
	if (err != 0) {
		//�Ҳ��� winsock.dll
		printf("���� winsock ʧ�ܣ��������Ϊ: %d\n", WSAGetLastError());
		return FALSE;
	}
	if (LOBYTE(wsaData.wVersion) != 2 || HIBYTE(wsaData.wVersion) != 2)
	{
		printf("�����ҵ���ȷ�� winsock �汾\n");
		WSACleanup();
		return FALSE;
	}
	// windowsϵͳ��,af_inet��pf_inet��ȫһ��
	// ���ĵ���������ΪTCPЭ���
	struct protoent* p;
	p = getprotobyname("tcp");
	ProxyServer = socket(AF_INET, SOCK_STREAM, p->p_proto); 
	if (INVALID_SOCKET == ProxyServer) {
		printf("���������׽���ʧ�ܣ��������Ϊ��%d\n", WSAGetLastError());
		return FALSE;
	}
	ProxyServerAddr.sin_family = AF_INET;
	ProxyServerAddr.sin_port = htons(ProxyPort); // htons()����ת�������ֽ�˳��
	ProxyServerAddr.sin_addr.S_un.S_addr = INADDR_ANY;
	if (bind(ProxyServer, (SOCKADDR*)&ProxyServerAddr, sizeof(SOCKADDR)) == SOCKET_ERROR) {
		printf("�󶨼����׽���ʧ��\n");
		return FALSE;
	}
	if (listen(ProxyServer, SOMAXCONN) == SOCKET_ERROR) { // ������д�СΪ5
		printf("�����˿�%d ʧ��", ProxyPort);
		return FALSE;
	}
	return TRUE;
}

//************************************
// Method: ProxyThread
// FullName: ProxyThread
// Access: public 
// Returns: unsigned int __stdcall
// Qualifier: �߳�ִ�к���
// Parameter: LPVOID lpParameter
//************************************
unsigned int __stdcall ProxyThread(LPVOID lpParameter) { // warning: ʹ��ջ���ռ�65560�ֽ�, ջ���
	//char* Buffer = new char[MAXSIZE]; // �ڶѷ���ռ�, ��ֹջռ�ù���
	//int count = 0;
	char Buffer[MAXSIZE]; // ���ڽ��ܱ���
	char socketBuffer[50];
	char* CacheBuffer;
	ZeroMemory(Buffer, MAXSIZE); // �ڴ��ö�����0���, ���ַ������൱�������ַ�'\0'
	ZeroMemory(socketBuffer, 50);
	SOCKADDR_IN clientAddr; // ûʲô�ô�
	int length = sizeof(SOCKADDR_IN); // û��ʲô�ô�
	int recvSize = 0;
	int ret;
	int curindex = 0;

	recvSize = recv(((ProxyParam*)lpParameter)->clientSocket, socketBuffer, 50, 0); // ��һ������Ϊ���ն�socket������
	while (recvSize == 50) { // TCP��ʽ����
		//printf("�߳� %d ���յ������ݴ�СΪ: %d\n", GetCurrentThreadId(), recvSize);
		memcpy(&Buffer[curindex], socketBuffer, recvSize);
		curindex += recvSize;
		ZeroMemory(socketBuffer, 50);
		recvSize = recv(((ProxyParam*)lpParameter)->clientSocket, socketBuffer, 50, 0);
	}
	memcpy(&Buffer[curindex], socketBuffer, recvSize);
	curindex += recvSize;
	ZeroMemory(socketBuffer, 50);
	//if (recvSize < 0) { // �����ж�
	//	printf("���տͻ�������ʱ����, �̹߳ر�\n");
	//	goto error;
	//}


	HttpHeader* httpHeader = new HttpHeader();
	CacheBuffer = new char[curindex + 1]; // ���汨�ĸ���, ��һ���ֽ�Ϊ'\0'�൱��һ�����ַ���
	ZeroMemory(CacheBuffer, curindex + 1);
	memcpy(CacheBuffer, Buffer, curindex);
	ParseHttpHead(CacheBuffer, httpHeader); // ����url
	delete[] CacheBuffer;

	if (httpHeader->method[0] == '\0') { // ��ӦCONNECT����
		goto error;
	}
	if (!ConnectToServer(&((ProxyParam*)lpParameter)->serverSocket, httpHeader->host)) {
		printf("����δ��������, �̹߳ر�\n");
		goto error;
	}
	printf("������������ %s �ɹ�\n", httpHeader->host);

	//���ͻ��˷��͵� HTTP ���ݱ���ֱ��ת����Ŀ�������
	// ��һ������Ϊ�������ݵ��׽���������
	// ���һ������Ϊ����Ϊ����ʽ
	ret = send(((ProxyParam*)lpParameter)->serverSocket, Buffer, sizeof(Buffer), 0); 
	if (ret == -1) {
		printf("����ת�����ĸ�������ʧ��, �̹߳ر�\n");
		goto error;
	}

	ZeroMemory(Buffer, MAXSIZE);
	curindex = 0;

	//�ȴ�Ŀ���������������
	//recvSize = recv(((ProxyParam*)lpParameter)->serverSocket, socketBuffer, 50, 0); // ��һ������Ϊ���ն�socket������
	//while (recvSize == 50) { // TCP��ʽ����
	//	//printf("�߳� %d ���յ������ݴ�СΪ: %d\n", GetCurrentThreadId(), recvSize);
	//	memcpy(&Buffer[curindex], socketBuffer, recvSize);
	//	curindex += recvSize;
	//	ZeroMemory(socketBuffer, 50);
	//	recvSize = recv(((ProxyParam*)lpParameter)->serverSocket, socketBuffer, 50, 0);
	//}
	//if (recvSize <= 0) {
	//	printf("���շ���������ʱ����, �̹߳ر�\n");
	//	goto error;
	//}
	//memcpy(&Buffer[curindex], socketBuffer, recvSize);
	//curindex += recvSize;
	//ZeroMemory(socketBuffer, 50);
	recvSize = recv(((ProxyParam*)lpParameter)->serverSocket, Buffer, MAXSIZE, 0);
	if (recvSize <= 0) {
		printf("���շ���������ʱ����, �̹߳ر�\n");
		goto error;
	}
	
	//��Ŀ����������ص�����ֱ��ת�����ͻ���
	ret = send(((ProxyParam*)lpParameter)->clientSocket, Buffer, sizeof(Buffer), 0);
	//printf("���ʹ���: %d", count++);
	if (ret == -1) {
		printf("����ת�����ĸ��ͻ���ʧ��, �̹߳ر�\n");
		goto error;
	}
	// ������
	error:
		Sleep(200);
		closesocket(((ProxyParam*)lpParameter)->serverSocket);
		closesocket(((ProxyParam*)lpParameter)->clientSocket);
		//delete[] Buffer;
		delete lpParameter;
		_endthreadex(0); // �����closehandle(), ���������ֱ�ӹر��߳�, �ͷ�ռ�õ��ڴ�
		return 0;
}


//************************************
// Method: ParseHttpHead
// FullName: ParseHttpHead
// Access: public 
// Returns: void
// Qualifier: ���� TCP �����е� HTTP ͷ��
// Parameter: char * buffer
// Parameter: HttpHeader * httpHeader
//************************************
void ParseHttpHead(char* buffer, HttpHeader* httpHeader) {
	char* p;
	char* ptr = NULL;
	const char* delim = "\r\n";
	p = strtok_s(buffer, delim, &ptr);//��ȡ��һ��
	if (p[0] == 'G') {//GET ��ʽ
		printf("%s\n", p);
		memcpy(httpHeader->method, "GET", 3);
		memcpy(httpHeader->url, &p[4], strlen(p) - 13); // 13 = "GET  http/1.1"
	}
	else if (p[0] == 'P') {//POST ��ʽ
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
			memcpy(httpHeader->host, &p[6], strlen(p) - 6); // p[6]����ȥ"host: "��6���ַ�
			break;
		case 'C'://Cookie
			if (strlen(p) > 8) {
				char header[8];
				ZeroMemory(header, sizeof(header));
				memcpy(header, p, 6);
				if (!strncmp(header, "Cookie", 6)) { // ȷ����cookie�ײ���
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
// Qualifier: ������������Ŀ��������׽��֣�������
// Parameter: SOCKET * serverSocket
// Parameter: char * host
//************************************
BOOL ConnectToServer(SOCKET* serverSocket, char* host) { // �൱�ڿͻ���, ����ʡ����bind���ñ���ip�Ͷ˿�
	sockaddr_in serverAddr; // ����ԭ�������ĵ�ַ
	serverAddr.sin_family = AF_INET;
	serverAddr.sin_port = htons(HTTP_PORT);
	HOSTENT* hostent = gethostbyname(host); // ����->32λIP��ַ
	if (!hostent) {
		printf("ͨ��������ȡip��ַʧ��\n");
		return FALSE;
	}
	in_addr Inaddr = *((in_addr*)*hostent->h_addr_list);
	serverAddr.sin_addr.s_addr = inet_addr(inet_ntoa(Inaddr));

	struct protoent* p;
	p = getprotobyname("tcp");
	*serverSocket = socket(AF_INET, SOCK_STREAM, p->p_proto);
	if (*serverSocket == INVALID_SOCKET) {
		printf("�������������׽���ʧ�ܣ��������Ϊ��%d\n", WSAGetLastError());
		return FALSE;
	}
	if (connect(*serverSocket, (SOCKADDR*)&serverAddr, sizeof(serverAddr)) == SOCKET_ERROR) {
		printf("��������ʧ��, �رմ��������׽���\n");
		closesocket(*serverSocket);
		return FALSE;
	}
	return TRUE;
}