#include "stdafx.h"
#include "i3SdkSampleApp.h"
#include "DlgTypeB.h"
#include "afxdialogex.h"
#include "i3SdkSampleAppDlg.h"

int gTeModel[] = {I3_TE_Q1, I3_TE_V1, I3_TE_M1, I3_TE_Q2};

IMPLEMENT_DYNAMIC(CDlgTypeB, CDialog)

CDlgTypeB::CDlgTypeB(CWnd* pParent /*=NULL*/)
	: CDialog(IDD_DLGTYPEB, pParent)
	, m_fEmissivity(0)
{
	m_pThrd = NULL;
	m_iHnd_Dev = 0;
	m_bAGC = true;
	m_iOffset = 0;
	m_fGain = 1.f;
	m_iColorMap = 0;
	m_bStopRecv = true;
	m_bIsPlaying = false;
	m_hPauseEvent = CreateEvent(NULL, TRUE, FALSE, NULL);
	m_hOpEndEvent = CreateEvent(NULL, TRUE, FALSE, NULL);
	m_bPause = false;
	m_pTEB = nullptr;
}

CDlgTypeB::~CDlgTypeB()
{
}

void CDlgTypeB::DoDataExchange(CDataExchange* pDX)
{
	CDialog::DoDataExchange(pDX);
	DDX_Check(pDX, IDC_CHECK_AGC_B, *((int*)&m_bAGC));
	DDX_Slider(pDX, IDC_SLIDER_GAIN_B, m_iGain);
	DDX_Slider(pDX, IDC_SLIDER_OFFSET_B, m_iOffset);
	DDX_Text(pDX, IDC_STATIC_GAINVAL_B, m_fGain);
	DDX_Text(pDX, IDC_STATIC_OFFSETVAL_B, m_iOffsetOut);
	DDX_Control(pDX, IDC_SLIDER_OFFSET_B, m_ctrlSldOffset);
	DDX_Control(pDX, IDC_SLIDER_GAIN_B, m_ctrlSldGain);
	DDX_Control(pDX, IDC_COMBO_COLORMAP_B, m_ctrlCBColorMap);
	DDX_CBIndex(pDX, IDC_COMBO_COLORMAP_B, m_iColorMap);
	DDX_Control(pDX, IDC_SPIN_CAMNUM_B, m_ctrlSpinCamNumB);
	DDX_Text(pDX, IDC_EDIT_CAMNUM_B, m_iHnd_Dev);
	DDX_Text(pDX, IDC_EDIT_EMISSIVITY_B, m_fEmissivity);
	DDX_Control(pDX, IDC_COMBO_RES_B, m_cb_res);
	DDX_CBIndex(pDX, IDC_COMBO_RES_B, m_nModel);
}


BEGIN_MESSAGE_MAP(CDlgTypeB, CDialog)
	ON_WM_ERASEBKGND()
	ON_BN_CLICKED(IDC_BUTTON_OPEN_B, &CDlgTypeB::OnBnClickedButtonOpenB)
	ON_BN_CLICKED(IDC_BUTTON_CLOSE_B, &CDlgTypeB::OnBnClickedButtonCloseB)
	ON_BN_CLICKED(IDC_BUTTON_START, &CDlgTypeB::OnBnClickedButtonStart)
	ON_BN_CLICKED(IDC_BUTTON_STOP, &CDlgTypeB::OnBnClickedButtonStop)
	ON_BN_CLICKED(IDC_BUTTON_RECV, &CDlgTypeB::OnBnClickedButtonRecv)
	ON_BN_CLICKED(IDC_CHECK_AGC_B, &CDlgTypeB::OnBnClickedCheckAgcB)
	ON_WM_HSCROLL()
	ON_CBN_SELCHANGE(IDC_COMBO_COLORMAP_B, &CDlgTypeB::OnCbnSelchangeComboColormapB)
	ON_BN_CLICKED(IDC_BUTTON_SHUTTERCALIB, &CDlgTypeB::OnBnClickedButtonShuttercalib)
	ON_BN_CLICKED(IDC_BUTTON_UPDATEDEAD, &CDlgTypeB::OnBnClickedButtonUpdatedead)
	ON_WM_PAINT()
	ON_WM_DESTROY()
	ON_BN_CLICKED(IDC_CHECK_AIE_B, &CDlgTypeB::OnBnClickedCheckAieB)
	ON_EN_CHANGE(IDC_EDIT_EMISSIVITY_B, &CDlgTypeB::OnEnChangeEditEmissivityB)
	ON_CBN_SELCHANGE(IDC_COMBO_RES_B, &CDlgTypeB::OnCbnSelchangeComboResB)
	ON_BN_CLICKED(IDC_BUTTON_GETFPATEMP_B, &CDlgTypeB::OnBnClickedButtonGetfpatemp)
