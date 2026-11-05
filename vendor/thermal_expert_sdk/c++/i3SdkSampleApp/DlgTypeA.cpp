#include "stdafx.h"
#include "i3SdkSampleApp.h"
#include "DlgTypeA.h"
#include "afxdialogex.h"
#include "i3SdkSampleAppDlg.h"

IMPLEMENT_DYNAMIC(CDlgTypeA, CDialog)

CDlgTypeA::CDlgTypeA(CWnd* pParent /*=NULL*/)
	: CDialog(IDD_DLGTYPEA, pParent)
	, m_fEmissivity(0)
{
	m_bStopRecv = true;
	m_bIsPlaying = false;
	m_bPause = false;
	m_hPauseEvent = CreateEvent(NULL, TRUE, FALSE, NULL);
	m_hOpEndEvent = CreateEvent(NULL, TRUE, FALSE, NULL);
	m_iHnd_Dev = 0;
	m_pThrd = NULL;
	m_pTEA = nullptr;
	m_pImgBuf = nullptr;
}

CDlgTypeA::~CDlgTypeA()
{
}

void CDlgTypeA::DoDataExchange(CDataExchange* pDX)
{
	CDialog::DoDataExchange(pDX);
	DDX_Control(pDX, IDC_COMBO_COLORMAP_A, m_ctrlCBColorMap);
	DDX_Check(pDX, IDC_CHECK_AGC_A, *((int*)&m_bAGC));
	DDX_CBIndex(pDX, IDC_COMBO_COLORMAP_A, m_iColorMap);
	DDX_Slider(pDX, IDC_SLIDER_GAIN_A, m_iGain);
	DDX_Slider(pDX, IDC_SLIDER_OFFSET_A, m_iOffset);
	DDX_Control(pDX, IDC_SLIDER_GAIN_A, m_ctrlSldGain);
	DDX_Control(pDX, IDC_SLIDER_OFFSET_A, m_ctrlSldOffset);
	DDX_Text(pDX, IDC_STATIC_GAINVAL_A, m_fGain);
	DDX_Text(pDX, IDC_STATIC_OFFSETVAL_A, m_iOffsetOut);
	DDX_Control(pDX, IDC_SPIN_CAMNUM, m_ctrlSpinCam);
	DDX_Text(pDX, IDC_EDIT_CAMNUM, m_iHnd_Dev);
	DDX_Control(pDX, IDC_SLIDER_AIE_A, m_sldCtrlAIE);
	DDX_Text(pDX, IDC_EDIT_EMISSIVITY_A, m_fEmissivity);
	DDX_Slider(pDX, IDC_SLIDER_LIMIT_GAIN_A, m_iLimitGain);
	DDX_Text(pDX, IDC_STATIC_LIMIT_GAIN_VAL_A, m_fLimitGain);
}


BEGIN_MESSAGE_MAP(CDlgTypeA, CDialog)
	ON_WM_ERASEBKGND()
	ON_CBN_SELCHANGE(IDC_COMBO_COLORMAP_A, &CDlgTypeA::OnCbnSelchangeComboColormapA)
	ON_BN_CLICKED(IDC_CHECK_AGC_A, &CDlgTypeA::OnBnClickedCheckAgcA)
	ON_BN_CLICKED(IDC_BUTTON_OPEN_A, &CDlgTypeA::OnBnClickedButtonOpenA)
	ON_BN_CLICKED(IDC_BUTTON_CLOSE_A, &CDlgTypeA::OnBnClickedButtonCloseA)
	ON_BN_CLICKED(IDC_BUTTON_START_A, &CDlgTypeA::OnBnClickedButtonStartA)
	ON_BN_CLICKED(IDC_BUTTON_STOP_A, &CDlgTypeA::OnBnClickedButtonStopA)
	ON_BN_CLICKED(IDC_BUTTON_RECV_A, &CDlgTypeA::OnBnClickedButtonRecvA)
	ON_MESSAGE(WM_UPDATEDATA, &CDlgTypeA::OnUpdateData)
	ON_WM_HSCROLL()
	ON_WM_LBUTTONDOWN()
	ON_WM_LBUTTONUP()
	ON_WM_PAINT()
	ON_WM_DESTROY()
	ON_BN_CLICKED(IDC_CHECK_AIE_A, &CDlgTypeA::OnBnClickedCheckAieA)
	ON_EN_CHANGE(IDC_EDIT_EMISSIVITY_A, &CDlgTypeA::OnEnChangeEditEmissivityA)
	ON_EN_CHANGE(IDC_EDIT_CAMNUM, &CDlgTypeA::OnEnChangeEditCamnum)
	ON_BN_CLICKED(IDC_BUTTON_SCAN_A, &CDlgTypeA::OnBnClickedButtonScanA)
	ON_BN_CLICKED(IDC_BUTTON_SHUTTERCALIB_A, &CDlgTypeA::OnBnClickedButtonShuttercalibA)
	ON_BN_CLICKED(IDC_BUTTON_RESET, &CDlgTypeA::OnBnClickedButtonReset)
	ON_BN_CLICKED(IDC_BUTTON_GETFPATEMP_A, &CDlgTypeA::OnBnClickedButtonGetfpatempA)
	ON_BN_CLICKED(IDC_BUTTON_GETID_A, &CDlgTypeA::OnBnClickedButtonGetidA)
	ON_BN_CLICKED(IDC_BUTTON_SHADING_CORRECTION_A, &CDlgTypeA::OnBnClickedButtonShadingCorrectionA)
	ON_BN_CLICKED(IDC_BUTTON_SAVE_SHADING_CORRECTION_A, &CDlgTypeA::OnBnClickedButtonSaveShadingCorrectionA)
	ON_BN_CLICKED(IDC_BUTTON_LOAD_SHADING_CORRECTION_A, &CDlgTypeA::OnBnClickedButtonLoadShadingCorrectionA)
