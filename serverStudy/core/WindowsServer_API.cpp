#include "WindowsServer.h"


namespace tcp
{

	void WindowsServer::Update()
	{
		for (int i = 0; i < m_Onlines->Count(); i++)
		{
			auto c = m_Onlines->Value(i);
			if (c->index == -1) continue;
			if (c->state == common::E_SSS_Free) continue;
			if (c->state == common::E_SSS_NeedSave) continue;

			UpdateDisconnect(c);
			if (c->closeState == common::E_SSC_ShutDown) continue;
			//解析指令
			ReadPackage_Head(c);
			//发送数据
			this->Post_Send(c);
		}
	}

	void WindowsServer::UpdateDisconnect(S_CONNECT_BASE* c)
	{
		//1、检查安全关闭
		int temp = 0;
		if (c->closeState == common::E_SSC_ShutDown)
		{
			temp = (int)time(NULL) - c->temp_CloseTime;
			if (c->recvs.isCompleted && c->sends.isCompleted)
			{
				ReleaseSocket(c->socketfd, c, 1001);
			}
			else if (temp > 1)
			{
				ReleaseSocket(c->socketfd, c, 1002);
			}
			return;
		}
		//2、检查安全连接
		temp = (int)time(NULL) - c->temp_ConnectTime;
		if (c->state == common::E_SSS_Connect)
		{
			if (temp > 10)
			{
				ShutDownSocket(c->socketfd, 0, c, 1001);
				return;
			}
		}
		//3、检查心跳连接
		temp = (int)time(NULL) - c->temp_HeartTime;
		if (temp > common::ServerXML->maxHeartTime)
		{
			ShutDownSocket(c->socketfd, 0, c, 1002);
			return;
		}

	}
	//解析消息头
	void WindowsServer::ReadPackage_Head(S_CONNECT_BASE* c)
	{
		if (!c->recvs.isCompleted) return;

		for (int i = 0; i < 1000; i++)
		{
			int len = c->recvs.tail - c->recvs.head;
			if (len < 8) break;
			char head[2];
			head[0] = c->recvs.buf[c->recvs.head] ^ c->xorCode;
			head[1] = c->recvs.buf[c->recvs.head + 1] ^ c->xorCode;
			if (head[0] != common::ServerXML->Head[0] || head[1] != common::ServerXML->Head[1])
			{
				ShutDownSocket(c->socketfd, 0, c, 6001);
				return;
			}

			uint32_t  length = (*(uint32_t*)(c->recvs.buf + c->recvs.head + 2)) ^ c->xorCode;
			uint16_t  cmd = (*(uint16_t*)(c->recvs.buf + c->recvs.head + 6)) ^ c->xorCode;

			if (c->recvs.tail < c->recvs.head + length) break;
			c->packageLength = length;

			ReadPackage_Command(c, cmd);

			if (c->state < common::E_SSS_Connect)
			{
				return;
			}

			//偏移length
			c->recvs.head += length;
		}

	}

	void WindowsServer::ReadPackage_Command(S_CONNECT_BASE* c, uint16_t cmd)
	{
		c->temp_HeartTime = (int)time(NULL);
		if (cmd < 65000)
		{
			if (this->m_Notify_Command != nullptr) m_Notify_Command(this, c, cmd);
			return;
		}

		switch (cmd)
		{
		case CMD_HEART:
			CreatePackage(c->index, CMD_HEART, NULL, 0);
			break;
		case CMD_SECURITY: //安全连接
			{
				char md5[MAX_MD5_LEN];
				char a[20];
				sprintf_s(a, "%s_%d", common::ServerXML->SecureCode, c->xorCode);
				memset(md5, 0, MAX_MD5_LEN);

				if (common::MD5_FunPoint != NULL) common::MD5_FunPoint(md5, (unsigned char*)a, strlen(a));

				S_CMD_SECURE secure;
				memset(&secure, 0, sizeof(S_CMD_SECURE));

				ReadPackage(c->index, &secure, sizeof(S_CMD_SECURE));
				if (secure.appVersion != common::ServerXML->appVersion)
				{
					S_CMD_RESULT  kind;
					kind.type = 1;

					CreatePackage(c->index, CMD_SECURITY, &kind, sizeof(S_CMD_RESULT));
					return;
				}
				int error = stricmp(md5, secure.appMD5);
				if(error != 0)
				{
					S_CMD_RESULT  kind;
					kind.type = 2;

					CreatePackage(c->index, CMD_SECURITY, &kind, sizeof(S_CMD_RESULT));
					return;
				}
				c->appID = secure.appID;
				c->state = common::E_SSS_Secure;
				S_CMD_RESULT  kind;
				kind.type = 0;
				CreatePackage(c->index, CMD_SECURITY, &kind, sizeof(S_CMD_RESULT));
				
				this->ComputeSecureNum(true);
				if (m_Notify_Secure != NULL) m_Notify_Secure(this, c, 0);
			}
		}

	}
	//解包消息体
	void WindowsServer::ReadPackage(const int index, void* v, const int len)
	{
		auto c = FindClient(index);
		if (c == nullptr) return;

		uint32_t temp_head = c->recvs.head + 8;
		uint32_t temp_tail = c->recvs.head + c->packageLength;

		if (c->index == -1 ||
			c->state <= 0 ||
			c->recvs.buf == nullptr ||
			temp_head + len >= common::ServerXML->recvBytesMax ||
			temp_head + len > temp_tail)
		{
			return;
		}

		memcpy(v, &c->recvs.buf[temp_head], len);
	}
	void WindowsServer::CreatePackage(const int index, const uint16_t cmd, void* v, const int len)
	{
		auto c = FindClient(index);
		if (c == nullptr) return;

		if (c->state <= 0 ||
			c->socketfd == INVALID_SOCKET ||
			c->sends.tail + 8 + len >= common::ServerXML->sendBytesMax)
		{
			ShutDownSocket(c->socketfd, 0, c, 5001);
			return;
		}
		if (c->sends.head == c->sends.tail)
		{
			c->sends.tail = 0;
			c->sends.head = 0;
		}

		int tail = c->sends.tail;

		//1、设置头
		c->sends.buf[tail + 0] = common::ServerXML->Head[0] ^ c->xorCode;
		c->sends.buf[tail + 1] = common::ServerXML->Head[1] ^ c->xorCode;
		//2、设置数据包长度
		uint32_t length = (8 + len) ^ c->xorCode;
		char* p = (char*)& length;
		for (int i = 0; i < 4; i++)  c->sends.buf[tail + 2 + i] = p[i];
		p = NULL;
		//3、设置头指令
		uint16_t newcmd = cmd ^ c->xorCode;
		p = (char*)& newcmd;
		for (int i = 0; i < 2; i++)  c->sends.buf[tail + 6 + i] = p[i];
		tail += 8;

		if (len > 0 && v != NULL)
		{
			memcpy(&c->sends.buf[tail], v, len);
			tail += len;
		}


		c->sends.tail = tail;
	}
	//***********************************************************************
	//***********************************************************************
	//***********************************************************************
	//***********************************************************************

