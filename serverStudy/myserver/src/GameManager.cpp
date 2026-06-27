#include "GameManager.h"

#include "GameCommand.h"
#include "PublicEntry.h"
#include <time.h>
#include "GamePlayer.h"
#include "GameTest.h"


#ifndef  ____Win32
#include <string.h>
#include <unistd.h>
#endif

namespace app
{
	int temp_time = 0;
	char printfstr[1000];
	//打印信息
	void printInfo()
	{
#ifdef  ____Win32
		int tempTime = (int)time(NULL) - temp_time;
		if (tempTime < 1) return;
		temp_time = (int)time(NULL);

		int concount = __IServer->ConnnectCount();
		int securtiycount = __IServer->SecureCount();

		sprintf(printfstr, "连接用户:%d  安全连接:%d", concount, securtiycount);
		SetWindowTextA(GetConsoleWindow(), printfstr);
#endif //  ____WIN32_
	}

	void Init()
	{
		__IServer = tcp::NewIServer();
		__IServer->SetNotify_Connect(Event_Server_Accept);
		__IServer->SetNotify_Secure(Event_Server_Secure);
		__IServer->SetNotify_DisConnect(Event_Server_Disconnect);
		__IServer->SetNotify_Command(Event_Server_Command);

		__IServer->StartServer();


		__Player = new GamePlayer();
		__Test = new GameTest();
	}

	void Update()
	{
		__IServer->Update();
		__Player->Update();
		printInfo();

	}
	int StartApp()
	{
		if (!entry::Init()) return -1;
		Init();

		while (true)
		{
			Update();
#ifdef  ____Win32
			Sleep(2);
#else
			usleep(2 * 1000);
#endif

		}

		return 0;
	}

}

