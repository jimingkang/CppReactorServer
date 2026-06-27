#include "WindowsServer.h"


using namespace common;
using namespace recycle;

namespace tcp
{

	IServer* tcp::NewIServer()
	{
		return new WindowsServer();
	}




	

	WindowsServer::WindowsServer()
	{
		m_AcceptEx        = NULL;
		m_GetAcceptEx     = NULL;
		m_ListenSocket    = INVALID_SOCKET;
		m_Completeport    = NULL;
		m_ConnectCount    = 0;
		m_SecurityCount   = 0;
		m_IsRunning       = false;
		m_Onlines         = NULL;
		m_OnlinesIndexs   = NULL;

		m_Notify_Accept     = NULL;
		m_Notify_Secure     = NULL;
		m_Notify_Disconnect = NULL;
		m_Notify_Command    = NULL;
	}
	WindowsServer::~WindowsServer()
	{
	}

	void WindowsServer::StartServer()
	{
		//1、创建连接用户
		m_Onlines = new common::HashContainer<S_CONNECT_BASE>(common::ServerXML->appMaxConnect);
		for (int i = 0; i < m_Onlines->Count(); i++)
		{
			auto c = m_Onlines->Value(i);
			c->Init();
		}
		//2、创建连接用户索引
		m_OnlinesIndexs = new common::HashContainer<S_CONNECT_INDEX>(MAX_SOCKETFD_LEN);
		for (int i = 0; i < m_OnlinesIndexs->Count(); i++)
		{
			auto index = m_OnlinesIndexs->Value(i);
			index->Reset();
		}
		//3、初始化socket
		int ret = InitSocket();
		if (ret < 0)
		{
			LOGINFO("InitSocket err...%d \n", ret);
			if (m_Completeport != NULL && m_Completeport != INVALID_HANDLE_VALUE)
			{
				CloseHandle(m_Completeport);
				m_Completeport = INVALID_HANDLE_VALUE;
			}
			if (m_ListenSocket != INVALID_SOCKET) 
			{ 
				closesocket(m_ListenSocket); 
				m_ListenSocket = INVALID_SOCKET; 
			}
			if (ret != -2) WSACleanup();
			return;
		}
		//4、初始化投递数据
		InitAccept();
		//5、开始运行线程
		InitThread();
	}
	void WindowsServer::StopServer()
	{
	}


	int WindowsServer::InitSocket()
	{
		//1、创建完成端口
		m_Completeport = CreateIoCompletionPort(INVALID_HANDLE_VALUE, NULL, 0, 0);
		if (m_Completeport == NULL) return -1;

		//2、加载套接字库
		WSADATA wsData;
		int errcode = WSAStartup(MAKEWORD(2, 2), &wsData);
		if (errcode != 0) return -2;

		//3、创建套接字
		m_ListenSocket = WSASocket(AF_INET, SOCK_STREAM, 0, NULL, 0, WSA_FLAG_OVERLAPPED);
		if (m_ListenSocket == INVALID_SOCKET) return -3;

		//设置非阻塞模式
		unsigned long ul = 1;
		errcode = ioctlsocket(m_ListenSocket, FIONBIO, (unsigned long*)& ul);
		if (errcode == SOCKET_ERROR) return -4;
		//关闭监听的发送接收缓冲区
		int buffersize = 0;
		setsockopt(m_ListenSocket, SOL_SOCKET, SO_RCVBUF, (char*)& buffersize, sizeof(int));
		setsockopt(m_ListenSocket, SOL_SOCKET, SO_SNDBUF, (char*)& buffersize, sizeof(int));
		//4、绑定完成端口
		HANDLE handle = CreateIoCompletionPort((HANDLE)m_ListenSocket, m_Completeport, 0, 0);
		if (handle == NULL) return -5;

		//5、绑定到IP和端口
		sockaddr_in serAddr;
		serAddr.sin_family = AF_INET;
		serAddr.sin_port   = htons(common::ServerXML->appPort);
		serAddr.sin_addr.S_un.S_addr = INADDR_ANY;

		errcode = ::bind(m_ListenSocket, (struct sockaddr*) & serAddr, sizeof(serAddr));
		if (errcode == SOCKET_ERROR) return -6;
		//6、监听socket 已经完成三次握手的队列数量
		errcode = ::listen(m_ListenSocket, SOMAXCONN);
		if (errcode == SOCKET_ERROR) return -7;


		GUID GuidAcceptEx = WSAID_ACCEPTEX;
		GUID GuidGetAcceptEx = WSAID_GETACCEPTEXSOCKADDRS;
		DWORD dwBytes = 0;


		//1你需要控制的套接口的句柄。
		//2将进行的操作的控制代码，即你所需要操控的类型
		//3输入缓冲区的地址（如果你想指定一个函数对套接字进行控制，可以是一个guid，指定这个控制函数的guid）。
		//4输入缓冲区的大小（这里为guid的大小，即sizeof(&guid)）。
		//5输出缓冲区的地址（这里即是函数指针的地址）。
		//6输出缓冲区的大小（函数指针的大小）。
		//7输出实际字节数的地址。
		//8WSAOVERLAPPED结构的地址（一般为NULL）。
		//9一个指向操作结束后调用的例程指针（一般为NULL）。
		if (m_AcceptEx == NULL)
		{
			errcode = WSAIoctl(m_ListenSocket,
								SIO_GET_EXTENSION_FUNCTION_POINTER,
								&GuidAcceptEx,
								sizeof(GuidAcceptEx),
								&m_AcceptEx,
								sizeof(m_AcceptEx),
								&dwBytes,
								NULL,
								NULL);
		}

		if (m_AcceptEx == NULL || errcode == SOCKET_ERROR) return -8;

		if (m_GetAcceptEx == NULL)
		{
			errcode = WSAIoctl(m_ListenSocket,
								SIO_GET_EXTENSION_FUNCTION_POINTER,
								&GuidGetAcceptEx,
								sizeof(GuidGetAcceptEx),
								&m_GetAcceptEx,
								sizeof(m_GetAcceptEx),
								&dwBytes,
								NULL,
								NULL);
		}

		if (m_GetAcceptEx == NULL || errcode == SOCKET_ERROR) return -9;

		return 0;
	}