END_MESSAGE_MAP()

BOOL CDlgTypeA::OnEraseBkgnd(CDC* pDC)
{
	return CDialog::OnEraseBkgnd(pDC);
}

UINT CDlgTypeA::RecvThread(LPVOID lpvoid)
{
	CDlgTypeA *pDlg = (CDlgTypeA*)lpvoid;
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

		pDlg->OnBnClickedButtonRecvA();
		Sleep(10);
	}
	pDlg->m_bIsPlaying = false;
	return 1;
}

BOOL CDlgTypeA::OnInitDialog()
{
	CDialog::OnInitDialog();

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

	m_ctrlSldGain.SetRange(0, 50);
	m_ctrlSldOffset.SetRange(-255, 255);
	m_sldCtrlAIE.SetRange(10, 100);

	m_ctrlSldGain.SetTicFreq(10);
	m_ctrlSldGain.SetLineSize(1);
	m_ctrlSldGain.SetPageSize(5);
	m_ctrlSldOffset.SetTicFreq(50);
	m_ctrlSldOffset.SetLineSize(1);
	m_ctrlSldOffset.SetPageSize(10);
	m_sldCtrlAIE.SetTicFreq(1);
	m_sldCtrlAIE.SetLineSize(1);
	m_sldCtrlAIE.SetPageSize(10);
	
	int multi = 3;
	m_ctrlSldGain.SetRange(0, 500);
	m_ctrlSldOffset.SetRange(-65535*multi, 65535 * multi);

	m_ctrlSpinCam.SetRange(0, MAX_USB_NUM-1);
	UDACCEL accels[] = { { 0,1 } };
	m_ctrlSpinCam.SetAccel(1, accels);

	((CSliderCtrl*)GetDlgItem(IDC_SLIDER_LIMIT_GAIN_A))->SetRange(1, 100);

	UpdateData(TRUE);

	m_bAGC = true;
	m_fGain = 1.0f;
	m_iGain = 10;
	m_iOffset = 0;
	m_iOffsetOut = 0;
	m_fEmissivity = 1.f;
	m_ctrlSldOffset.SetPos(65535 * multi);
	m_iHnd_Dev = 0;
	m_iLimitGain = 12;
	m_fLimitGain = 1.2f;
	UpdateData(FALSE);

	return TRUE;
}


void CDlgTypeA::OnCbnSelchangeComboColormapA()
{
	UpdateData(TRUE);
}


void CDlgTypeA::OnBnClickedCheckAgcA()
{
	UpdateData(TRUE);
}


void CDlgTypeA::OnBnClickedButtonOpenA()
{
	// Connect device
	m_pTEA = i3::OpenTE_A(m_iHnd_Dev);
	if (m_pTEA) {
		m_pTEA->SetAgcLimitGain(m_fLimitGain);

		if (m_pImgBuf)
			delete m_pImgBuf;
		m_pImgBuf = new WORD[m_pTEA->GetImageWidth() * m_pTEA->GetImageHeight()];

		((Ci3SdkSampleAppDlg*)m_pParent)->InsertString(L"Usb Opened");
	}
	else {
		((Ci3SdkSampleAppDlg*)m_pParent)->InsertString(L"Usb Not Opened");
		return;
	}
}


void CDlgTypeA::OnBnClickedButtonCloseA()
{
	// Disconnect device
	if (m_pTEA)
		m_pTEA->CloseTE();
	m_pTEA = nullptr;

	if(m_pImgBuf)
		delete m_pImgBuf;
	m_pImgBuf = nullptr;

	((Ci3SdkSampleAppDlg*)m_pParent)->InsertString(L"Usb Closed");
}


