#pragma once

#include "opencv2\core\core.hpp"

void SetColorTable();

#ifdef __OPENCV_CORE_HPP__
void ApplyColorMap(cv::Mat &src, cv::Mat &dst, int nColor);
int Mat2CImage(cv::Mat &src, CImage &dst);
#endif // __OPENCV_CORE_HPP__

// Celsius -> Fahrenheit
float Cel2Fah(float Celsius);