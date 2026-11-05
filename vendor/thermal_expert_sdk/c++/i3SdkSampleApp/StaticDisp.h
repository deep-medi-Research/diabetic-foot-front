#pragma once
#include "opencv2\core\core.hpp"
using namespace cv;

class CStaticDisp : public CStatic
{
	DECLARE_DYNAMIC(CStaticDisp)

public:
	CStaticDisp();
	virtual ~CStaticDisp();
	void Draw(Mat &src);
	void Draw(WORD* pRecvImage, int nWidth, int nHeight, int nColorMap, bool _applyGammaCorr, int _gamma, float _gain = 1.f/256.f, float _offset = 0.f);
	void DisplayFPS();

protected:
	DECLARE_MESSAGE_MAP()
	bool	m_bIsDispFPS;
	clock_t	m_clkStart, m_clkCur;
	int		m_iFrameNum;
};


