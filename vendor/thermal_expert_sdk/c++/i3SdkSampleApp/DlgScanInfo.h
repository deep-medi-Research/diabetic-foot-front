#pragma once
#include "afxwin.h"

#include "i3system_TE.h"

class CDlgScanInfo : public CDialog
{
	DECLARE_DYNAMIC(CDlgScanInfo)

public:
	CDlgScanInfo(CWnd* pParent = NULL);
	virtual ~CDlgScanInfo();

#ifdef AFX_DESIGN_TIME
	enum { IDD = IDD_DLGSCANINFO };
#endif

protected:
	virtual void DoDataExchange(CDataExchange* pDX);

	DECLARE_MESSAGE_MAP()
	CListBox m_ctrlListScan;
	BYTE	m_pConOrder[MAX_USB_NUM];
	CWnd    *m_pParent;
public:
	afx_msg int OnCreate(LPCREATESTRUCT lpCreateStruct);
	void SetParent(CWnd *pParent) { m_pParent = pParent; }
	afx_msg void OnBnClickedButtonRefresh();

	// Scan connected TE and show result in list box
	void Scan();
};