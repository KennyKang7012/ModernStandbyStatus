/*
  * @file : ModernStandbyStaus.cpp
  * @brief : 偵測Windows系統的電源事件
  * @author : KennyKang
  * @e-mail : kenny7012@gmail.com
  * @date : 2020/09/30 
 */

#include <Windows.h>
#include <tchar.h>
#include <string>
#include <ctime>
#include <PowrProf.h>
#pragma comment(lib,"PowrProf.lib")
#include <WinNT.h>

SERVICE_STATUS        g_ServiceStatus = { 0 };
SERVICE_STATUS_HANDLE g_StatusHandle = NULL;
HANDLE                g_ServiceStopEvent = INVALID_HANDLE_VALUE;
HANDLE				  g_MonitorPowerOnEvent = INVALID_HANDLE_VALUE;
HANDLE				  g_MonitorPowerOffEvent = INVALID_HANDLE_VALUE;
HANDLE				  g_MonitorShutDownEvent = INVALID_HANDLE_VALUE;

VOID WINAPI ServiceMain(DWORD argc, LPTSTR *argv);
DWORD WINAPI ServiceCtrlHandler(DWORD dwControl, DWORD dwEventType, LPVOID lpEventData, LPVOID lpContext);
DWORD WINAPI ServiceWorkerThread(LPVOID lpParam);;
BOOL IsSupportConnectedStandby(VOID);

#define SERVICE_NAME _T("MonitorPowerStatus")

int _tmain(int argc, TCHAR *argv[])
{
	OutputDebugString(_T("[ModernStandby]: Main: Entry"));

	SERVICE_TABLE_ENTRY ServiceTable[] =
	{
		{(WCHAR *) SERVICE_NAME, (LPSERVICE_MAIN_FUNCTION)ServiceMain},
		{NULL, NULL}
	};

	if (StartServiceCtrlDispatcher(ServiceTable) == FALSE) {
		OutputDebugString(_T("[ModernStandby]: Main: StartServiceCtrlDispatcher returned error"));
		return GetLastError();
	}

	OutputDebugString(_T("[ModernStandby]: Main: Exit"));
	return 0;
}

