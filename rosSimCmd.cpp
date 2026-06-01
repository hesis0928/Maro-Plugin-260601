// 1. 매크로 충돌 방지 (무조건 최상단)
#define NOMINMAX
#define _HAS_STD_BYTE 0

// ==========================================================
// 2. [★핵심] 외부 라이브러리 및 C++ 표준 헤더를 마야보다 "먼저" 로드합니다.
// ==========================================================
#include <memory>
#include <string>
#include <stdexcept>
#include <cstdint>

// Boost Interprocess
#include <boost/interprocess/windows_shared_memory.hpp>
#include <boost/interprocess/mapped_region.hpp>
#include <boost/interprocess/sync/named_mutex.hpp>
#include <boost/interprocess/sync/scoped_lock.hpp>

// ==========================================================
// 3. Maya API 헤더는 그 이후에 로드합니다.
// ==========================================================
#include <maya/MPxCommand.h>
#include <maya/MFnPlugin.h>
#include <maya/MGlobal.h>
#include <maya/MTimerMessage.h>
#include <maya/M3dView.h>
#include <maya/MImage.h>
#include <maya/MArgParser.h>
#include <maya/MSyntax.h>

// Track C (제어) 용 마야 API 추가
#include <maya/MFnTransform.h>
#include <maya/MDagPath.h>
#include <maya/MSelectionList.h>
#include <maya/MVector.h>
#include <maya/MQuaternion.h>

// 통신 규약 헤더 포함 (트리 구조에 맞게 경로 지정)
#include "src/Maro_library/include/maro_library/IpcData.h"

// Maro 지능형 트러블슈팅 시스템 적용
#include "Maro_DebugUtility/boad_Maro.h"
#include "Maro_DebugUtility/ghost_Maro.h"

namespace RosMayaPlugin {

	class ViewportStreamer {
	public:
		ViewportStreamer()
			: mutex(boost::interprocess::open_or_create, MaroPlugin::MUTEX_NAME),
			ctrl_mutex(boost::interprocess::open_or_create, MaroPlugin::CTRL_MUTEX_NAME),
			m_lastCommandIndex(0)
		{
			// 기존에 남아있을 수 있는 공유 메모리 제거
			boost::interprocess::shared_memory_object::remove(MaroPlugin::SHM_NAME);
			boost::interprocess::shared_memory_object::remove(MaroPlugin::CTRL_SHM_NAME);
			BoadMaro::devInfo("ViewportStreamer instance created. Initial shared memory remnants cleaned.");
		}

		~ViewportStreamer() { stop(); }

		void start(double rate) {
			if (isRunning()) return;

			BoadMaro::devInfo("Start requested. Rate: " + MString() + (1.0 / rate) + " fps");
			BoadMaro::devInfo(BoadMaro::dumpState(this));

			try {
				// 1. 뷰포트 메모리 세그먼트 생성 (Track B)
				shm_segment = boost::interprocess::windows_shared_memory(
					boost::interprocess::create_only,
					MaroPlugin::SHM_NAME,
					boost::interprocess::read_write,
					MaroPlugin::SHM_SIZE
				);
				shm_region = boost::interprocess::mapped_region(shm_segment, boost::interprocess::read_write);
				std::memset(shm_region.get_address(), 0, shm_region.get_size());

				// 2. 제어 메모리 세그먼트 생성 (Track C)
				ctrl_shm = boost::interprocess::windows_shared_memory(
					boost::interprocess::create_only,
					MaroPlugin::CTRL_SHM_NAME,
					boost::interprocess::read_write,
					MaroPlugin::CTRL_SHM_SIZE
				);
				ctrl_region = boost::interprocess::mapped_region(ctrl_shm, boost::interprocess::read_write);
				std::memset(ctrl_region.get_address(), 0, ctrl_region.get_size());

				// 타이머 콜백 등록 (주기적으로 뷰포트 캡처)
				timerCallbackId = MTimerMessage::addTimerCallback(
					static_cast<float>(rate),
					&ViewportStreamer::timerCallbackStatic,
					this
				);

				MARO_ASSERT(timerCallbackId != 0, "Timer callback creation failed!");

				BoadMaro::info("Viewport Streamer started.");
				BoadMaro::devInfo(BoadMaro::dumpState(this));
			}
			catch (const boost::interprocess::interprocess_exception& e) {
				MString errorMsg = "Failed to create shared memory: ";
				errorMsg += e.what();
				errorMsg += "\n[Suggestion] Check if another process (e.g., a zombie shm_reader) is using the same shared memory, or check system permissions.";
				BoadMaro::error("IPC_CREATE_FAILURE", errorMsg);
				stop();
			}
		}

