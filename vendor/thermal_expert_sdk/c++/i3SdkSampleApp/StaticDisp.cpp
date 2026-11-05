#include "stdafx.h"
#include "i3SdkSampleApp.h"
#include "StaticDisp.h"
#include "opencv2\imgproc\imgproc.hpp"
#include "DataConversion.h"
#include "i3system_TE.h"

IMPLEMENT_DYNAMIC(CStaticDisp, CStatic)

CStaticDisp::CStaticDisp()
{
	m_bIsDispFPS = false;
}

CStaticDisp::~CStaticDisp()
{
}

void CStaticDisp::Draw(Mat & src)
{
	CRect rect;
	GetClientRect(&rect);
	Mat matResize;
	resize(src, matResize, Size(rect.Width(), rect.Height()));
	CImage img;
	int nRes = Mat2CImage(matResize, img);
	if (nRes == -1) {

	}
	else {
		m_iFrameNum++;
		CDC *pDC = GetDC(), memDC;
		memDC.CreateCompatibleDC(pDC);

		CImage imgTemp;
		imgTemp.Create(rect.Width(), rect.Height(), 24);
		CBitmap *pOldBmp = memDC.SelectObject(CBitmap::FromHandle(imgTemp));
		img.Draw(memDC.GetSafeHdc(), rect, Gdiplus::InterpolationModeBilinear);
		if (m_bIsDispFPS) {
			m_clkCur = clock();
			CString strFPS;
			strFPS.Format(_T("fps : %.1f"), (float)m_iFrameNum / (((float)(m_clkCur - m_clkStart)) / (float)(CLOCKS_PER_SEC)));
			memDC.TextOutW(10, 10, strFPS);
		}
		
		pDC->BitBlt(0, 0, rect.Width(), rect.Height(), &memDC, 0, 0, SRCCOPY);		

		memDC.SelectObject(pOldBmp);
		memDC.DeleteDC();
		ReleaseDC(pDC);
	}
}

void CStaticDisp::Draw(WORD * pRecvImage, int nWidth, int nHeight, int nColorMap, bool _applyGammaCorr, int _gamma, float _gain, float _offset)
{
	int size = nWidth * nHeight;
	UCHAR *src8 = new UCHAR[size];
	UCHAR *src8uc3 = new UCHAR[size*3];
	float gain = _gain, offset = _offset;
	for (int i = size; i--;) {
		int temp = (int)(pRecvImage[i] * gain + offset);
		src8[i] = (uchar)(max(min(255, temp), 0));
	}

	i3::ApplyColorMap(src8uc3, src8, nColorMap, nWidth, nHeight);
	cv::Mat matColor(nHeight, nWidth, CV_8UC3, src8uc3);
		
	Draw(matColor);
	delete src8;
	delete src8uc3;
}

void CStaticDisp::DisplayFPS()
{
	m_bIsDispFPS = !m_bIsDispFPS;
	if (m_bIsDispFPS) {
		m_clkStart = clock();	
		m_iFrameNum = 0;
	}
}

BEGIN_MESSAGE_MAP(CStaticDisp, CStatic)
END_MESSAGE_MAP()