END_MESSAGE_MAP()


BOOL CDlgTypeB::OnEraseBkgnd(CDC* pDC)
{
	return CDialog::OnEraseBkgnd(pDC);
}


void CDlgTypeB::OnBnClickedButtonOpenB()
{
	// connect device & read flash data to initialize
	int model = gTeModel[m_nModel];
	m_pTEB = OpenTE_B(model, m_iHnd_Dev);
	if (m_pTEB) {
		((Ci3SdkSampleAppDlg*)m_pParent)->InsertString(L"Usb Opened");
	}
	else {
		((Ci3SdkSampleAppDlg*)m_pParent)->InsertString(L"Usb Not Opened");
	}
}



void CDlgTypeB::OnBnClickedButtonCloseB()
{
	// disconnect device
	if (m_pTEB)
		m_pTEB->CloseTE();

	m_pTEB = nullptr;
	((Ci3SdkSampleAppDlg*)m_pParent)->InsertString(L"Usb Closed");
}


void CDlgTypeB::OnBnClickedButtonStart()
{
	// create thread to get & display image
	if (!m_pTEB) {
		((Ci3SdkSampleAppDlg*)m_pParent)->InsertString(L"Usb is Not Opened");
		return;
	}

	m_bStopRecv = false;
	m_pThrd = AfxBeginThread(RecvThread, this);
	if (!m_pThrd) {
		((Ci3SdkSampleAppDlg*)m_pParent)->InsertString(L"Cannot Create Thread");
		m_bStopRecv = true;
	}
}


void CDlgTypeB::OnBnClickedButtonStop()
{
	// stop receiving image
	m_bStopRecv = true;
}


void CDlgTypeB::OnBnClickedButtonRecv()
{
	// get 1 frame image
	if (!m_pTEB) {
		((Ci3SdkSampleAppDlg*)m_pParent)->InsertString(L"Usb is Not Opened");
		return;
	}

	int nWidth = m_pTEB->GetImageWidth(), nHeight = m_pTEB->GetImageHeight();
	int ret;
	WORD *pRecv = new WORD[nWidth * nHeight];
	if (m_bAGC) {
		ret = m_pTEB->RecvImage(pRecv);
	}
	else {
		float *pfRecv = new float[nWidth * nHeight];
		ret = m_pTEB->RecvImage(pfRecv);
		for (int i = 0; i < nWidth * nHeight; i++) {
			int a = (int)((pfRecv[i] * m_fGain + m_iOffset) * 256.f);
			if (a < 0)
				pRecv[i] = 0;
			else if (a > 65535)
				pRecv[i] = 65535;
			else
				pRecv[i] = a;
		}
		delete pfRecv;
	}
	if (ret == 1) {
		if (((Ci3SdkSampleAppDlg*)m_pParent)->m_bIsFullScr) {
			((Ci3SdkSampleAppDlg*)m_pParent)->m_dlgFullScr.Draw(pRecv, nWidth, nHeight, m_iColorMap);
		}
		else {
			((Ci3SdkSampleAppDlg*)m_pParent)->m_stcDisplay.Draw(pRecv, nWidth, nHeight, m_iColorMap, false, 0);
			if (!m_bIsPlaying)
				((Ci3SdkSampleAppDlg*)m_pParent)->InsertString(L"Image Received");
		}
	}

	delete pRecv;
}

