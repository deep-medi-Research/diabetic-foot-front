#pragma once

#include "opencv2\imgproc\imgproc.hpp"
#include "DataConversion.h"

using namespace cv;

class CDlgFullScr : public CDialog
{
	DECLARE_DYNAMIC(CDlgFullScr)

public:
	CDlgFullScr(CDialog* pParent = NULL);
	virtual ~CDlgFullScr();

#ifdef AFX_DESIGN_TIME
	enum { IDD = IDD_DLG_FULLSCR};
#endif

protected:
	virtual void DoDataExchange(CDataExchange* pDX);

	DECLARE_MESSAGE_MAP()

public:
	void Draw(WORD * pRecvImage, int nWidth, int nHeight, int nColorMap, float _gain = 1.f / 256.f, float _offset = 0.f);
	void Draw(Mat &src);
	virtual BOOL PreTranslateMessage(MSG* pMsg);

	bool m_bIsDrawing;
	CDialog* m_pParent;
};