	S_CONNECT_BASE* WindowsServer::FindNoStateData()
	{
		std::lock_guard<std::mutex> guard(this->m_Mutex_NoState);
		
		for (int i = 0; i < m_Onlines->Count(); i++)
		{
			auto c = m_Onlines->Value(i);
			if (c->state == common::E_SSS_Free)
			{
				c->Reset();
				c->index = i;
				c->state = common::E_SSS_Connect;
				return c;
			}

		}
		return nullptr;
	}

	S_CONNECT_INDEX* WindowsServer::FindOnlinesIndex(const int socketfd)
	{
		if (socketfd < 0 || socketfd >= MAX_SOCKETFD_LEN) return nullptr;
		auto index = m_OnlinesIndexs->Value(socketfd);
		return index;
	}

	//自定义的结构体,用于TCP服务器
	typedef struct tcp_keepalive
	{
		unsigned long onoff;
		unsigned long keepalivetime;
		unsigned long keepaliveinterval;
	}TCP_KEEPALIVE, * PTCP_KEEPALIVE;

	//用于检测突然断线,只适用于windows 2000后平台
	//即客户端也需要win2000以上平台
	int WindowsServer::SetWindowsHeart(SOCKET s)
	{
		DWORD dwError = 0L, dwBytes = 0;
		TCP_KEEPALIVE sKA_Settings = { 0 }, sReturned = { 0 };
		sKA_Settings.onoff = 1;
		sKA_Settings.keepalivetime = 5500; // Keep Alive in 5.5 sec.
		sKA_Settings.keepaliveinterval = 1000; // Resend if No-Reply

		DWORD SIO_KEEPALIVE_VALS = IOC_IN | IOC_VENDOR | 4;

		dwError = WSAIoctl(s,
			SIO_KEEPALIVE_VALS,
			&sKA_Settings, sizeof(sKA_Settings),
			&sReturned, sizeof(sReturned),
			&dwBytes,
			NULL,
			NULL);
		if (dwError == SOCKET_ERROR)
		{
			dwError = WSAGetLastError();
			LOGINFO("SetHeartCheck->WSAIoctl()发生错误,错误代码: %ld  \n", dwError);
			return -1;
		}
		return 0;
	}

	void WindowsServer::ComputeSecureNum(bool isadd)
	{
		std::lock_guard<std::mutex> guard(this->m_Mutex_SecureCount);

		if (isadd)
			m_SecurityCount++;
		else
			m_SecurityCount--;
	}

	void WindowsServer::ComputeConnectNum(bool isadd)
	{
		std::lock_guard<std::mutex> guard(this->m_Mutex_ConnectCount);

		if (isadd)
			m_ConnectCount++;
		else
			m_ConnectCount--;
	}



	bool WindowsServer::IsCloseClient(const int index, int secure)
	{
		if (index < 0 || index >= m_Onlines->Count()) return false;

		auto c = m_Onlines->Value(index);
		if (c == NULL) return false;
		if (c->state >= secure) return false;

		ShutDownSocket(c->socketfd, 0, c, 7001);
		return true;
	}

	S_CONNECT_BASE* WindowsServer::FindClient(const int socketfd, bool issecure)
	{
		if (socketfd < 0 || socketfd >= MAX_SOCKETFD_LEN) return nullptr;
		auto cindex = m_OnlinesIndexs->Value(socketfd);
		if (cindex == nullptr) return nullptr;
		if (cindex->index < 0) return nullptr;

		auto c = FindClient(cindex->index);
		if (c == nullptr)
		{
			return nullptr;
		}
		if (issecure)
		{
			if (!c->IsEqual(socketfd)) return nullptr;
		}

		return c;
	}

	S_CONNECT_BASE* WindowsServer::FindClient(const int index)
	{
		if (index < 0 || index >= m_Onlines->Count()) return nullptr;
		auto c = m_Onlines->Value(index);
		return c;
	}




	



	void WindowsServer::SetNotify_Connect(ISERVER_NOTIFY e)
	{
		m_Notify_Accept = e;
	}

	void WindowsServer::SetNotify_Secure(ISERVER_NOTIFY e)
	{
		m_Notify_Secure = e;
	}

	void WindowsServer::SetNotify_DisConnect(ISERVER_NOTIFY e)
	{
		m_Notify_Disconnect = e;
	}

	void WindowsServer::SetNotify_Command(ISERVER_NOTIFY e)
	{
		m_Notify_Command = e;
	}
}