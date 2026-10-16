#include "QRCodeForStream.h"

#include <string>
#include <string_view>
#include <algorithm>

#include "QRScanner.h"
#include "ScannerTask.hpp"
#include "MhyApi.hpp"
#include "StreamFrameConverter.hpp"

QRCodeForStream::QRCodeForStream(QObject* parent) :
    QThread(parent),
    pAvdictionary(nullptr),
    pAVFormatContext(nullptr),
    pSwsContext(nullptr),
    pAVFrame(nullptr),
    pAVPacket(nullptr),
    pAVCodecContext(nullptr),
    m_stop(false)

{
    av_log_set_level(AV_LOG_FATAL);
    m_config = &(ConfigDate::getInstance());
}

QRCodeForStream::~QRCodeForStream()
{
    stop();
    this->requestInterruption();
    this->wait();
    threadPool.waitForDone();
    av_dict_free(&pAvdictionary);
}

void QRCodeForStream::setScanInterval(int intervalMs)
{
    submitClock.setInterval(intervalMs);
}

void QRCodeForStream::setLoginInfo1(const std::string_view uid, const std::string_view stoken, const std::string_view mid)
{
    kuroAccount = false;
    authorizationCancelled.store(false);
    lastTicket.clear();
    lastQrCode.clear();
    this->uid = uid;
    this->gameToken = stoken;
    this->mid = mid;
}

void QRCodeForStream::setKuroLoginInfo(const std::string& uid, const std::string& token)
{
    this->uid = uid;
    kuroAccount = true;
    authorizationCancelled.store(false);
    kuroSession.setAccount(token, resultGeneration);
}

void QRCodeForStream::LoginOfficial()
{
    while (m_stop.load())
    {
        if (readDeadline.expired(av_gettime_relative()))
        {
            WriteScannerLog("stream: no decoded video frame within deadline");
            ret = ScanRet::STREAMERROR;
            break;
        }
        if (av_read_frame(pAVFormatContext, pAVPacket) < 0)
        {
            if (m_stop.load())
            {
                ret = readDeadline.expired(av_gettime_relative()) ? ScanRet::STREAMERROR : ScanRet::LIVESTOP;
            }
            break;
        }
        if (pAVPacket->stream_index != videoStreamIndex)
        {
            av_packet_unref(pAVPacket);
            continue;
        }
        const int sendResult = avcodec_send_packet(pAVCodecContext, pAVPacket);
        // A malformed live packet is recoverable; only valid frames reset the deadline.
        if (sendResult == AVERROR_INVALIDDATA)
        {
            WriteScannerLog("stream: skipping invalid video packet");
            av_packet_unref(pAVPacket);
            continue;
        }
        if (sendResult < 0)
        {
            WriteScannerLog("stream: send packet error=" + std::to_string(sendResult));
            av_packet_unref(pAVPacket);
            ret = ScanRet::STREAMERROR;
            break;
        }
        if (pAVFrame == nullptr)
        {
            std::cerr << "Error allocating frame" << std::endl;
            ret = ScanRet::LIVESTOP;
            break;
        }
        int receiveResult = AVERROR(EAGAIN);
        while (m_stop.load())
        {
            receiveResult = avcodec_receive_frame(pAVCodecContext, pAVFrame);
            if (receiveResult == AVERROR_INVALIDDATA)
            {
                WriteScannerLog("stream: skipping invalid decoded frame");
                if (readDeadline.expired(av_gettime_relative()))
                {
                    ret = ScanRet::STREAMERROR;
                    stop();
                    break;
                }
                continue;
            }
            if (receiveResult < 0)
            {
                break;
            }
            cv::Mat img;
            if (!convertFrame(img))
            {
                ret = ScanRet::STREAMERROR;
                stop();
                break;
            }
#ifdef ERYOU_SCAN_PREVIEW
            cv::imshow("Video_Stream", img);
            cv::waitKey(1);
#endif
            submitFrame(std::move(img));
        }
        if (m_stop.load() && receiveResult != AVERROR(EAGAIN) && receiveResult != AVERROR_EOF &&
            receiveResult != AVERROR_INVALIDDATA)
        {
            WriteScannerLog("stream: receive frame error=" + std::to_string(receiveResult));
            ret = ScanRet::STREAMERROR;
            stop();
        }
        av_frame_unref(pAVFrame);
        av_packet_unref(pAVPacket);
    }
}


int QRCodeForStream::interruptStream(void* opaque)
{
    const auto* scanner = static_cast<QRCodeForStream*>(opaque);
    return scanner->m_cancelled.load() || !scanner->m_stop.load() ||
           scanner->readDeadline.expired(av_gettime_relative());
}

void QRCodeForStream::submitFrame(cv::Mat img)
{
    pendingFrame = std::move(img);
    const auto now = av_gettime_relative();
    if (!m_stop.load() || !submitClock.due(now))
    {
        return;
    }
    // A shallow Mat copy retains the frame if all workers are busy; no queue grows.
    if (threadPool.tryStart([this, img = pendingFrame]() { scanFrame(img); }))
    {
        submitClock.submitted(now);
        pendingFrame.release();
    }
}

