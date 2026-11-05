#pragma once

#ifndef __AFXWIN_H__
	#error "include 'stdafx.h' before this file for PCH."
#endif

#include "resource.h"

class i3SdkSampleAppApp : public CWinApp
{
public:
	i3SdkSampleAppApp();

public:
	virtual BOOL InitInstance();

	DECLARE_MESSAGE_MAP()
};

extern i3SdkSampleAppApp theApp;