		void stop() {
			if (timerCallbackId != 0) {
				MMessage::removeCallback(timerCallbackId);
				timerCallbackId = 0;
			}
			// 공유 메모리 객체 제거
			boost::interprocess::shared_memory_object::remove(MaroPlugin::SHM_NAME);
			boost::interprocess::named_mutex::remove(MaroPlugin::MUTEX_NAME);
			boost::interprocess::shared_memory_object::remove(MaroPlugin::CTRL_SHM_NAME);
			boost::interprocess::named_mutex::remove(MaroPlugin::CTRL_MUTEX_NAME);
			BoadMaro::info("Viewport Streamer stopped.");
			BoadMaro::devInfo(BoadMaro::dumpState(this));
		}

		bool isRunning() const { return timerCallbackId != 0; }
		MCallbackId getTimerCallbackId() const { return timerCallbackId; }

	private:
		void onTimer(float, float) {
			try {
				// ==========================================================
				// [Track C] 1. ROS2 제어 명령 수신 및 로봇 구동
				// ==========================================================
				bool isRobotMoved = false;
				MaroPlugin::RobotControlData cmd;

				{
					// 제어 메모리 락 획득 후 빠르게 데이터만 복사
					boost::interprocess::scoped_lock<boost::interprocess::named_mutex> lock(ctrl_mutex);
					auto* pCtrl = static_cast<MaroPlugin::RobotControlData*>(ctrl_region.get_address());

					uint64_t currentIndex = pCtrl->command_index.load(std::memory_order_relaxed);
					if (currentIndex > m_lastCommandIndex) {
						cmd = *pCtrl; // 구조체 값 복사
						m_lastCommandIndex = currentIndex;
						isRobotMoved = true;
					}
				}

				// 수신된 제어 명령이 있다면 Maya 내부 모델 이동
				if (isRobotMoved && cmd.has_transform) {
					MSelectionList list;
					list.add("Robot_Root_Controller"); // ★ 실제 마야 씬 내의 로봇 컨트롤러 이름으로 매칭 필요
					MDagPath nodePath;

					if (list.getDagPath(0, nodePath) == MS::kSuccess) {
						MFnTransform transFn(nodePath);
						transFn.setTranslation(MVector(cmd.position[0], cmd.position[1], cmd.position[2]), MSpace::kWorld);
						transFn.setRotation(MQuaternion(cmd.orientation[0], cmd.orientation[1], cmd.orientation[2], cmd.orientation[3]), MSpace::kWorld);
					}
					else {
						BoadMaro::warn("Track C: 'Robot_Root_Controller' not found in the scene.");
					}
				}

				// ==========================================================
				// [Track B] 2. 뷰포트 캡처 및 ROS2로 전송
				// ==========================================================
				M3dView activeView = M3dView::active3dView();

				// 로봇이 움직였다면 뷰포트를 강제로 한 번 갱신하여 렌더링 반영
				if (isRobotMoved) {
					activeView.refresh(false, true);
				}

				unsigned int view_w = activeView.portWidth();
				unsigned int view_h = activeView.portHeight();

				BoadMaro::devInfo("onTimer tick. Viewport size: " + MString() + view_w + "x" + view_h);

				// 뷰포트 크기가 0이거나 너무 크면 스킵
				if (view_w == 0 || view_h == 0 || (view_w * view_h * 4) > MaroPlugin::MAX_RESOLUTION_BYTES) {
					BoadMaro::devInfo("Skipping frame: invalid viewport dimensions.");
					return;
				}

				MImage image;
				if (activeView.readColorBuffer(image, true)) {
					image.convertPixelFormat(MImage::kByte); // 픽셀 포맷을 바이트로 변환 (RGBA는 기본값)
					unsigned char* pixels = image.pixels();
					unsigned int w, h;
					image.getSize(w, h);

					MARO_ASSERT(pixels != nullptr, "MImage::pixels() returned null.");

					// 뮤텍스로 데이터 동시 접근 방지
					boost::interprocess::scoped_lock<boost::interprocess::named_mutex> lock(mutex);

					auto* header = static_cast<MaroPlugin::SharedImageHeader*>(shm_region.get_address());
					header->width = w;
					header->height = h;
					header->channels = 4; // RGBA

					// 헤더 바로 뒤에 픽셀 데이터 복사
					unsigned char* buffer = static_cast<unsigned char*>(shm_region.get_address()) + sizeof(MaroPlugin::SharedImageHeader);
					std::memcpy(buffer, pixels, w * h * 4);

					// 데이터 기록 후 프레임 인덱스 증가 (atomic)
					header->frame_index.fetch_add(1, std::memory_order_relaxed);
					BoadMaro::devInfo("Frame " + MString() + (int)header->frame_index.load() + " written to shared memory.");
				}
			}
			catch (const MStatus& ms) {
				BoadMaro::warn(MString("Error reading viewport buffer: ") + ms.errorString());
			}
			catch (const boost::interprocess::interprocess_exception& e) {
				MString errorMsg = "Shared memory access error during timer callback: ";
				errorMsg += e.what();
				errorMsg += "\n[Suggestion] This may happen if the shared memory was unexpectedly removed. The streamer will now stop.";
				BoadMaro::error("IPC_ACCESS_FAILURE", errorMsg);
				stop();
			}
		}

