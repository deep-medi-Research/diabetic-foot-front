
#pragma once
#include "i3system_TE.h"
#include "afxwin.h"
#include "StaticDisp.h"
#include "afxcmn.h"
#include "DlgScanInfo.h"
#include "DlgFullScr.h"
#include "DlgTypeA.h"
#include "DlgTypeB.h"

using namespace TE_NAMESPACE;

class Ci3SdkSampleAppDlg : public CDialogEx
{
public:
	Ci3SdkSampleAppDlg(CWnd* pParent = NULL);
	~Ci3SdkSampleAppDlg();

#ifdef AFX_DESIGN_TIME
	enum { IDD = IDD_DLL_TESTPROGRAM_DIALOG };
#endif

	protected:
	virtual void DoDataExchange(CDataExchange* pDX);

protected:
	HICON m_hIcon;

	virtual BOOL OnInitDialog();
	afx_msg void OnSysCommand(UINT nID, LPARAM lParam);
	afx_msg void OnPaint();
	afx_msg HCURSOR OnQueryDragIcon();
	DECLARE_MESSAGE_MAP()

public:

	bool	m_bIsFullScr;
	CStaticDisp m_stcDisplay;
	CDlgFullScr	m_dlgFullScr;

protected:
	CListBox m_listText;
	CTabCtrl m_ctrlTabType;

	CDlgTypeA m_dlgA;
	CDlgTypeB m_dlgB;
	CWnd*	m_pWndShow;

	bool m_bUsbCon, m_bInit;

public:
	void InsertString(wchar_t* str);
	virtual BOOL PreTranslateMessage(MSG* pMsg);
	afx_msg void OnHScroll(UINT nSBCode, UINT nPos, CScrollBar* pScrollBar);
	virtual LRESULT WindowProc(UINT message, WPARAM wParam, LPARAM lParam);
	afx_msg void OnLButtonDblClk(UINT nFlags, CPoint point);
	afx_msg void OnTcnSelchangeTabType(NMHDR *pNMHDR, LRESULT *pResult);
	afx_msg void OnSize(UINT nType, int cx, int cy);
	afx_msg void OnMove(int x, int y);
	afx_msg void OnLButtonDown(UINT nFlags, CPoint point);
	afx_msg void OnLButtonUp(UINT nFlags, CPoint point);
	int GetDispWidth();
	int GetDispHeight();
	afx_msg void OnBnClickedButtonScan();
	afx_msg void OnDestroy();
};
