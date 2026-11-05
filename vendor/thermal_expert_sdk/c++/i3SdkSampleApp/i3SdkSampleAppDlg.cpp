
// Dll_TestProgramDlg.cpp : 구현 파일
//

#include "stdafx.h"
#include "i3SdkSampleApp.h"
#include "i3SdkSampleAppDlg.h"
#include "afxdialogex.h"
#include "opencv2\core\core.hpp"
#include "opencv2\highgui\highgui.hpp"
#include "DataConversion.h"

using namespace cv;

#ifdef _DEBUG
#define new DEBUG_NEW
#endif

class CAboutDlg : public CDialogEx
{
public:
	CAboutDlg();

#ifdef AFX_DESIGN_TIME
	enum { IDD = IDD_ABOUTBOX };
#endif

	protected:
	virtual void DoDataExchange(CDataExchange* pDX);

protected:
	DECLARE_MESSAGE_MAP()
};

CAboutDlg::CAboutDlg() : CDialogEx(IDD_ABOUTBOX)
{
}

void CAboutDlg::DoDataExchange(CDataExchange* pDX)
{
	CDialogEx::DoDataExchange(pDX);
}

BEGIN_MESSAGE_MAP(CAboutDlg, CDialogEx)
	ON_WM_PAINT()
END_MESSAGE_MAP()


Ci3SdkSampleAppDlg::Ci3SdkSampleAppDlg(CWnd* pParent /*=NULL*/)
	: CDialogEx(IDD_DLL_TESTPROGRAM_DIALOG, pParent)
{
	m_hIcon = AfxGetApp()->LoadIcon(IDR_MAINFRAME);
	m_pWndShow = NULL;
	m_bIsFullScr = false;
	m_bInit = true;
	m_bUsbCon = false;
}

Ci3SdkSampleAppDlg::~Ci3SdkSampleAppDlg()
{
}

void Ci3SdkSampleAppDlg::DoDataExchange(CDataExchange* pDX)
{
	CDialogEx::DoDataExchange(pDX);
	DDX_Control(pDX, IDC_STATIC_DISPLAY, m_stcDisplay);
	DDX_Control(pDX, IDC_LIST_TEXT, m_listText);
	DDX_Control(pDX, IDC_TAB_TYPE, m_ctrlTabType);
}

BEGIN_MESSAGE_MAP(Ci3SdkSampleAppDlg, CDialogEx)
	ON_WM_SYSCOMMAND()
	ON_WM_PAINT()
	ON_WM_QUERYDRAGICON()
	ON_WM_MOUSEMOVE()
	ON_WM_HSCROLL()
	ON_WM_LBUTTONDBLCLK()
	ON_NOTIFY(TCN_SELCHANGE, IDC_TAB_TYPE, &Ci3SdkSampleAppDlg::OnTcnSelchangeTabType)
	ON_WM_SIZE()
	ON_WM_MOVE()
	ON_WM_LBUTTONDOWN()
	ON_WM_LBUTTONUP()
	ON_BN_CLICKED(IDC_BUTTON_SCAN, &Ci3SdkSampleAppDlg::OnBnClickedButtonScan)
	ON_WM_DESTROY()
END_MESSAGE_MAP()


BOOL Ci3SdkSampleAppDlg::OnInitDialog()
{
	CDialogEx::OnInitDialog();

	ASSERT((IDM_ABOUTBOX & 0xFFF0) == IDM_ABOUTBOX);
	ASSERT(IDM_ABOUTBOX < 0xF000);

	CMenu* pSysMenu = GetSystemMenu(FALSE);
	if (pSysMenu != NULL)
	{
		BOOL bNameValid;
		CString strAboutMenu;
		bNameValid = strAboutMenu.LoadString(IDS_ABOUTBOX);
		ASSERT(bNameValid);
		if (!strAboutMenu.IsEmpty())
		{
			pSysMenu->AppendMenu(MF_SEPARATOR);
			pSysMenu->AppendMenu(MF_STRING, IDM_ABOUTBOX, strAboutMenu);
		}
	}

	SetIcon(m_hIcon, TRUE);
	SetIcon(m_hIcon, FALSE);

	// Title
	SetWindowText(L"i3SdkSampleApp 2.10.9");

	CRect rect;
	m_ctrlTabType.GetClientRect(&rect);

	m_dlgA.Create(IDD_DLGTYPEA, &m_ctrlTabType);
	m_dlgA.SetWindowPos(NULL, 5, 25, rect.Width() - 12, rect.Height() - 33, SWP_SHOWWINDOW | SWP_NOZORDER);
	m_dlgA.SetParent(this);
	m_pWndShow = &m_dlgA;

	m_dlgB.Create(IDD_DLGTYPEB, &m_ctrlTabType);
	m_dlgB.SetWindowPos(NULL, 5, 25, rect.Width() - 12, rect.Height() - 33, SWP_NOZORDER);
	m_dlgB.SetParent(this);

	m_ctrlTabType.InsertItem(0, L"Type A");
	m_ctrlTabType.InsertItem(1, L"Type B");

	OnTcnSelchangeTabType(NULL, NULL);

	UpdateData(TRUE);

	UpdateData(FALSE);

	SetColorTable();

	if (!RegisterTE(this->GetSafeHwnd())) {
		AfxMessageBox(L"register failed");
	}
	return TRUE;  // 포커스를 컨트롤에 설정하지 않으면 TRUE를 반환합니다.
}