void QRCodeForStream::scanFrame(const cv::Mat& img)
{
    try
    {
        if (!m_stop.load())
        {
            return;
        }
        thread_local QRScanner qrScanners;
        std::string str;
        qrScanners.decodeSingle(img, str);
        std::unique_lock<std::mutex> lock(mtx, std::try_to_lock);
        if (!lock.owns_lock() || !m_stop.load())
        {
            return;
        }
        if (kuroAccount)
        {
            if (!KuroApi::isGameQr(str)) return;
            const auto status = kuroSession.prepare(str, resultGeneration);
            if (!m_stop.load()) return;
            if (status == ScanRet::UNKNOW) return;
            stop();
            if (status == ScanRet::SUCCESS) Q_EMIT loginConfirm(GameType::WutheringWaves, false, resultGeneration);
            else Q_EMIT loginResults(status, resultGeneration);
            return;
        }
        std::string ticket;
        if (!parseOfficialQRCode(str, ticket) || lastTicket == ticket)
        {
            return;
        }
        const std::string passportQrUrl = PandaScanQRCode(scanUrl, ticket, gameType);
        if (!m_stop.load()) return;
        const bool success = !passportQrUrl.empty();
        if (success)
        {
            lastQrCode = passportQrUrl;
        }
        else
        {
            Q_EMIT loginResults(ScanRet::FAILURE_1, resultGeneration);
        }
        if (success)
        {
            lastTicket = ticket;
            stop();
            Q_EMIT loginConfirm(gameType, false, resultGeneration);
        }
        stop();
    }
    catch (...)
    {
        LogScannerException("stream: QR worker");
        stop();
        Q_EMIT loginResults(ScanRet::STREAMERROR, resultGeneration);
    }
}

void QRCodeForStream::setStreamHW()
{
    if (pAVFrame->width < pAVFrame->height ||
        pAVFrame->height == 480 ||
        pAVFrame->height == 720)
    {
        videoStreamWidth = pAVFrame->width;
        videoStreamHeight = pAVFrame->height;
    }
    else
    {
        videoStreamWidth = (std::max)(1, static_cast<int>(pAVFrame->width / 1.5));
        videoStreamHeight = (std::max)(1, static_cast<int>(pAVFrame->height / 1.5));
    }
}

bool QRCodeForStream::convertFrame(cv::Mat& img)
{
    if (!pAVFrame || pAVFrame->width <= 0 || pAVFrame->height <= 0 ||
        !av_pix_fmt_desc_get(static_cast<AVPixelFormat>(pAVFrame->format)) ||
        !sws_isSupportedInput(static_cast<AVPixelFormat>(pAVFrame->format)) ||
        av_image_check_size(pAVFrame->width, pAVFrame->height, 0, nullptr) < 0)
    {
        WriteScannerLog("stream: invalid decoded frame");
        return false;
    }
    if (inputWidth != pAVFrame->width || inputHeight != pAVFrame->height || inputFormat != pAVFrame->format)
    {
        inputWidth = pAVFrame->width;
        inputHeight = pAVFrame->height;
        inputFormat = pAVFrame->format;
        WriteScannerLog("stream: frame width=" + std::to_string(inputWidth) +
                        " height=" + std::to_string(inputHeight) + " format=" + std::to_string(inputFormat));
    }
    setStreamHW();
    img.create(videoStreamHeight, videoStreamWidth, CV_8UC3);
    const int result = ConvertStreamFrame(pSwsContext, *pAVFrame, videoStreamWidth, videoStreamHeight,
                                          img.data, static_cast<int>(img.step));
    if (result != videoStreamHeight)
    {
        WriteScannerLog("stream: conversion failed result=" + std::to_string(result));
        return false;
    }
    readDeadline.refresh(av_gettime_relative(), 10000000);
    if (!readyEmitted)
    {
        readyEmitted = true;
        Q_EMIT streamReady(resultGeneration);
    }
    return true;
}

void QRCodeForStream::stop()
{
    m_cancelled.store(true);
    m_stop.store(false);
}

void QRCodeForStream::setUrl(const std::string& url, const std::map<std::string, std::string> heard)
{
    streamUrl = url;
    m_cancelled.store(false);
    av_dict_free(&pAvdictionary);
    for (const auto& it : heard)
    {
        av_dict_set(&pAvdictionary, it.first.c_str(), it.second.c_str(), 0);
    }
    av_dict_set(&pAvdictionary, "probesize", "5000000", 0);
    av_dict_set(&pAvdictionary, "analyzeduration", "2000000", 0);
    av_dict_set(&pAvdictionary, "buffer_size", "1024000", 0);
    av_dict_set(&pAvdictionary, "rw_timeout", "5000000", 0);
}

