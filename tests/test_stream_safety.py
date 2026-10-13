"""Source guards for official live-stream scanning (no Qt SDK required)."""

import pathlib
import re
import unittest


ROOT = pathlib.Path(__file__).resolve().parents[1]
SOURCE = (ROOT / "src/UI/QRCodeForStream.cpp").read_text(encoding="utf-8-sig")


class StreamSafetyTests(unittest.TestCase):
    def test_official_path_skips_invalid_video_packets(self):
        branches = SOURCE.split("if (sendResult == AVERROR_INVALIDDATA)")[1:]
        self.assertEqual(len(branches), 1)
        for branch in branches:
            recover = branch.split("continue;", 1)[0]
            self.assertIn("av_packet_unref(pAVPacket)", recover)
            self.assertNotIn("ret = ScanRet::STREAMERROR", recover)
            self.assertNotIn("stop()", recover)

    def test_official_path_skips_invalid_decoded_frames(self):
        self.assertEqual(SOURCE.count("receiveResult != AVERROR_INVALIDDATA"), 1)
        self.assertEqual(SOURCE.count("stream: skipping invalid decoded frame"), 1)
        branches = SOURCE.split("if (receiveResult == AVERROR_INVALIDDATA)")[1:]
        self.assertEqual(len(branches), 1)
        for branch in branches:
            recovery = branch.split("cv::Mat img", 1)[0]
            self.assertIn("continue;", recovery)
            self.assertIn("readDeadline.expired", recovery)

    def test_only_valid_frames_refresh_read_deadline(self):
        loops = SOURCE.split("void QRCodeForStream::LoginOfficial()", 1)[1].split(
            "int QRCodeForStream::interruptStream", 1
        )[0]
        self.assertNotIn("readDeadline.refresh", loops)

    def test_scaler_is_not_created_before_first_decoded_frame(self):
        init = SOURCE.split("auto QRCodeForStream::init()", 1)[1].split(
            "void QRCodeForStream::continueLastLogin", 1
        )[0]
        self.assertNotIn("sws_getContext", init)
        self.assertNotIn("sws_getCachedContext", init)

    def test_official_path_uses_checked_frame_conversion(self):
        self.assertEqual(SOURCE.count("if (!convertFrame(img))"), 1)

    def test_no_single_element_scaler_arrays(self):
        self.assertIsNone(re.search(r"dst(?:Data|Linesize)\s*\[1\]", SOURCE))

    def test_audio_packets_are_released(self):
        paths = SOURCE.split("if (pAVPacket->stream_index != videoStreamIndex)")[1:]
        self.assertEqual(len(paths), 1)
        for path in paths:
            self.assertIn("av_packet_unref(pAVPacket)", path.split("continue;", 1)[0])

    def test_workers_finish_before_resources_are_freed(self):
        run = SOURCE.split("void QRCodeForStream::run()", 1)[1]
        self.assertIn("threadPool.waitForDone()", run)
        self.assertLess(run.index("threadPool.waitForDone()"), run.index("avformat_close_input"))

    def test_interrupt_callback_is_installed_before_open(self):
        init = SOURCE.split("auto QRCodeForStream::init()", 1)[1]
        self.assertIn("interrupt_callback", init)
        self.assertLess(init.index("interrupt_callback"), init.index("avformat_open_input"))

    def test_official_path_submits_latest_frame(self):
        self.assertEqual(SOURCE.count("submitFrame(std::move(img))"), 1)
        self.assertIn("pendingFrame = std::move(img)", SOURCE)

    def test_ready_is_emitted_only_after_checked_conversion(self):
        convert = SOURCE.split("bool QRCodeForStream::convertFrame", 1)[1].split(
            "void QRCodeForStream::stop", 1
        )[0]
        self.assertIn("Q_EMIT streamReady()", convert)
        self.assertLess(convert.index("result != videoStreamHeight"), convert.index("Q_EMIT streamReady()"))

    def test_stop_before_worker_start_is_not_lost(self):
        run = SOURCE.split("void QRCodeForStream::run()", 1)[1]
        self.assertNotIn("m_stop.store(true)", run)
        self.assertIn("m_cancelled.load()", run)


if __name__ == "__main__":
    unittest.main()