VOID WINAPI ServiceMain(DWORD argc, LPTSTR *argv)
{
	DWORD Status = E_FAIL;
	HANDLE hThread = NULL;
	HPOWERNOTIFY hNotify[2] = { NULL, NULL };

	OutputDebugString(_T("[ModernStandby]: ServiceMain: Entry"));

	// Create Monitor Power On/Off Event
	SECURITY_DESCRIPTOR SD;
	SECURITY_ATTRIBUTES EventAttributes;
	EventAttributes.nLength = sizeof(SECURITY_ATTRIBUTES);
	EventAttributes.lpSecurityDescriptor = &SD;
	EventAttributes.bInheritHandle = TRUE;
	InitializeSecurityDescriptor(&SD, SECURITY_DESCRIPTOR_REVISION);
	SetSecurityDescriptorDacl(&SD, TRUE, (PACL)NULL, FALSE);

	g_MonitorPowerOnEvent = CreateEvent(&EventAttributes, TRUE, FALSE, L"Global\\MonitorPowerOnEvent");
	g_MonitorPowerOffEvent = CreateEvent(&EventAttributes, TRUE, FALSE, L"Global\\MonitorPowerOffEvent");
	g_MonitorShutDownEvent = CreateEvent(&EventAttributes, TRUE, FALSE, L"Global\\MonitorShutDownEvent");

	g_StatusHandle = RegisterServiceCtrlHandlerEx(SERVICE_NAME, ServiceCtrlHandler, NULL);

	if (g_StatusHandle == NULL) {
		OutputDebugString(_T("[ModernStandby]: ServiceMain: RegisterServiceCtrlHandler returned error"));
		goto EXIT;
	}

	// Tell the service controller we are starting
	ZeroMemory(&g_ServiceStatus, sizeof(g_ServiceStatus));
	g_ServiceStatus.dwServiceType = SERVICE_WIN32_OWN_PROCESS;
	g_ServiceStatus.dwControlsAccepted = 0;
	g_ServiceStatus.dwCurrentState = SERVICE_START_PENDING;
	g_ServiceStatus.dwWin32ExitCode = 0;
	g_ServiceStatus.dwServiceSpecificExitCode = 0;
	g_ServiceStatus.dwCheckPoint = 0;

	if (SetServiceStatus(g_StatusHandle, &g_ServiceStatus) == FALSE) {
		OutputDebugString(_T("[ModernStandby]: ServiceMain: SetServiceStatus returned error"));
	}

	/* Connected Standby Process */
	if (IsSupportConnectedStandby()) {
		hNotify[0] = RegisterPowerSettingNotification(g_StatusHandle, &GUID_MONITOR_POWER_ON, DEVICE_NOTIFY_SERVICE_HANDLE);
		if (!hNotify[0])
			OutputDebugString(_T("[ModernStandby] ServiceMain: [GUID_MONITOR_POWER_ON] register err!!!!"));
		else
			OutputDebugString(_T("[ModernStandby] ServiceMain: [GUID_MONITOR_POWER_ON] register success!!!!"));

		hNotify[1] = RegisterPowerSettingNotification(g_StatusHandle, &GUID_CONSOLE_DISPLAY_STATE, DEVICE_NOTIFY_SERVICE_HANDLE);
		if (!hNotify[1])
			OutputDebugString(_T("[ModernStandby] ServiceMain: [GUID_CONSOLE_DISPLAY_STATE] register err!!!!"));
		else
			OutputDebugString(_T("[ModernStandby] ServiceMain: [GUID_CONSOLE_DISPLAY_STATE] register success!!!!"));
	}

	OutputDebugString(_T("[ModernStandby]: ServiceMain: Performing Service Start Operations"));

	g_ServiceStopEvent = CreateEvent(NULL, TRUE, FALSE, NULL);
	if (g_ServiceStopEvent == NULL) {
		OutputDebugString(_T("[ModernStandby]: ServiceMain: CreateEvent(g_ServiceStopEvent) returned error"));

		g_ServiceStatus.dwControlsAccepted = 0;
		g_ServiceStatus.dwCurrentState = SERVICE_STOPPED;
		g_ServiceStatus.dwWin32ExitCode = GetLastError();
		g_ServiceStatus.dwCheckPoint = 1;

		if (SetServiceStatus(g_StatusHandle, &g_ServiceStatus) == FALSE) {
			OutputDebugString(_T("[ModernStandby]: ServiceMain: SetServiceStatus returned error"));
		}
		goto EXIT;
	}

	g_ServiceStatus.dwControlsAccepted = SERVICE_ACCEPT_STOP | SERVICE_ACCEPT_PRESHUTDOWN;
	g_ServiceStatus.dwCurrentState = SERVICE_RUNNING;
	g_ServiceStatus.dwWin32ExitCode = 0;
	g_ServiceStatus.dwCheckPoint = 0;

	if (SetServiceStatus(g_StatusHandle, &g_ServiceStatus) == FALSE) {
		OutputDebugString(_T("[ModernStandby]: ServiceMain: SetServiceStatus returned error"));
	}

	hThread = CreateThread(NULL, 0, ServiceWorkerThread, NULL, 0, NULL);

	OutputDebugString(_T("[ModernStandby]: ServiceMain: Waiting for Worker Thread to complete"));

	WaitForSingleObject(hThread, INFINITE);

	OutputDebugString(_T("[ModernStandby]: ServiceMain: Worker Thread Stop Event signaled"));

	OutputDebugString(_T("[ModernStandby]: ServiceMain: Performing Cleanup Operations"));

	CloseHandle(g_ServiceStopEvent);
	CloseHandle(g_MonitorPowerOnEvent);
	CloseHandle(g_MonitorPowerOffEvent);
	CloseHandle(g_MonitorShutDownEvent);

	g_ServiceStatus.dwControlsAccepted = 0;
	g_ServiceStatus.dwCurrentState = SERVICE_STOPPED;
	g_ServiceStatus.dwWin32ExitCode = 0;
	g_ServiceStatus.dwCheckPoint = 3;

	if (SetServiceStatus(g_StatusHandle, &g_ServiceStatus) == FALSE) {
		OutputDebugString(_T("[ModernStandby]: ServiceMain: SetServiceStatus returned error"));
	}

EXIT:
	if (hNotify[0])
		UnregisterPowerSettingNotification(hNotify[0]);
	if (hNotify[1])
		UnregisterPowerSettingNotification(hNotify[1]);
	OutputDebugString(_T("[ModernStandby]: ServiceMain: Exit"));
	return;
}

//Converting a Ansi string to WChar string   
std::wstring Ansi2WChar(LPCSTR pszSrc, int nLen)
{
	int nSize = MultiByteToWideChar(CP_ACP, 0, (LPCSTR)pszSrc, nLen, 0, 0);
	if (nSize <= 0) return NULL;
	WCHAR *pwszDst = new WCHAR[nSize + 1];
	if (NULL == pwszDst) return NULL;
	MultiByteToWideChar(CP_ACP, 0, (LPCSTR)pszSrc, nLen, pwszDst, nSize);
	pwszDst[nSize] = 0;
	if (pwszDst[0] == 0xFEFF) // skip Oxfeff   
		for (int i = 0; i < nSize; i++)
			pwszDst[i] = pwszDst[i + 1];
	std::wstring wcharString(pwszDst);
	delete pwszDst;
	return wcharString;
}

std::wstring s2ws(const std::string& s)
{
	return Ansi2WChar(s.c_str(), (int)s.size());
}