		static void timerCallbackStatic(float elapsedTime, float lastTime, void* clientData) {
			MARO_ASSERT(clientData != nullptr, "clientData is null in timer callback!");
			static_cast<ViewportStreamer*>(clientData)->onTimer(elapsedTime, lastTime);
		}

		MCallbackId timerCallbackId = 0;

		// Track B: 뷰포트 전송용 메모리
		boost::interprocess::windows_shared_memory shm_segment;
		boost::interprocess::mapped_region shm_region;
		boost::interprocess::named_mutex mutex;

		// Track C: 제어 수신용 메모리
		boost::interprocess::windows_shared_memory ctrl_shm;
		boost::interprocess::mapped_region ctrl_region;
		boost::interprocess::named_mutex ctrl_mutex;
		uint64_t m_lastCommandIndex;
	};

	static std::unique_ptr<ViewportStreamer> g_streamer = nullptr;

	class rosSimCmd : public MPxCommand {
	public:
		static const char* kRateFlag;
		static const char* kRateFlagLong;
		static const char* kTestShutdownFlag;
		static const char* kTestShutdownFlagLong;

		virtual MStatus doIt(const MArgList& args) override {
			MStatus status;
			MArgParser parser(syntax(), args, &status);
			if (!status) return status;

			// GhostMaro 셧다운/부활 시나리오 테스트
			if (parser.isFlagSet(kTestShutdownFlag)) {
				BoadMaro::warn("--- [DEV] SIMULATING SYSTEM SHUTDOWN SCENARIO ---");
				GhostMaro::InitiateEmergencyFragmentSave();
				BoadMaro::warn("...System Crashed...");
				BoadMaro::warn("--- [DEV] SIMULATING REBOOT AND FRAGMENT ASSEMBLY ---");
				GhostMaro::AssembleFragmentsOnReboot();
				return MS::kSuccess;
			}

			if (!g_streamer) {
				g_streamer = std::make_unique<ViewportStreamer>();
			}

			MARO_ASSERT(g_streamer != nullptr, "Failed to create g_streamer instance.");

			if (g_streamer->isRunning()) {
				g_streamer->stop();
				return MS::kSuccess;
			}

			double rate = 1.0 / 30.0; // 기본값: 30fps
			if (parser.isFlagSet(kRateFlag)) {
				parser.getFlagArgument(kRateFlag, 0, rate);
			}

			g_streamer->start(rate);
			return MS::kSuccess;
		}

		static MSyntax newSyntax() {
			MSyntax syntax;
			syntax.addFlag(kRateFlag, kRateFlagLong, MSyntax::kDouble);
			syntax.addFlag(kTestShutdownFlag, kTestShutdownFlagLong);
			return syntax;
		}

		static void* creator() { return new rosSimCmd; }
	};
	const char* rosSimCmd::kRateFlag = "-r";
	const char* rosSimCmd::kRateFlagLong = "-rate";
	const char* rosSimCmd::kTestShutdownFlag = "-tst";
	const char* rosSimCmd::kTestShutdownFlagLong = "-testShutdown";
}

MStatus initializePlugin(MObject obj) {
	MFnPlugin plugin(obj, "ToolDeveloper", "1.0", "Any");
	return plugin.registerCommand("rosSim", RosMayaPlugin::rosSimCmd::creator, RosMayaPlugin::rosSimCmd::newSyntax);
}

MStatus uninitializePlugin(MObject obj) {
	MFnPlugin plugin(obj);
	if (RosMayaPlugin::g_streamer && RosMayaPlugin::g_streamer->isRunning()) {
		RosMayaPlugin::g_streamer->stop();
	}
	RosMayaPlugin::g_streamer = nullptr;
	return plugin.deregisterCommand("rosSim");
}