UINT CDlgTypeB::RecvThread(LPVOID lpvoid)
{
	CDlgTypeB *pDlg = (CDlgTypeB*)lpvoid;
	pDlg->m_bIsPlaying = true;
	while (true) {
		if (pDlg->m_bStopRecv) {
			break;
		}
		if (pDlg->m_bPause) {
			SetEvent(pDlg->m_hPauseEvent);
			WaitForSingleObject(pDlg->m_hOpEndEvent, INFINITE);
			ResetEvent(pDlg->m_hOpEndEvent);
			pDlg->m_bPause = false;
		}

		pDlg->OnBnClickedButtonRecv();
	}
	pDlg->m_bIsPlaying = false;
	return 1;
}


BOOL CDlgTypeB::OnInitDialog()
{
	CDialog::OnInitDialog();

	m_ctrlSldGain.SetRange(0, 50);
	m_ctrlSldOffset.SetRange(-255, 255);

	m_ctrlSldGain.SetTicFreq(10);
	m_ctrlSldGain.SetLineSize(1);
	m_ctrlSldGain.SetPageSize(5);
	m_ctrlSldOffset.SetTicFreq(50);
	m_ctrlSldOffset.SetLineSize(1);
	m_ctrlSldOffset.SetPageSize(10);

	m_ctrlCBColorMap.AddString(L"WhiteHot");
	m_ctrlCBColorMap.AddString(L"BlackHot");
	m_ctrlCBColorMap.AddString(L"Iron");
	m_ctrlCBColorMap.AddString(L"BlueRed");
	m_ctrlCBColorMap.AddString(L"Medical");
	m_ctrlCBColorMap.AddString(L"Purple");
	m_ctrlCBColorMap.AddString(L"PurpleYellow");
	m_ctrlCBColorMap.AddString(L"DarkBlue");
	m_ctrlCBColorMap.AddString(L"Cyan");
	m_ctrlCBColorMap.AddString(L"Rainbow");
	m_ctrlCBColorMap.SetCurSel(0);

	m_cb_res.AddString(L"Q1");
	m_cb_res.AddString(L"V1");
	m_cb_res.AddString(L"M1");
	m_cb_res.AddString(L"Q2");
	m_cb_res.SetCurSel(0);

	UpdateData(TRUE);
	m_ctrlSpinCamNumB.SetRange(0, 8);
	UDACCEL accels[] = { { 0,1 } };
	m_ctrlSpinCamNumB.SetAccel(1, accels);

	m_bAGC = true;
	m_fGain = 1.0f;
	m_iGain = 10;
	m_iOffset = 0;
	m_iOffsetOut = 0;
	m_fEmissivity = 1.f;
	m_ctrlSldOffset.SetPos(255);
	m_nModel = 0;
	UpdateData(FALSE);

	return TRUE;
}


void CDlgTypeB::OnBnClickedCheckAgcB()
{
	UpdateData(TRUE);
}


void CDlgTypeB::OnHScroll(UINT nSBCode, UINT nPos, CScrollBar* pScrollBar)
{
	if (pScrollBar) {
		UpdateData(TRUE);
		m_fGain = (float)(m_iGain / 10.f);
		m_iOffsetOut = m_iOffset;
		UpdateData(FALSE);
	}

	CDialog::OnHScroll(nSBCode, nPos, pScrollBar);
}


void CDlgTypeB::OnCbnSelchangeComboColormapB()
{
	UpdateData(TRUE);
}