DWORD WINAPI ServiceCtrlHandler(DWORD dwControl, DWORD dwEventType, LPVOID lpEventData, LPVOID lpContext)
{
	OutputDebugString(_T("[ModernStandby]: ServiceCtrlHandler: Entry"));

	std::string str;
	PPOWERBROADCAST_SETTING setting = (PPOWERBROADCAST_SETTING)lpEventData;

	switch (dwControl)
	{
		case SERVICE_CONTROL_STOP:
			OutputDebugString(_T("[ModernStandby]: ServiceCtrlHandler: SERVICE_CONTROL_STOP Request"));

			if (g_ServiceStatus.dwCurrentState != SERVICE_RUNNING)
				break;

			g_ServiceStatus.dwControlsAccepted = 0;
			g_ServiceStatus.dwCurrentState = SERVICE_STOP_PENDING;
			g_ServiceStatus.dwWin32ExitCode = 0;
			g_ServiceStatus.dwCheckPoint = 4;

			if (SetServiceStatus(g_StatusHandle, &g_ServiceStatus) == FALSE)
			{
				OutputDebugString(_T("[ModernStandby]: ServiceCtrlHandler: SetServiceStatus returned error"));
			}

			SetEvent(g_ServiceStopEvent);
			break;

		case SERVICE_CONTROL_PRESHUTDOWN:
			OutputDebugString(_T("[ModernStandby]: ServiceCtrlHandler: SERVICE_CONTROL_PRESHUTDOWN Request"));
			SetEvent(g_MonitorShutDownEvent);
			break;

		case SERVICE_CONTROL_POWEREVENT:
			switch (dwEventType)
			{
				case PBT_POWERSETTINGCHANGE:
				{
					OutputDebugString(_T("[ModernStandby] PBT_POWERSETTINGCHANGE"));
					ResetEvent(g_MonitorPowerOnEvent);
					ResetEvent(g_MonitorPowerOffEvent);

					if (setting->PowerSetting == GUID_MONITOR_POWER_ON) {
						str = "GUID_MONITOR_POWER_ON ";
						if (setting->DataLength == 4) {
							DWORD data = *(DWORD*)(setting->Data);
							str += std::to_string(data);
							if (data == 0) {
								SetEvent(g_MonitorPowerOffEvent);
								OutputDebugString(_T("[ModernStandby] ServiceCtrlHandler: MONITOR_POWER_OFF"));
							}
							else {
								SetEvent(g_MonitorPowerOnEvent);
								OutputDebugString(_T("[ModernStandby] ServiceCtrlHandler: MONITOR_POWER_ON"));
							}
						}
						else {
							str += "len: ";
							str += std::to_string(setting->DataLength);
						}
						OutputDebugString(s2ws(str).c_str());
					}
					else if (setting->PowerSetting == GUID_CONSOLE_DISPLAY_STATE) {
						str = "GUID_CONSOLE_DISPLAY_STATE ";
						DWORD data = *(DWORD*)(setting->Data);
						str += std::to_string(data);

						if (data == 0)
							OutputDebugString(_T("[ModernStandby] Display off"));
						else if (data == 1)
							OutputDebugString(_T("[ModernStandby] Display on"));
						else if (data == 2)
							OutputDebugString(_T("[ModernStandby] Display dimmed"));

						OutputDebugString(s2ws(str).c_str());
					}
					else
						OutputDebugString(_T("[ModernStandby] Unknown GUID"));
					break;
				}

				default:
					break;
			}
			break;

		default:
			break;
	}
	OutputDebugString(_T("[ModernStandby]: ServiceCtrlHandler: Exit"));
	return NO_ERROR;
}

//S0 Low Power Idle
BOOL IsSupportConnectedStandby(VOID)
{
	WCHAR sInfo[256] = { 0 };
	bool result = false;
	SYSTEM_POWER_CAPABILITIES info = { 0 };
	NTSTATUS ret = CallNtPowerInformation(SystemPowerCapabilities, NULL, 0, &info, sizeof(info));
	if (ret != 0) {
		swprintf(sInfo, L"[ModernStandby]: Get Info Error: 0x%x\n", ret);
		OutputDebugString(sInfo);
		goto Exit;
	}
	if (info.AoAc == TRUE)
		result = true;
Exit:
	return result;
}

DWORD WINAPI ServiceWorkerThread(LPVOID lpParam)
{
	OutputDebugString(_T("[ModernStandby]: ServiceWorkerThread: Entry"));

	DWORD WaitResult;
	while (1) {
		WaitResult = WaitForSingleObject(g_ServiceStopEvent, INFINITE);
		if (WaitResult == WAIT_OBJECT_0) {
			OutputDebugString(_T("[ModernStandby] ServiceWorkerThread: g_ServiceStopEvent Trigger"));
			break;
		}
		Sleep(3000);
	}

	OutputDebugString(_T("[ModernStandby]: ServiceWorkerThread: Exit"));
	return ERROR_SUCCESS;
}
