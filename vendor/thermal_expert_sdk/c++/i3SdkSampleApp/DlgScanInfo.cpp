#include "stdafx.h"
#include "i3SdkSampleApp.h"
#include "DlgScanInfo.h"
#include "afxdialogex.h"

IMPLEMENT_DYNAMIC(CDlgScanInfo, CDialog)

CDlgScanInfo::CDlgScanInfo(CWnd* pParent /*=NULL*/)
	: CDialog(IDD_DLGSCANINFO, pParent)
{
}

CDlgScanInfo::~CDlgScanInfo()
{
}

void CDlgScanInfo::DoDataExchange(CDataExchange* pDX)
{
	CDialog::DoDataExchange(pDX);
}


BEGIN_MESSAGE_MAP(CDlgScanInfo, CDialog)
	ON_WM_CREATE()
	ON_BN_CLICKED(IDC_BUTTON_REFRESH, &CDlgScanInfo::OnBnClickedButtonRefresh)
END_MESSAGE_MAP()


int CDlgScanInfo::OnCreate(LPCREATESTRUCT lpCreateStruct)
{
	if (CDialog::OnCreate(lpCreateStruct) == -1)
		return -1;

	memset(m_pConOrder, 0, MAX_USB_NUM * sizeof(BYTE));

	CRect rectDlg;
	GetClientRect(rectDlg);

	RECT	rect;
	rect.bottom = rectDlg.bottom - 50;
	rect.top = rectDlg.top + 10;
	rect.left = rectDlg.left + 10;
	rect.right = rectDlg.right - 10;
	m_ctrlListScan.Create(WS_CHILD | WS_VISIBLE | WS_VSCROLL | WS_BORDER, rect, this, IDC_LIST_SCANINFO);
	Scan();
	return 1;
}

void CDlgScanInfo::OnBnClickedButtonRefresh()
{
	m_ctrlListScan.ResetContent();
	Scan();
}

enum TE {
	TE_Q1 = I3_TE_Q1,
	TE_EQ1 = I3_TE_EQ1,
	TE_V1 = I3_TE_V1,
	TE_EV1 = I3_TE_EV1,
	TE_EQ2 = I3_TE_EQ2,
	TE_EV2 = I3_TE_EV2,
};

void CDlgScanInfo::Scan()
{
	// scan
	i3::SCANINFO *pDev_Con = new i3::SCANINFO[MAX_USB_NUM];
	int nRes = i3::ScanTE(pDev_Con);
	if (nRes == 1) {
		for (int i = 0, insertOrder = 0; i < MAX_USB_NUM; i++) {
			if (pDev_Con[i].bDevCon) {
				// scan output
				CString strScan;
				if (pDev_Con[i].nProdVer == TE_Q1) {
					strScan.Format(_T("Q1-%06d, hnd_dev : %d"), pDev_Con[i].nCoreID, i);
				}
				else if (pDev_Con[i].nProdVer == TE_EQ1) {
					strScan.Format(_T("EQ1-%06d  hnd_dev : %d"), pDev_Con[i].nCoreID, i);
				}
				else if (pDev_Con[i].nProdVer == TE_V1) {
					strScan.Format(_T("V1-%06d  hnd_dev : %d"), pDev_Con[i].nCoreID, i);
				}
				else if (pDev_Con[i].nProdVer == TE_EV1) {
					strScan.Format(_T("EV1-%06d  hnd_dev : %d"), pDev_Con[i].nCoreID, i);
				}
				else if (pDev_Con[i].nProdVer == TE_EQ2) {
					strScan.Format(_T("EQ2-%06d  hnd_dev : %d"), pDev_Con[i].nCoreID, i);
				}
				else if (pDev_Con[i].nProdVer == TE_EV2) {
					strScan.Format(_T("EV2-%06d  hnd_dev : %d"), pDev_Con[i].nCoreID, i);
				}
				m_pConOrder[insertOrder++] = i;
				m_ctrlListScan.InsertString(m_ctrlListScan.GetCount(), strScan);
			}
		}
	}
	delete pDev_Con;
}
