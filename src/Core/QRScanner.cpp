#include "QRScanner.h"

#include "QRModelPaths.hpp"

QRScanner::QRScanner()
{
    const auto paths = QRModelPaths(QRModelDirectory());
    detector = cv::makePtr<cv::wechat_qrcode::WeChatQRCode>(paths[0].string(), paths[1].string(),
                                                          paths[2].string(), paths[3].string());
    // Scales the image handed to the detector network. The library's own default
    // targets an area of 160000 px, but 1.0 fed it the entire 1280x720 scan frame
    // (921600 px, 5.76x more) and cost ~88 ms per decode on a busy screen instead
    // of ~7 ms. 0.42*1280 x 0.42*720 == 162500 px, i.e. that documented default.
    // The detection floor is unchanged: codes down to ~44 px inside the scan frame
    // still decode, and the real Wuthering Waves login code measures ~149 px there.
    detector->setScaleFactor(0.42f);
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