void Ci3SdkSampleAppDlg::OnSysCommand(UINT nID, LPARAM lParam)
{
	if ((nID & 0xFFF0) == IDM_ABOUTBOX)
	{
		CAboutDlg dlgAbout;
		dlgAbout.DoModal();
	}
	else
	{
		CDialogEx::OnSysCommand(nID, lParam);
	}
}

void Ci3SdkSampleAppDlg::OnPaint()
{
	if (IsIconic())
	{
		CPaintDC dc(this);

		SendMessage(WM_ICONERASEBKGND, reinterpret_cast<WPARAM>(dc.GetSafeHdc()), 0);

		int cxIcon = GetSystemMetrics(SM_CXICON);
		int cyIcon = GetSystemMetrics(SM_CYICON);
		CRect rect;
		GetClientRect(&rect);
		int x = (rect.Width() - cxIcon + 1) / 2;
		int y = (rect.Height() - cyIcon + 1) / 2;

		dc.DrawIcon(x, y, m_hIcon);
	}
	else
	{
		CDialogEx::OnPaint();
	}
}

HCURSOR Ci3SdkSampleAppDlg::OnQueryDragIcon()
{
	return static_cast<HCURSOR>(m_hIcon);
}

void Ci3SdkSampleAppDlg::InsertString(wchar_t *str)
{	
	m_listText.InsertString(m_listText.GetCount(), str);
	m_listText.SetCurSel(m_listText.GetCount() - 1);
	m_listText.SetCurSel(-1);
}

BOOL Ci3SdkSampleAppDlg::PreTranslateMessage(MSG* pMsg)
{
	if (pMsg->message == WM_KEYDOWN) {
		if (pMsg->wParam == 'F') {
			m_stcDisplay.DisplayFPS();
			return TRUE;
		}
		else if (pMsg->wParam == 'M') {
			m_dlgFullScr.DestroyWindow();
			m_bIsFullScr = m_dlgFullScr.Create(IDD_DLG_FULLSCR, this);
			if (m_bIsFullScr) {
				m_dlgFullScr.MoveWindow(CRect(0, 0, (LONG)::GetSystemMetrics(SM_CXSCREEN), (LONG)::GetSystemMetrics(SM_CYSCREEN)));
				m_dlgFullScr.ShowWindow(TRUE);
			}
			else {
				m_dlgFullScr.DestroyWindow();
			}
			return TRUE;
		}
		else if (pMsg->wParam == VK_ESCAPE) {
			m_dlgA.PreTranslateMessage(pMsg);
			return TRUE;
		}
	}

	return CDialogEx::PreTranslateMessage(pMsg);
}

void Ci3SdkSampleAppDlg::OnHScroll(UINT nSBCode, UINT nPos, CScrollBar* pScrollBar)
{
	CDialogEx::OnHScroll(nSBCode, nPos, pScrollBar);
}

LRESULT Ci3SdkSampleAppDlg::WindowProc(UINT message, WPARAM wParam, LPARAM lParam)
{
	CString	strOut;
	int nRes = WindowProcTE(message, wParam, lParam);

	if (nRes == 1)
	{
		if (!m_bUsbCon || m_bInit) {
			InsertString(L"Window Proc \r\n: USB device connection available");
			m_bUsbCon = true;
		}
		m_bInit = false;
	}
	else if (nRes == 2) {
		InsertString(L"Window Proc \r\n: USB device connection not available");
		m_bInit = false;
	}
	else if (nRes == 3) {
		if (m_bUsbCon || m_bInit) {
			InsertString(L"Window Proc \r\n: USB device removed");
			m_bUsbCon = false;
		}
		m_bInit = false;
	}
	else if (nRes == 4) {
		InsertString(L"Window Proc \r\n: Failed to remove USB device");
		m_bInit = false;
	}

	return CDialogEx::WindowProc(message, wParam, lParam);
}

