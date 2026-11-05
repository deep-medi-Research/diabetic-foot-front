#pragma once
#include "afxwin.h"
#include "i3system_TE.h"
#include "afxcmn.h"
#include <vector>
#include "DlgScanInfo.h"

using namespace TE_NAMESPACE;
using namespace std;

class CDlgTypeA : public CDialog
{
	DECLARE_DYNAMIC(CDlgTypeA)

public:
	CDlgTypeA(CWnd* pParent = NULL);
	virtual ~CDlgTypeA();

#ifdef AFX_DESIGN_TIME
	enum { IDD = IDD_DLGTYPEA };
#endif

protected:
	virtual void DoDataExchange(CDataExchange* pDX);

	DECLARE_MESSAGE_MAP()
public:
	afx_msg BOOL OnEraseBkgnd(CDC* pDC);
private:
	CComboBox m_ctrlCBColorMap;
	TE_A *m_pTEA;
	int m_iHnd_Dev;
	CWnd *m_pParent;
	bool m_bAGC;
	int m_iOffset, m_iOffsetOut, m_iGain, m_iLimitGain;
	float m_fGain, m_fLimitGain;
	int m_iColorMap;

	bool m_bStopRecv;
	bool m_bIsPlaying;
	bool m_bPause;
	HANDLE	m_hPauseEvent;
	HANDLE	m_hOpEndEvent;
	CSliderCtrl m_ctrlSldGain;
	CSliderCtrl m_ctrlSldOffset;

	static UINT RecvThread(LPVOID lpvoid);

public:
	virtual BOOL OnInitDialog();
	void SetParent(CWnd *_pParent) { m_pParent = _pParent; }
	afx_msg void OnCbnSelchangeComboColormapA();
	afx_msg void OnBnClickedCheckAgcA();
	afx_msg void OnBnClickedButtonOpenA();
	afx_msg void OnBnClickedButtonCloseA();
	afx_msg void OnBnClickedButtonStartA();
	afx_msg void OnBnClickedButtonStopA();
	afx_msg void OnBnClickedButtonRecvA();
	afx_msg void OnHScroll(UINT nSBCode, UINT nPos, CScrollBar* pScrollBar);
	LRESULT OnUpdateData(WPARAM _wparam, LPARAM _lparam);
	int GetImageWidth();
	int GetImageHeight();
	void CalcTemp(float *_pfTempBuf);
#ifdef _BK
	void GetData(WORD *_buf);
	WORD GetPt100();
#endif // _BK

private:
	CSpinButtonCtrl m_ctrlSpinCam;
	CWinThread *m_pThrd;
public:
	virtual BOOL PreTranslateMessage(MSG* pMsg);
	afx_msg void OnLButtonDown(UINT nFlags, CPoint point);
	afx_msg void OnLButtonUp(UINT nFlags, CPoint point);
	afx_msg void OnPaint();
	afx_msg void OnDestroy();
	afx_msg void OnBnClickedCheckAieA();
	afx_msg void OnEnChangeEditEmissivityA();
	afx_msg void OnEnChangeEditCamnum();
	afx_msg void OnBnClickedButtonScanA();
private:
	CSliderCtrl m_sldCtrlAIE;
	float m_fEmissivity;
	CDlgScanInfo m_dlgScan;
	WORD *m_pImgBuf;

public:
	afx_msg void OnBnClickedButtonShuttercalibA();
	afx_msg void OnBnClickedButtonReset();
	afx_msg void OnBnClickedButtonGetfpatempA();
	afx_msg void OnBnClickedButtonGetidA();
	afx_msg void OnBnClickedButtonShadingCorrectionA();
	afx_msg void OnBnClickedButtonSaveShadingCorrectionA();
	afx_msg void OnBnClickedButtonLoadShadingCorrectionA();
};