	void WindowsServer::InitAccept()
	{
		for (int i = 0; i < ServerXML->maxPostAccept;i++)
		{
			Post_Accept();
		}
	}
	//投递新的连接
	int WindowsServer::Post_Accept()
	{
		SOCKET socketfd = WSASocket(AF_INET, SOCK_STREAM, 0, NULL, 0, WSA_FLAG_OVERLAPPED);
		if(socketfd == INVALID_SOCKET) return -1;

		ULONG ul = 1;
		int errcode = ioctlsocket(socketfd, FIONBIO, (unsigned long*)& ul);
		if (errcode == SOCKET_ERROR)
		{
			ReleaseSocket(socketfd, NULL, 201);
			return -2;
		}
		auto  context = PostAcceptRecycle::Pop();
		context->m_PostSocket = socketfd;
//1、监听socket
//2、接受连接socket
//3、接受缓冲区 a、客户端发来第一组数据 b、server地址 c、client地址
//4、0不会等到数据到来直接返回 非0等待数据
//5、本地地址大小；长度必须为地址长度 + 16字节
//6、远端地址大小；长度必须为地址长度 + 16字节
//7、同步方式才有用 我们是异步IO没用，不用管；
//8、本次重叠I / O所要用到的重叠结构

		unsigned long dwBytes = 0;
		errcode = m_AcceptEx(m_ListenSocket,
							context->m_PostSocket,
							context->m_buf,
							0,
							sizeof(SOCKADDR_IN) + 16,
							sizeof(SOCKADDR_IN) + 16,
							&dwBytes,
							&context->m_OverLapped);
		if (errcode == false)
		{
			int error = WSAGetLastError();
			if (ERROR_IO_PENDING != error)
			{
				ReleaseSocket(socketfd, NULL, 202);
				PostAcceptRecycle::Push(context);
				return -3;
			}
	    }

		return 0;
	}
	//通知-有新的连接
	int WindowsServer::Event_Accept(void* context, int tid)
	{
		PostAcceptRecycle* acc = (PostAcceptRecycle*)context;
		if (acc == NULL) return -1;

		SOCKADDR_IN* ClientAddr = NULL;
		SOCKADDR_IN* LocalAddr = NULL;
		int remoteLen = sizeof(SOCKADDR_IN);
		int localLen = sizeof(SOCKADDR_IN);
		int errcode = 0;

//1、指向传递给AcceptEx函数接收第一块数据的缓冲区
//2、缓冲区大小，必须和传递给AccpetEx函数的一致
//3、本地地址大小，必须和传递给AccpetEx函数一致
//4、远程地址大小，必须和传递给AccpetEx函数一致
//5、用来返回连接的本地地址
//6、用来返回本地地址的长度
//7、用来返回远程地址
//8、用来返回远程地址的长度

		m_GetAcceptEx(acc->m_buf,
			            0,
						sizeof(SOCKADDR_IN) + 16,
						sizeof(SOCKADDR_IN) + 16,
						(LPSOCKADDR*)& LocalAddr, 
						&localLen,
						(LPSOCKADDR*)& ClientAddr, 
						&remoteLen);

		setsockopt(acc->m_PostSocket, SOL_SOCKET, SO_UPDATE_ACCEPT_CONTEXT, (char*)& m_ListenSocket, sizeof(m_ListenSocket));

		//2、设置发送 接收数据缓冲区
		int recvone = ServerXML->recvBytesOne;
		int sendone = ServerXML->sendBytesOne;
		setsockopt(m_ListenSocket, SOL_SOCKET, SO_RCVBUF, (char*)& recvone, sizeof(recvone));
		setsockopt(m_ListenSocket, SOL_SOCKET, SO_SNDBUF, (char*)& sendone, sizeof(sendone));

		if (this->SetWindowsHeart(acc->m_PostSocket) < 0) return -2;

		//绑定完成端口
		HANDLE hTemp = CreateIoCompletionPort((HANDLE)acc->m_PostSocket, m_Completeport, 0, 0);
		if (hTemp == NULL) return -3;

		S_CONNECT_INDEX* cindex = FindOnlinesIndex(acc->m_PostSocket);
		if (cindex == NULL) return -4;

		S_CONNECT_BASE* c = FindNoStateData();
		if (c == NULL)
		{
			LOGINFO("server full... \n");
			return -5;
		}

		cindex->index = c->index;
		memcpy(c->ip, inet_ntoa(ClientAddr->sin_addr), MAX_IP_LEN);
		c->socketfd = acc->m_PostSocket;
		c->port = ntohs(ClientAddr->sin_port);
		c->state = common::E_SSS_Connect;
		c->temp_ConnectTime = (int)time(NULL);
		c->temp_HeartTime   = (int)time(NULL);

		//投递接收数据
		int ret = Post_Recv(c->socketfd);
		if (ret != 0)
		{
			c->Reset();
			cindex->Reset();
			return -5;
		}

		ComputeConnectNum(true);
		//给客户端发送一条数据包 异或码
		srand(time(NULL));
		uint8_t value = rand() % 125 + 1;
		S_CMD_XOR  code;
		code.xorCode = value ^ c->xorCode;

		CreatePackage(c->index, CMD_XOR, &code, sizeof(S_CMD_XOR));
		c->xorCode = value;


		if (m_Notify_Accept != nullptr) m_Notify_Accept(this, c, 0);
		PostAcceptRecycle::Push(acc);

		this->Post_Accept();

		return 0;
	}
	//投递接收数据
	int WindowsServer::Post_Recv(SOCKET s)
	{
		PostRecvRecycle* context = PostRecvRecycle::Pop();
		context->m_PostSocket = s;
		unsigned long bytes = 0;
		unsigned long flag = 0;
		//1、 操作的套接字
		//2、 接收缓冲区
		//3、 wsaBuf数组中WSABUF结构的数目
		//4、 如果接收操作立即完成,返回函数调用所接收到的字节数
		//5、 用来控制套接字的行为 一般设置为0
		//6、 重叠结构
		//7、 一个指向接收操作结束后调用的例程的指针
		int err = WSARecv(context->m_PostSocket,
							&context->m_wsaBuf,
							1,
							&bytes,
							&flag,
							&context->m_OverLapped,
							NULL);
		if (SOCKET_ERROR == err)
		{
			int error = WSAGetLastError();
			if (error != WSA_IO_PENDING)
			{
				PostRecvRecycle::Push(context);
				return -1;
			}
		}

		return 0;
	}
	//收到通知 新的数据到来
	int WindowsServer::Event_Recv(void* context, int recvBytes, int tid)
	{
		PostRecvRecycle* rece = (PostRecvRecycle*)context;
		if (rece == NULL) return -1;

		auto c = FindClient((int)rece->m_PostSocket, true);
		if (c == NULL)
		{
			ShutDownSocket(rece->m_PostSocket, 0, NULL, 3001);
			PostRecvRecycle::Push(rece);
			return -2;
		}

		if (c->recvs.head == c->recvs.tail)
		{
			c->recvs.tail = 0;
			c->recvs.head = 0;
		}

		if (c->recvs.tail + recvBytes >= ServerXML->recvBytesMax)
		{
			c->recvs.isCompleted = true;
			ShutDownSocket(rece->m_PostSocket, 0, NULL, 3002);
			PostRecvRecycle::Push(rece);
			return -2;
		}

		memcpy(&c->recvs.buf[c->recvs.tail], rece->m_wsaBuf.buf, recvBytes);
		c->recvs.tail += recvBytes;

		int ret = Post_Recv(rece->m_PostSocket);
		if (ret < 0)
		{
			c->recvs.isCompleted = true;
			ShutDownSocket(rece->m_PostSocket, 0, NULL, 3003);
			PostRecvRecycle::Push(rece);
			return -2;
		}

		c->recvs.isCompleted = true;
		PostRecvRecycle::Push(rece);
		return 0;
	}
	//投递发送数据
	int WindowsServer::Post_Send(S_CONNECT_BASE* c)
	{
		if (!c->sends.isCompleted) return -1;
		if (c->index < 0 || c->state == E_SSS_Free || c->closeState == E_SSC_ShutDown || c->socketfd == INVALID_SOCKET) return -2;

		if (c->sends.tail <= c->sends.head) return 0;

		int sendBytes = c->sends.tail - c->sends.head;
		if (sendBytes > ServerXML->sendBytesOne) sendBytes = ServerXML->sendBytesOne;
		c->sends.isCompleted = false;

		PostSendRecycle* context = PostSendRecycle::Pop();
		context->SetPostSend(c->socketfd, &c->sends.buf[c->sends.head], sendBytes);
		//1、 操作的套接字
		//2、 接收缓冲区
		//3、 wsaBuf数组中WSABUF结构的数目
		//4、 如果接收操作立即完成,返回函数调用所接收到的字节数
		//5、 用来控制套接字的行为 一般设置为0
		//6、 重叠结构
		//7、 一个指向接收操作结束后调用的例程的指针

		unsigned long dwBytes = 0;
		int err = WSASend(context->m_PostSocket,
							&context->m_wsaBuf,
							1,
							&dwBytes,
							0,
							&context->m_OverLapped,
							NULL);
		if (err == SOCKET_ERROR)
		{
			int error = WSAGetLastError();
			if (error != WSA_IO_PENDING)
			{
				ShutDownSocket(context->m_PostSocket, 0, NULL, 4001);
				PostSendRecycle::Push(context);
				return -3;
			}
		}

		return 0;
	}
	//完成端口通知 发送数据成功
	int WindowsServer::Event_Send(void* context, int sendBytes, int tid)
	{
		PostSendRecycle* sc = (PostSendRecycle*)context;
		if (sc == NULL) return -1;

		if (sc->m_wsaBuf.len != sendBytes)
		{
			ShutDownSocket(sc->m_PostSocket, 0, NULL, 4002);
			PostSendRecycle::Push(sc);
			return -1;
		}

		auto c = FindClient(sc->m_PostSocket, true);
		if (c == NULL)
		{
			ShutDownSocket(sc->m_PostSocket, 0, NULL, 4003);
			PostSendRecycle::Push(sc);
			return -1;
		}

		if (c->index < 0 || c->state == E_SSS_Free || c->closeState == E_SSC_ShutDown || c->socketfd == INVALID_SOCKET)
		{
			c->sends.isCompleted = true;
			PostSendRecycle::Push(sc);
			return -2;
		}

		//发送成功
		c->sends.head += sendBytes;
		c->sends.isCompleted = true;
		PostSendRecycle::Push(sc);
		return 0;
	}




	

