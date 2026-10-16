#pragma once

#include <atomic>
#include <mutex>
#include <string_view>

extern "C"
{
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libavutil/imgutils.h>
#include <libavutil/time.h>
#include <libswscale/swscale.h>
};

#include <QThread>
#include <QMutex>
#include <QtConcurrent/QtConcurrent>
#include <QFuture>
#include <QThreadPool>

#include "ApiDefs.hpp"
#include "ConfigDate.h"
#include "ScannerBase.hpp"
#include "StreamTiming.hpp"
#include <opencv2/core/mat.hpp>

class QRCodeForStream final :
    public QThread,
    public ScannerBase
{
    Q_OBJECT
public:
    QRCodeForStream(QObject* parent = nullptr);
    ~QRCodeForStream();
    Q_DISABLE_COPY_MOVE(QRCodeForStream)

    void setLoginInfo1(const std::string_view uid, const std::string_view stoken, const std::string_view mid);
    void setKuroLoginInfo(const std::string& uid, const std::string& token);
    void setUrl(const std::string& url, const std::map<std::string, std::string> heard = {});
    auto init() -> bool;
    void run();
    void stop();
    void continueLastLogin();
    void setScanInterval(int intervalMs);

Q_SIGNALS:
    void streamReady(quint64 generation);
    void loginResults(const ScanRet ret, quint64 generation);
    void loginConfirm(const GameType gameType, bool b, quint64 generation);

private:
    std::mutex mtx;
    void LoginOfficial();
    void setStreamHW();
    bool convertFrame(cv::Mat& img);
    void scanFrame(const cv::Mat& img);
    void submitFrame(cv::Mat img);
    static int interruptStream(void* opaque);
    std::string streamUrl{};
    ConfigDate* m_config;
    ScanRet ret = ScanRet::UNKNOW;
    AVDictionary* pAvdictionary;
    AVFormatContext* pAVFormatContext;
    AVCodecContext* pAVCodecContext;
    SwsContext* pSwsContext;
    AVFrame* pAVFrame;
    AVPacket* pAVPacket;
    int videoStreamIndex{ 0 };
    int videoStreamWidth{};
    int videoStreamHeight{};
    int inputWidth{};
    int inputHeight{};
    int inputFormat{ AV_PIX_FMT_NONE };
    const int threadNumber{ 3 };
    cv::Mat pendingFrame;
    StreamSubmitClock submitClock;
    StreamDeadline readDeadline;
    bool readyEmitted{ false };
    QThreadPool threadPool;
    std::atomic<bool> m_stop;
    std::atomic<bool> m_cancelled{ false };
};