void CDlgTypeA::OnBnClickedButtonStartA()
{
	// Create thread to get & display image
	if (!m_pTEA) {
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


void CDlgTypeA::OnBnClickedButtonStopA()
{
	// Stop receiving image
	m_bStopRecv = true;
}


void CDlgTypeA::OnBnClickedButtonRecvA()
{
	// get 1 frame image
	if (!m_pTEA) {
		((Ci3SdkSampleAppDlg*)m_pParent)->InsertString(L"Usb is Not Opened");
		return;
	}

	int nWidth = m_pTEA->GetImageWidth(), nHeight = m_pTEA->GetImageHeight();
	if (m_bAGC) {
		if (m_pTEA->RecvImage(m_pImgBuf, true) != 1)
			return;
	}
	else {
		if (m_pTEA->RecvImage(m_pImgBuf, false) != 1)
			return;
	}

	
	if (((Ci3SdkSampleAppDlg*)m_pParent)->m_bIsFullScr) {
		if (m_bAGC){
			((Ci3SdkSampleAppDlg*)m_pParent)->m_dlgFullScr.Draw(m_pImgBuf, nWidth, nHeight, m_iColorMap, 1 / 256.f);
		}
		else {
			((Ci3SdkSampleAppDlg*)m_pParent)->m_dlgFullScr.Draw(m_pImgBuf, nWidth, nHeight, m_iColorMap);
		}
	}
	else {		
		if (m_bAGC){
			((Ci3SdkSampleAppDlg*)m_pParent)->m_stcDisplay.Draw(m_pImgBuf, nWidth, nHeight, m_iColorMap, false, 0, 1 / 256.f, 0.f);
		}
		else{
			((Ci3SdkSampleAppDlg*)m_pParent)->m_stcDisplay.Draw(m_pImgBuf, nWidth, nHeight, m_iColorMap, false, 0,  m_fGain / 256.f * 2.f, m_iOffset / 100.f * 2.f);
		}
		if (!m_bIsPlaying)
			((Ci3SdkSampleAppDlg*)m_pParent)->InsertString(L"Image Received");
	}
}

void CDlgTypeA::OnHScroll(UINT nSBCode, UINT nPos, CScrollBar* pScrollBar)
{
	if (pScrollBar) {
		UpdateData(TRUE);
		m_fGain = (float)(m_iGain / 10.f);
		m_iOffsetOut = m_iOffset;
		m_fLimitGain = m_iLimitGain / 10.f;
		if (m_pTEA) {
			m_pTEA->SetAgcLimitGain(m_fLimitGain);
		}
		UpdateData(FALSE);
	}

	CDialog::OnHScroll(nSBCode, nPos, pScrollBar);
}

LRESULT CDlgTypeA::OnUpdateData(WPARAM _wparam, LPARAM _lparam)
{
	UpdateData(FALSE);
	return LRESULT();
}

int CDlgTypeA::GetImageWidth()
{
	return m_pTEA->GetImageWidth();
}

int CDlgTypeA::GetImageHeight()
{
	return m_pTEA->GetImageHeight();
}

void CDlgTypeA::CalcTemp(float * _pfTempBuf)
{
	// get temperature
	ASSERT(_pfTempBuf);
	int size = m_pTEA->GetImageHeight() * m_pTEA->GetImageWidth();
	WORD *pBuf = new WORD[size];
	m_pTEA->CalcTemp(pBuf);
	for (int i = 0; i < size; i++) {
		_pfTempBuf[i] = ((float)pBuf[i] - 5000.f) / 100.f;
	}
	delete pBuf;
}

#ifdef _BK
void CDlgTypeA::GetData(WORD * _buf)
{
	ASSERT(_buf);
	int size = m_pTEA->GetImageWidth() * m_pTEA->GetImageHeight();
	memcpy(_buf, m_pImgBuf, sizeof(WORD) * m_pTEA->GetImageWidth() * m_pTEA->GetImageHeight());
}
WORD CDlgTypeA::GetPt100()
{
	if (m_pTEA) {
		return m_pTEA->GetPT100RawValue();
	}
	else {
		return 0;
	}
}
#endif // _BK

BOOL CDlgTypeA::PreTranslateMessage(MSG* pMsg)
{
	if (pMsg->message == WM_SYSKEYDOWN && pMsg->wParam == VK_F4) {	// alt + f4
																	// quit program
		if (m_pThrd) {
			m_bStopRecv = true;
			WaitForSingleObject(m_pThrd->m_hThread, 5000);
			m_pThrd = NULL;
		}
		if (m_pTEA) {
			m_pTEA->CloseTE();
			m_pTEA = nullptr;
		}

		if (m_pImgBuf)
			delete m_pImgBuf;
		m_pImgBuf = nullptr;

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

void CDlgTypeA::OnLButtonDown(UINT nFlags, CPoint point)
{
	CDialog::OnLButtonDown(nFlags, point);
}


void CDlgTypeA::OnLButtonUp(UINT nFlags, CPoint point)
{
	CDialog::OnLButtonUp(nFlags, point);
}

void CDlgTypeA::OnPaint()
{
	CPaintDC dc(this);
}


void CDlgTypeA::OnDestroy()
{
	CDialog::OnDestroy();

	// disconnect device before quit program if device is connected
	if (m_pThrd) {
		m_bStopRecv = true;
		WaitForSingleObject(m_pThrd->m_hThread, 5000);
		m_pThrd = NULL;
	}
	if (m_pTEA) {
		m_pTEA->CloseTE();
		m_pTEA = nullptr;
	}

	if (m_pImgBuf)
		delete m_pImgBuf;
	m_pImgBuf = nullptr;
}


void CDlgTypeA::OnBnClickedCheckAieA()
{
	UpdateData();
}


void CDlgTypeA::OnEnChangeEditEmissivityA()
{
	// Set emissivity
	UpdateData();
	if(m_pTEA && (m_fEmissivity > 0.f && m_fEmissivity <= 1.f))
		m_pTEA->SetEmissivity(m_fEmissivity);
}

void CDlgTypeA::OnEnChangeEditCamnum()
{
	// change device number
	CString strCam;
	GetDlgItemText(IDC_EDIT_CAMNUM, strCam);
	int num = _ttoi(strCam);
	if ((num >= 0) && (num < MAX_USB_NUM) && (num != m_iHnd_Dev)) {
		if (m_pThrd) {
			m_bStopRecv = true;
			WaitForSingleObject(m_pThrd->m_hThread, 5000);
			m_pThrd = NULL;
		}
		if (m_pTEA) {
			m_pTEA->CloseTE();
			m_pTEA = nullptr;
		}

		if (m_pImgBuf)
			delete m_pImgBuf;
		m_pImgBuf = nullptr;

		UpdateData();
	}
}


void CDlgTypeA::OnBnClickedButtonScanA()
{
	// create dialog for device scan
	m_dlgScan.DestroyWindow();
	m_dlgScan.Create(IDD_DLGSCANINFO, this);
	m_dlgScan.ShowWindow(SW_SHOW);
}

void CDlgTypeA::OnBnClickedButtonShuttercalibA()
{
	// calibration
	// See uniform scene before call this function
	if (m_pTEA) {
		if (m_pTEA->ShutterCalibrationOn()) {

		}
		else {
			AfxMessageBox(L"calib failed");
		}
	}
}


void CDlgTypeA::OnBnClickedButtonReset()
{
	// reset device
	if (m_pTEA) {
		if (m_pTEA->ResetMainBoard()) {

		}
		else {
			AfxMessageBox(L"Failed to reset");
		}
	}
	else {

	}
}


void CDlgTypeA::OnBnClickedButtonGetfpatempA()
{
	// Get fpa temp
	if (m_pTEA) {
		float temp = m_pTEA->GetFpaTemp();
		CString strout;
		strout.Format(_T("fpa temp : %f"), temp);
		AfxMessageBox(strout);
	}
}


void CDlgTypeA::OnBnClickedButtonGetidA()
{
	// Get camera serial number
	if (m_pTEA) {
		UINT id = m_pTEA->GetID();
		CString strout;
		strout.Format(_T("%hd"), id);
		AfxMessageBox(strout);
	}
}


void CDlgTypeA::OnBnClickedButtonShadingCorrectionA()
{
	// correct shading in outer region of image
	if (m_pTEA) {
		if (!m_pTEA->LensShadingCorrection()) {
			AfxMessageBox(L"Correction failed");
		}
	}
}


void CDlgTypeA::OnBnClickedButtonSaveShadingCorrectionA()
{
	// save current lens correction params
	CFileDialog dlg(FALSE);
	if (dlg.DoModal() == IDOK) {
		POSITION pos = dlg.GetStartPosition();
		CString fname = dlg.GetNextPathName(pos);
		if (m_pTEA->SaveCalibration(((CStringA)fname).GetBuffer())) {
			AfxMessageBox(L"file saved");
		}
		else {
			AfxMessageBox(L"file save failed");
		}
	}
}


void CDlgTypeA::OnBnClickedButtonLoadShadingCorrectionA()
{
	// load lens correction params
	CFileDialog dlg(TRUE);
	if (dlg.DoModal() == IDOK) {
		POSITION pos = dlg.GetStartPosition();
		CString fname = dlg.GetNextPathName(pos);
		if (m_pTEA->LoadCalibration(((CStringA)fname).GetBuffer())) {
			AfxMessageBox(L"file loaded");
		}
		else {
			AfxMessageBox(L"file load failed");
		}
	}
}