void CDlgTypeB::OnBnClickedButtonShuttercalib()
{
	// calibration
	// See uniform scene before call this function
	if (!m_pTEB) {
		((Ci3SdkSampleAppDlg*)m_pParent)->InsertString(L"Usb is Not Opened");
		return;
	}

	if (m_pTEB->ShutterCalibrationOn())
		((Ci3SdkSampleAppDlg*)m_pParent)->InsertString(L"Calibration Completed");
	else
		((Ci3SdkSampleAppDlg*)m_pParent)->InsertString(L"Calibration Failed");

	if (m_bIsPlaying) {
		SetEvent(m_hOpEndEvent);
	}
}


void CDlgTypeB::OnBnClickedButtonUpdatedead()
{
	// update bad pixel
	// See uniform scene before call this function
	if (!m_pTEB) {
		((Ci3SdkSampleAppDlg*)m_pParent)->InsertString(L"Usb is Not Opened");
		return;
	}

	if (m_bIsPlaying) {
		m_bPause = true;
		WaitForSingleObject(m_hPauseEvent, 3000);
		ResetEvent(m_hPauseEvent);
	}

	if (m_pTEB->UpdateDead())
		((Ci3SdkSampleAppDlg*)m_pParent)->InsertString(L"Bad Pixel Update Completed");
	else
		((Ci3SdkSampleAppDlg*)m_pParent)->InsertString(L"Bad Pixel Update Failed");

	if (m_bIsPlaying) {
		SetEvent(m_hOpEndEvent);
	}
}

int CDlgTypeB::GetImageWidth()
{
	return m_pTEB->GetImageWidth();
}

int CDlgTypeB::GetImageHeight()
{
	return m_pTEB->GetImageHeight();
}

float CDlgTypeB::CalcTemp(int _x, int _y)
{
	// get temperature
	return m_pTEB->CalcTemp(_x, _y);
}

BOOL CDlgTypeB::PreTranslateMessage(MSG* pMsg)
{
	// quit program
	if (pMsg->message == WM_SYSKEYDOWN && pMsg->wParam == VK_F4) {
		if (m_pThrd) {
			m_bStopRecv = true;
			WaitForSingleObject(m_pThrd->m_hThread, 5000);
			m_pThrd = NULL;
		}
		if (m_pTEB) {
			m_pTEB->CloseTE();
			m_pTEB = nullptr;
		}
		PostQuitMessage(0);
		return TRUE;
	}
	else if (pMsg->message == WM_KEYDOWN) {
		switch (pMsg->wParam) {
		case VK_RETURN:
		case VK_ESCAPE:
			return TRUE;
		}
	}

	return CDialog::PreTranslateMessage(pMsg);
}

void CDlgTypeB::OnPaint()
{
	CPaintDC dc(this);
}

void CDlgTypeB::OnDestroy()
{
	CDialog::OnDestroy();

	// disconnect device before quit program if device is connected
	if (m_pThrd) {
		m_bStopRecv = true;
		WaitForSingleObject(m_pThrd->m_hThread, 5000);
		m_pThrd = NULL;
	}
	if (m_pTEB) {
		m_pTEB->CloseTE();
		m_pTEB = nullptr;
	}
}


void CDlgTypeB::OnBnClickedCheckAieB()
{
	UpdateData();
}


void CDlgTypeB::OnEnChangeEditEmissivityB()
{
	// change emissivity
	UpdateData();
	if(m_pTEB && (m_fEmissivity > 0.f && m_fEmissivity <= 1.f))
		m_pTEB->SetEmissivity(m_fEmissivity);
}

void CDlgTypeB::OnCbnSelchangeComboResB()
{
	// select device resolution
	UpdateData();
}


void CDlgTypeB::OnBnClickedButtonGetfpatemp()
{
	// get fpa temperature
	if (m_pTEB) {
		float temp = m_pTEB->GetFpaTemp();
		CString strout;
		strout.Format(_T("fpa temp : %f"), temp);
		AfxMessageBox(strout);
	}
}
