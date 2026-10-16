#include "QRScanner.h"

#include "QRModelPaths.hpp"

QRScanner::QRScanner()
{
    const auto paths = QRModelPaths(QRModelDirectory());
    detector = cv::makePtr<cv::wechat_qrcode::WeChatQRCode>(paths[0].string(), paths[1].string(),
                                                          paths[2].string(), paths[3].string());
    // Keep the original full-resolution detector input for compatibility with the
    // previous scanner behavior and its established detection characteristics.
    detector->setScaleFactor(1.0);
}

QRScanner::~QRScanner()
{
}

void QRScanner::decodeSingle(const cv::Mat& img, std::string& qrCode)
{
    qrCode.clear();
    if (img.empty())
    {
        return;
    }
    const std::vector<std::string>& strDecoded = detector->detectAndDecode(img);
    if (strDecoded.size() > 0)
    {
        qrCode = strDecoded[0];
    }
}

void QRScanner::decodeMultiple(const cv::Mat& img, std::string& qrCode)
{
    qrCode.clear();
    if (img.empty())
    {
        return;
    }
    const std::vector<std::string>& strDecoded = detector->detectAndDecode(img);
    if (!strDecoded.empty())
    {
        qrCode = strDecoded.back();
    }
}
