#pragma once

#include "i3system_TE.h"
#include "afxcmn.h"
#include "afxwin.h"
#ifdef ASTROHN
#include "Astrohn_TE.h"
#endif

using namespace TE_NAMESPACE;

class CDlgTypeB : public CDialog
{
	DECLARE_DYNAMIC(CDlgTypeB)

public:
	CDlgTypeB(CWnd* pParent = NULL);
	virtual ~CDlgTypeB();

#ifdef AFX_DESIGN_TIME
	enum { IDD = IDD_DLGTYPEB };
#endif

protected:
	virtual void DoDataExchange(CDataExchange* pDX);

	DECLARE_MESSAGE_MAP()
public:
	afx_msg BOOL OnEraseBkgnd(CDC* pDC);
	afx_msg void OnBnClickedButtonOpenB();
	afx_msg void OnBnClickedButtonCloseB();
	afx_msg void OnBnClickedButtonStart();
	afx_msg void OnBnClickedButtonStop();
	afx_msg void OnBnClickedButtonRecv();
	virtual BOOL OnInitDialog();
	afx_msg void OnBnClickedCheckAgcB();
	afx_msg void OnHScroll(UINT nSBCode, UINT nPos, CScrollBar* pScrollBar);

	void SetParent(CWnd *_pParent) { m_pParent = _pParent; }

private:
	TE_B *m_pTEB;
	int m_iHnd_Dev;
	CWnd *m_pParent;
	bool m_bAGC;
	int m_iOffset, m_iOffsetOut, m_iGain;
	float m_fGain;
	int m_iColorMap;

	static UINT RecvThread(LPVOID lpvoid);
	bool m_bStopRecv;
	bool m_bIsPlaying;
	HANDLE	m_hPauseEvent;
	HANDLE	m_hOpEndEvent;
	bool	m_bPause;
	CSliderCtrl m_ctrlSldOffset;
	CSliderCtrl m_ctrlSldGain;
	CComboBox m_ctrlCBColorMap;
	CSpinButtonCtrl m_ctrlSpinCamNumB;
	float m_fEmissivity;
	CWinThread *m_pThrd;
	int m_nModel;
	CComboBox m_cb_res;
public:
	afx_msg void OnCbnSelchangeComboColormapB();
	afx_msg void OnBnClickedButtonShuttercalib();
	afx_msg void OnBnClickedButtonUpdatedead();
	int GetImageWidth();
	int GetImageHeight();
	float CalcTemp(int _x, int _y);
	virtual BOOL PreTranslateMessage(MSG* pMsg);
	afx_msg void OnPaint();
	afx_msg void OnDestroy();
	afx_msg void OnBnClickedCheckAieB();
	afx_msg void OnEnChangeEditEmissivityB();
	afx_msg void OnCbnSelchangeComboResB();
	afx_msg void OnBnClickedButtonGetfpatemp();
};
