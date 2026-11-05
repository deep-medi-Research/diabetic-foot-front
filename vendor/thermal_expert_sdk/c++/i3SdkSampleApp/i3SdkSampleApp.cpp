#include "stdafx.h"
#include "i3SdkSampleApp.h"
#include "i3SdkSampleAppDlg.h"
#ifdef _DEBUG
#define new DEBUG_NEW
#endif


#define _CRTDBG_MAP_ALLOC

BEGIN_MESSAGE_MAP(i3SdkSampleAppApp, CWinApp)
	ON_COMMAND(ID_HELP, &CWinApp::OnHelp)
END_MESSAGE_MAP()

i3SdkSampleAppApp::i3SdkSampleAppApp()
{
	m_dwRestartManagerSupportFlags = AFX_RESTART_MANAGER_SUPPORT_RESTART;
}

i3SdkSampleAppApp theApp;

BOOL i3SdkSampleAppApp::InitInstance()
{
	INITCOMMONCONTROLSEX InitCtrls;
	InitCtrls.dwSize = sizeof(InitCtrls);
	InitCtrls.dwICC = ICC_WIN95_CLASSES;
	InitCommonControlsEx(&InitCtrls);

	CWinApp::InitInstance();

	bool bIsInit = AfxOleInit();
	if (!bIsInit) {
		TRACE("Could not initialize COM dll");
		return FALSE;
	}
	COleMessageFilter *pFilter = AfxOleGetMessageFilter();
	pFilter->SetMessagePendingDelay(50000);

	AfxEnableControlContainer();

	CShellManager *pShellManager = new CShellManager;

	CMFCVisualManager::SetDefaultManager(RUNTIME_CLASS(CMFCVisualManagerWindows));

	Ci3SdkSampleAppDlg dlg;
	m_pMainWnd = &dlg;
	INT_PTR nResponse = dlg.DoModal();
	if (nResponse == IDOK)
	{
	}
	else if (nResponse == IDCANCEL)
	{
	}
	else if (nResponse == -1)
	{
		TRACE(traceAppMsg, 0, "Warning: Application exited unexpectedly because dialog was not created.\n");
		TRACE(traceAppMsg, 0, "Warning: Cannot execute #define _AFX_NO_MFC_CONTROLS_IN_DIALOGS if MFC control is used in dialog.\n");
	}

	if (pShellManager != NULL)
	{
		delete pShellManager;
	}

	return FALSE;
}