	int32_t WindowsServer::ReleaseSocket(SOCKET socketfd, S_CONNECT_BASE* c, int kind)
	{
		if (socketfd == SOCKET_ERROR || socketfd == INVALID_SOCKET) return -1;
		if (c != nullptr)
		{
			if (c->state == common::E_SSS_Free) return 0;
			if (c->state >= common::E_SSS_Secure)
			{
				this->ComputeSecureNum(false);
			}
		}
		switch (kind)
		{
		case 101:
		case 201:
		case 202:
			if (socketfd != INVALID_SOCKET)
			{
				closesocket(socketfd);
				socketfd = INVALID_SOCKET;
			}
			break;
		default:
			this->ComputeConnectNum(false);
			shutdown(socketfd, SD_BOTH);
			if (socketfd != INVALID_SOCKET)
			{
				closesocket(socketfd);
				socketfd = INVALID_SOCKET;
			}
			break;
		}

		if (m_Notify_Disconnect != nullptr) this->m_Notify_Disconnect(this, c, kind);

		return 0;
	}

	void WindowsServer::ShutDownSocket(SOCKET s, const int32_t mode, S_CONNECT_BASE* c, int kind)
	{
		if (c != nullptr)
		{
			if (c->state == common::E_SSS_Free) return;
			if (c->closeState == common::E_SSC_ShutDown) return;

			c->temp_ShutDown = kind;
			c->temp_CloseTime = (int)time(NULL);
			c->closeState = common::E_SSC_ShutDown;
			shutdown(s, SD_BOTH);

			CancelIoEx((HANDLE)s, nullptr);
			return;
		}

		auto c2 = FindClient(s, true);
		if (c2 == nullptr)
		{
			return;
		}
		if (c2->state == common::E_SSS_Free) return;
		if (c2->closeState == common::E_SSC_ShutDown) return;

		switch (mode)
		{
		case common::E_CT_Recv:
			c2->recvs.isCompleted = true;
			break;
		case common::E_CT_Send:
			c2->sends.isCompleted = true;
			break;
		}

		c2->temp_ShutDown = kind;
		c2->temp_CloseTime = (int)time(NULL);
		c2->closeState = common::E_SSC_ShutDown;
		shutdown(s, SD_BOTH);

	}
//************************************************************************
//************************************************************************
//************************************************************************
//************************************************************************
//************************************************************************
//线程
	void WindowsServer::InitThread()
	{
		m_IsRunning = true;
		if (common::ServerXML->maxThread <= 0) common::ServerXML->maxThread = 1;
		else if (common::ServerXML->maxThread > MAX_THREAD_LEN) common::ServerXML->maxThread = MAX_THREAD_LEN;
		for (int i = 0; i < common::ServerXML->maxThread; i++)
		{
			m_WorkThread[i].reset(new std::thread(WindowsServer::RunThread, this, i));
		}

		for (int i = 0; i < common::ServerXML->maxThread; i++)
			m_WorkThread[i]->detach();
	}