void Ci3SdkSampleAppDlg::OnLButtonDblClk(UINT nFlags, CPoint point)
{
	CRect rect;
	GetDlgItem(IDC_STATIC_DISPLAY)->GetWindowRect(rect);
	ScreenToClient(rect);
	
	if (rect.PtInRect(point)) {
		if (m_ctrlTabType.GetCurSel() == 0) {

			// Display temperature for clicked pixel
			UINT x, y;
			x = (UINT)((point.x - rect.left) * (float)m_dlgA.GetImageWidth() / (float)rect.Width() + 0.5);
			y = (UINT)((point.y - rect.top) * (float)m_dlgA.GetImageHeight() / (float)rect.Height() + 0.5);

#ifdef _BK
			WORD *pDataBuf = new WORD[m_dlgA.GetImageHeight() * m_dlgA.GetImageWidth()];
			m_dlgA.GetData(pDataBuf);
			WORD data = pDataBuf[x + y * m_dlgA.GetImageWidth()];
			WORD pt100 = m_dlgA.GetPt100();
			CString str;
			str.Format(_T("x : %d, y : %d, data : %hu, PT100 : %hu"), x, y, data, pt100);
			wchar_t *pwch = (T2W)(str.GetBuffer(0));
			InsertString(pwch);
#else
			float *pTempBuf = new float[m_dlgA.GetImageHeight() * m_dlgA.GetImageWidth()];
			m_dlgA.CalcTemp(pTempBuf);
			float fTemp = pTempBuf[x + y * m_dlgA.GetImageWidth()];
			CString str;
			str.Format(_T("x : %d, y : %d, Temperature : %.1f℃"), x, y, fTemp);
			wchar_t *pwch = (T2W)(str.GetBuffer(0));
			InsertString(pwch);

			delete pTempBuf;
#endif // _BK
		}
		else if (m_ctrlTabType.GetCurSel() == 1) {
			// Display temperature for clicked pixel
			UINT x, y;
			x = (UINT)((point.x - rect.left) * (float)m_dlgB.GetImageWidth() / (float)rect.Width() + 0.5);
			y = (UINT)((point.y - rect.top) * (float)m_dlgB.GetImageHeight() / (float)rect.Height() + 0.5);

			float fTemp = m_dlgB.CalcTemp(x, y);
			CString str;
			str.Format(_T("x : %d, y : %d, Temperature : %.1f℃"), x, y, fTemp);
			wchar_t *pwch = (T2W)(str.GetBuffer(0));
			InsertString(pwch);
		}
	}
	else {
		CDialogEx::OnLButtonDblClk(nFlags, point);
	}	
}


void Ci3SdkSampleAppDlg::OnTcnSelchangeTabType(NMHDR *pNMHDR, LRESULT *pResult)
{
	CRect rect;
	m_ctrlTabType.GetWindowRect(&rect);
	if (m_pWndShow != NULL)
	{
		m_pWndShow->ShowWindow(SW_HIDE);
		m_pWndShow = NULL;
	}
	
	if (m_ctrlTabType.GetCurSel() == 0) {
		m_dlgA.ShowWindow(SW_SHOW);
		m_pWndShow = &m_dlgA;
	}
	else if (m_ctrlTabType.GetCurSel() == 1) {
		m_dlgB.ShowWindow(SW_SHOW);
		m_pWndShow = &m_dlgB;
	}
}

void Ci3SdkSampleAppDlg::OnSize(UINT nType, int cx, int cy)
{
	CDialogEx::OnSize(nType, cx, cy);
}


void Ci3SdkSampleAppDlg::OnMove(int x, int y)
{
	CDialogEx::OnMove(x, y);
}

void Ci3SdkSampleAppDlg::OnLButtonDown(UINT nFlags, CPoint point)
{
	CRect rect;
	GetDlgItem(IDC_STATIC_DISPLAY)->GetWindowRect(rect);
	ScreenToClient(rect);
	if (rect.PtInRect(point)) {
		if (m_ctrlTabType.GetCurSel() == 0) {
			point.x -= rect.left;
			point.y -= rect.top;
			m_dlgA.OnLButtonDown(nFlags, point);
		}
	}
	CDialogEx::OnLButtonDown(nFlags, point);
}


void Ci3SdkSampleAppDlg::OnLButtonUp(UINT nFlags, CPoint point)
{
	CRect rect;
	GetDlgItem(IDC_STATIC_DISPLAY)->GetWindowRect(rect);
	ScreenToClient(rect);
	if (rect.PtInRect(point)) {
		if (m_ctrlTabType.GetCurSel() == 0) {
			point.x -= rect.left;
			point.y -= rect.top;
			m_dlgA.OnLButtonUp(nFlags, point);
		}
	}
	CDialogEx::OnLButtonUp(nFlags, point);
}

int Ci3SdkSampleAppDlg::GetDispWidth()
{
	CRect rect;
	GetDlgItem(IDC_STATIC_DISPLAY)->GetWindowRect(rect);
	ScreenToClient(rect);
	return rect.Width();
}

int Ci3SdkSampleAppDlg::GetDispHeight()
{
	CRect rect;
	GetDlgItem(IDC_STATIC_DISPLAY)->GetWindowRect(rect);
	ScreenToClient(rect);
	return rect.Height();
}

void Ci3SdkSampleAppDlg::OnBnClickedButtonScan()
{
}


void Ci3SdkSampleAppDlg::OnDestroy()
{
	CDialogEx::OnDestroy();

	UnregisterTE();
}
