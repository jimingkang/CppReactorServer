#ifndef  ____WINDOWSERVER_H
#define  ____WINDOWSERVER_H

#include "ITcp.h"
#include <mutex>
#include <thread>
#include  <MSWSock.h>
//作者QQ：105153739 提供指导学习交流

#define ACCEPT_BUF_LENGTH  ((sizeof(struct sockaddr_in) + 16))*2

namespace recycle
{
	class RecycleBase
	{
	public:
		RecycleBase();
		~RecycleBase();
	public:
		WSAOVERLAPPED m_OverLapped;//重叠结构 理解ID
		SOCKET        m_PostSocket;
		int           m_PostType;
	};
	//1、投递新的连接
	class PostAcceptRecycle :public RecycleBase
	{
	public:
		PostAcceptRecycle(int type);
		~PostAcceptRecycle();
	public:
		unsigned char m_buf[ACCEPT_BUF_LENGTH];
		void Clear();
		static PostAcceptRecycle* Pop();
		static void Push(PostAcceptRecycle* acc);
	};
	//2、投递新的接收数据
	class PostRecvRecycle :public RecycleBase
	{
	public:
		PostRecvRecycle(int type);
		~PostRecvRecycle();
	private:
		char*  m_Buffs;
	public:
		WSABUF m_wsaBuf;//每次的操作缓冲区
		void Clear();
		static PostRecvRecycle* Pop();
		static void Push(PostRecvRecycle* acc);
	};
	//3、投递新的发送数据
	class PostSendRecycle :public RecycleBase
	{
	public:
		PostSendRecycle(int type);
		~PostSendRecycle();
	private:
		char* m_Buffs;//WSABUF里具体字符的缓冲区
	public:
		WSABUF m_wsaBuf;//每次的操作缓冲区
		void Clear();
		int SetPostSend(SOCKET s, char* data, const int sendByte);
		static PostSendRecycle* Pop();
		static void Push(PostSendRecycle* acc);
	};
}


namespace tcp
{
	class WindowsServer : public IServer
	{
	private:
		int   InitSocket();
		void  InitAccept();
		void  InitThread();

		int Post_Accept();
		int Event_Accept(void* context, int tid);
		int Post_Recv(SOCKET s);
		int Event_Recv(void* context, int recvBytes, int tid);
		int Post_Send(S_CONNECT_BASE* c);
		int Event_Send(void* context, int sendBytes, int tid);

		void  ReadPackage_Head(S_CONNECT_BASE* c);
		void  ReadPackage_Command(S_CONNECT_BASE* c, uint16_t cmd);
		void  UpdateDisconnect(S_CONNECT_BASE* c);

		int32_t ReleaseSocket(SOCKET socketfd, S_CONNECT_BASE* c, int kind);
		void ShutDownSocket(SOCKET s, const int32_t mode, S_CONNECT_BASE* c, int kind);


		S_CONNECT_BASE*    FindNoStateData();
		S_CONNECT_INDEX*   FindOnlinesIndex(const int socketfd);

		int  SetWindowsHeart(SOCKET s);
		void ComputeSecureNum(bool isadd);
		void ComputeConnectNum(bool isadd);
		inline HANDLE CompletePort() { return m_Completeport; }

		static void RunThread(WindowsServer* tcp, int id);
	public:
		WindowsServer();
		virtual ~WindowsServer();

		virtual inline int ConnnectCount() { return m_ConnectCount; };
		virtual inline int SecureCount()   { return m_SecurityCount; };

		virtual void StartServer();
		virtual void StopServer();

		virtual bool IsCloseClient(const int index, int secure);
		virtual S_CONNECT_BASE* FindClient(const int socketfd, bool issecure);
		virtual S_CONNECT_BASE* FindClient(const int index);

		virtual void Update();

		virtual void CreatePackage(const int index, const uint16_t cmd, void* v, const int len);
		virtual void ReadPackage(const int index, void* v, const int len);

		virtual void SetNotify_Connect(ISERVER_NOTIFY e);
		virtual void SetNotify_Secure(ISERVER_NOTIFY e); 
		virtual void SetNotify_DisConnect(ISERVER_NOTIFY e);
		virtual void SetNotify_Command(ISERVER_NOTIFY e);
	private:
		//玩家数据
		common::HashContainer<S_CONNECT_BASE>* m_Onlines;//在线玩家数据
		common::HashContainer<S_CONNECT_INDEX>* m_OnlinesIndexs;//连接玩家索引数组
		//通知事件
		ISERVER_NOTIFY      m_Notify_Accept;
		ISERVER_NOTIFY      m_Notify_Secure;
		ISERVER_NOTIFY      m_Notify_Disconnect;
		ISERVER_NOTIFY      m_Notify_Command;
		//互斥锁
		std::mutex          m_Mutex_NoState;
		std::mutex          m_Mutex_ConnectCount;
		std::mutex          m_Mutex_SecureCount;
		//私有数据
		int32_t             m_ConnectCount;//当前连接数
		int32_t             m_SecurityCount;//安全连接数

		bool                         m_IsRunning;
		SOCKET                       m_ListenSocket;   //监听套接字句柄
		HANDLE                       m_Completeport;//完成端口句柄
		LPFN_ACCEPTEX                m_AcceptEx;//AcceptEx函数地址
		LPFN_GETACCEPTEXSOCKADDRS    m_GetAcceptEx;//获取客户端信息函数地址
		std::shared_ptr<std::thread> m_WorkThread[MAX_THREAD_LEN];

	};



}

#endif