#include "stdafx.h"
#include "i3SdkSampleApp.h"
#include "DlgFullScr.h"
#include "afxdialogex.h"
#include "i3SdkSampleAppDlg.h"


IMPLEMENT_DYNAMIC(CDlgFullScr, CDialog)

CDlgFullScr::CDlgFullScr(CDialog* pParent /*=NULL*/)
	: CDialog(IDD_DLG_FULLSCR, pParent)
{
	m_pParent = pParent;
}

CDlgFullScr::~CDlgFullScr()
{
}

void CDlgFullScr::DoDataExchange(CDataExchange* pDX)
{
	CDialog::DoDataExchange(pDX);
}

void CDlgFullScr::Draw(WORD * pRecvImage, int nWidth, int nHeight, int nColorMap, float _gain, float _offset)
{
	m_bIsDrawing = true;
	cv::Mat img(nHeight, nWidth, CV_16U, pRecvImage);
	Mat img8;
	img.convertTo(img8, CV_8UC1, _gain, _offset);
	Mat in[] = { img8, img8, img8 };
	Mat img8uc3;
	merge(in, 3, img8uc3);

	Mat matColor;
	ApplyColorMap(img8uc3, matColor, nColorMap);
	Draw(matColor);
}

void CDlgFullScr::Draw(Mat & src)
{
	CRect rect;
	GetClientRect(&rect);
	int nScrWidth = rect.Width();
	rect.right = (LONG)(rect.Height() / 0.75);
	Mat matResize;
	resize(src, matResize, Size(rect.Width(), rect.Height()));
	CImage img;
	int nRes = Mat2CImage(matResize, img);
	if (nRes == -1) {

	}
	else {
		//m_iFrameNum++;
		CDC *pDC = GetDC(), memDC;
		memDC.CreateCompatibleDC(pDC);

		//img.Draw(pDC->GetSafeHdc(), rect, Gdiplus::InterpolationModeBilinear);
		CImage imgTemp;
		imgTemp.Create(rect.Width(), rect.Height(), 24);
		CBitmap *pOldBmp = memDC.SelectObject(CBitmap::FromHandle(imgTemp));
		img.Draw(memDC.GetSafeHdc(), rect, Gdiplus::InterpolationModeBilinear);
		//if (m_bIsDispFPS) {
		//	m_clkCur = clock();
		//	CString strFPS;
		//	strFPS.Format(_T("fps : %.1f"), (float)m_iFrameNum / (((float)(m_clkCur - m_clkStart)) / (float)(CLOCKS_PER_SEC)));
		//	memDC.TextOutW(10, 10, strFPS);
		//}
		pDC->BitBlt((int)(nScrWidth - rect.Width())/2, 0, rect.Width(), rect.Height(), &memDC, 0, 0, SRCCOPY);
		memDC.SelectObject(pOldBmp);
		memDC.DeleteDC();
		ReleaseDC(pDC);
	}
	m_bIsDrawing = false;
}


BEGIN_MESSAGE_MAP(CDlgFullScr, CDialog)
END_MESSAGE_MAP()


BOOL CDlgFullScr::PreTranslateMessage(MSG* pMsg)
{
	if (pMsg->message == WM_KEYDOWN) {
		if (pMsg->wParam == 77) {
			while (m_bIsDrawing) {
				Sleep(5);
			}
			((Ci3SdkSampleAppDlg*)GetParent())->m_bIsFullScr = false;
			this->DestroyWindow();
			return TRUE;
		}
	}

	return CDialog::PreTranslateMessage(pMsg);
}
