#define NOMINMAX

#include "ViewportStreamer.h"
#include "Maro_DebugUtility/boad_Maro.h"
#include <maya/M3dView.h>
#include <maya/MGlobal.h>
#include <maya/MImage.h>
#include <maya/MTimerMessage.h>
#include <boost/interprocess/sync/scoped_lock.hpp>

namespace MaroPlugin {

    ViewportStreamer::ViewportStreamer()
        : m_isRunning(false), m_callbackId(0), m_pHeader(nullptr) {

        BoadMaro::devInfo("ViewportStreamer instance created.");

        // [Track C] 제어용 공유 메모리 및 뮤텍스 사전 초기화
        try {
            m_ctrl_mutex = std::make_unique<boost::interprocess::named_mutex>(
                boost::interprocess::open_or_create, "MayaControlMutex");
            m_ctrl_shm = std::make_unique<boost::interprocess::windows_shared_memory>(
                boost::interprocess::open_or_create, "MayaControlSHM", boost::interprocess::read_write, sizeof(RobotControlData));
            m_ctrl_region = std::make_unique<boost::interprocess::mapped_region>(
                *m_ctrl_shm, boost::interprocess::read_write);
        }
        catch (const boost::interprocess::interprocess_exception& e) {
            BoadMaro::error("CTRL_SHM_INIT", std::string("Failed to init control SHM: ") + e.what());
        }
    }

    ViewportStreamer::~ViewportStreamer() {
        stop();
    }

    MStatus ViewportStreamer::start(double period) {
        if (m_isRunning.load()) {
            BoadMaro::warn("Streamer is already running.");
            return MS::kSuccess;
        }

        try {
            // [Track B] 영상 스트리밍용 메모리 연결 (기존 로직)
            m_mutex = std::make_unique<boost::interprocess::named_mutex>(boost::interprocess::open_only, MUTEX_NAME);
            m_shm = std::make_unique<boost::interprocess::windows_shared_memory>(boost::interprocess::open_only, SHM_NAME, boost::interprocess::read_write);
            m_region = std::make_unique<boost::interprocess::mapped_region>(*m_shm, boost::interprocess::read_write);
            m_pHeader = reinterpret_cast<SharedImageHeader*>(m_region->get_address());

            // 마야 타이머 콜백 등록
            m_callbackId = MTimerMessage::addTimerCallback(period, timerCallback, this);
            m_isRunning.store(true);
            BoadMaro::devInfo("Streamer started successfully.");
            return MS::kSuccess;
        }
        catch (...) {
            BoadMaro::error("STREAM_START_FAIL", "Failed to start streamer. Check bridge status.");
            return MS::kFailure;
        }
    }

    void ViewportStreamer::stop() {
        if (m_callbackId != 0) {
            MTimerMessage::removeCallback(m_callbackId);
            m_callbackId = 0;
        }

        // 영상 스트리밍 자원 해제
        m_region.reset();
        m_shm.reset();
        m_mutex.reset();
        m_pHeader = nullptr;
        m_isRunning.store(false);
    }
    
    void ViewportStreamer::timerCallback(float elapsedTime, float lastTime, void* clientData) {
        if (clientData) {
            static_cast<ViewportStreamer*>(clientData)->streamUpdate();
        }
    }
}