auto QRCodeForStream::init() -> bool
{
    pAVFormatContext = avformat_alloc_context();
    if (!pAVFormatContext)
    {
        WriteScannerLog("stream: format context allocation failed");
        return false;
    }
    readDeadline.refresh(av_gettime_relative(), 10000000);
    pAVFormatContext->interrupt_callback = { &QRCodeForStream::interruptStream, this };
    int result = avformat_open_input(&pAVFormatContext, streamUrl.c_str(), NULL, &pAvdictionary);
    if (result < 0)
    {
        std::cerr << "Error opening input file" << std::endl;
        WriteScannerLog("stream: open input error=" + std::to_string(result));
        return false;
    }
    result = avformat_find_stream_info(pAVFormatContext, NULL);
    if (result < 0)
    {
        std::cerr << "Error finding stream information" << std::endl;
        WriteScannerLog("stream: find stream info error=" + std::to_string(result));
        return false;
    }
    AVStream* videoStream = nullptr;
    for (int i = 0; i < pAVFormatContext->nb_streams; i++)
    {
        if (pAVFormatContext->streams[i]->codecpar->codec_type == AVMEDIA_TYPE_VIDEO)
        {
            videoStream = pAVFormatContext->streams[i];
            break;
        }
    }
    if (videoStream == nullptr)
    {
        std::cerr << "No video stream found" << std::endl;
        WriteScannerLog("stream: no video stream");
        return false;
    }
    videoStreamIndex = videoStream->index;
    const AVCodec* decoder{ avcodec_find_decoder(videoStream->codecpar->codec_id) };
    if (decoder == nullptr)
    {
        std::cerr << "Codec not found" << std::endl;
        WriteScannerLog("stream: decoder not found");
        return false;
    }
    pAVCodecContext = avcodec_alloc_context3(decoder);
    if (!pAVCodecContext)
    {
        WriteScannerLog("stream: codec context allocation failed");
        return false;
    }
    result = avcodec_parameters_to_context(pAVCodecContext, videoStream->codecpar);
    if (result < 0)
    {
        WriteScannerLog("stream: copy codec parameters error=" + std::to_string(result));
        return false;
    }
    result = avcodec_open2(pAVCodecContext, decoder, NULL);
    if (result < 0)
    {
        std::cerr << "Error opening codec" << std::endl;
        WriteScannerLog("stream: open codec error=" + std::to_string(result));
        return false;
    }
    pAVPacket = av_packet_alloc();
    pAVFrame = av_frame_alloc();
    if (!pAVPacket || !pAVFrame)
    {
        WriteScannerLog("stream: packet or frame allocation failed");
        return false;
    }
    WriteScannerLog("stream: decoder initialized codec=" + std::to_string(videoStream->codecpar->codec_id));
    readDeadline.refresh(av_gettime_relative(), 10000000);
    return true;
}

void QRCodeForStream::continueLastLogin()
{
    if (authorizationCancelled.load()) return;
    if (kuroAccount)
    {
        Q_EMIT loginResults(kuroSession.confirm({}, resultGeneration), resultGeneration);
        return;
    }
    const bool loggedIn = ScanPassportQRLogin(lastQrCode, gameToken, mid) && !authorizationCancelled.load() &&
                          ConfirmPassportQRLogin(lastQrCode, gameToken, mid);
    Q_EMIT loginResults(loggedIn ? ScanRet::SUCCESS : ScanRet::FAILURE_2, resultGeneration);
}

void QRCodeForStream::run()
{
    threadPool.setMaxThreadCount(threadNumber);
    m_stop.store(!m_cancelled.load());
    ret = ScanRet::UNKNOW;
    inputWidth = inputHeight = 0;
    inputFormat = AV_PIX_FMT_NONE;
    readyEmitted = false;
    submitClock.reset();
    pendingFrame.release();
    try
    {
        if (m_stop.load())
        {
            QRScanner modelCheck;
        }
        if (m_stop.load() && init())
        {
#ifdef ERYOU_SCAN_PREVIEW
            cv::namedWindow("Video_Stream", cv::WINDOW_AUTOSIZE);
#endif
            LoginOfficial();
        }
        else if (!m_cancelled.load())
        {
            ret = ScanRet::STREAMERROR;
        }
    }
    catch (...)
    {
        LogScannerException("stream: decoder");
        ret = ScanRet::STREAMERROR;
    }
    stop();
    threadPool.waitForDone();
    pendingFrame.release();
    if (ret == ScanRet::LIVESTOP || ret == ScanRet::STREAMERROR)
    {
        emit loginResults(ret, resultGeneration);
    }
#ifdef ERYOU_SCAN_PREVIEW
    try
    {
        cv::destroyWindow("Video_Stream");
    }
    catch (...)
    {
        LogScannerException("stream: preview cleanup");
    }
#endif
    avformat_close_input(&pAVFormatContext);
    avcodec_free_context(&pAVCodecContext);
    sws_freeContext(pSwsContext);
    av_dict_free(&pAvdictionary);
    av_frame_free(&pAVFrame);
    av_packet_free(&pAVPacket);
    pAVFormatContext = nullptr;
    pAVCodecContext = nullptr;
    pSwsContext = nullptr;
    pAvdictionary = nullptr;
    pAVFrame = nullptr;
    pAVPacket = nullptr;
}