	void pushContext(RecycleBase* context)
	{
		switch (context->m_PostType)
		{
		case common::E_CT_Accept:
			PostAcceptRecycle::Push((PostAcceptRecycle*)context);
			break;
		case common::E_CT_Recv:
			PostRecvRecycle::Push((PostRecvRecycle*)context);
			break;
		case common::E_CT_Send:
			PostSendRecycle::Push((PostSendRecycle*)context);
			break;
		}
	}

	void WindowsServer::RunThread(WindowsServer* tcp, int id)
	{
		LOGINFO("RunThread...%d \n", id);

		ULONG_PTR key = 1;
		OVERLAPPED* overlapped = nullptr;
		DWORD recvBytes = 0;

		while (tcp->m_IsRunning)
		{
			bool iscomplete = GetQueuedCompletionStatus(tcp->CompletePort(), &recvBytes, &key, &overlapped, INFINITE);

			recycle::RecycleBase* context = CONTAINING_RECORD(overlapped, recycle::RecycleBase, m_OverLapped);
			if (context == nullptr)
			{
				if (key == 1) break;
				else continue;
			}

			if (iscomplete == false)
			{
				DWORD err = GetLastError();
				if (WAIT_TIMEOUT == err) continue;

				if (overlapped != NULL)
				{
					tcp->ShutDownSocket(context->m_PostSocket, context->m_PostType, NULL, 2001);
					pushContext(context);
					continue;
				}
				tcp->ShutDownSocket(context->m_PostSocket, context->m_PostType, NULL, 2002);
				pushContext(context);
				continue;
			}
			else
			{
				if (overlapped == NULL)
				{
					LOGINFO("overlapped == NULL \n");
					break;
				}
				if ((iscomplete == true) && (overlapped != NULL))//正常操作
				{
					if (key != 0)
					{
						LOGINFO("key !=0 \n");
						continue;
					}

					if (recvBytes == 0 && (context->m_PostType == E_CT_Recv || context->m_PostType == E_CT_Send))
					{
						tcp->ShutDownSocket(context->m_PostSocket, context->m_PostType, NULL, 2003);
						pushContext(context);
						continue;
					}
					switch (context->m_PostType)
					{
					case common::E_CT_Accept://新的连接
						{
							auto acc = (PostAcceptRecycle*)context;
							int err = tcp->Event_Accept(acc, id);
							if (err != 0)
							{
								tcp->ReleaseSocket(acc->m_PostSocket, NULL, 101);
								PostAcceptRecycle::Push(acc);
								tcp->Post_Accept();
							}
						}
						break;
					case common::E_CT_Recv://有新的数据
						tcp->Event_Recv(context, (int)recvBytes, id);
						break;
					case common::E_CT_Send://发送数据成功
						tcp->Event_Send(context, (int)recvBytes, id);
						break;
					}
				}


			}

		}

		LOGINFO("exit Thread...%d \n", id);
	}





}
