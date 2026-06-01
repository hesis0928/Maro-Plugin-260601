#include <gtest/gtest.h>
#include <cstdint>
#include <vector>
#include <boost/interprocess/windows_shared_memory.hpp>
#include <boost/interprocess/mapped_region.hpp>
#include <boost/interprocess/sync/named_mutex.hpp>
#include <boost/interprocess/sync/scoped_lock.hpp>
#include "src/common/IpcData.h"

class IpcPipeTest : public ::testing::Test {
protected:
    void SetUp() override {
        boost::interprocess::shared_memory_object::remove(MaroPlugin::SHM_NAME);
        boost::interprocess::named_mutex::remove(MaroPlugin::MUTEX_NAME);
    }
    void TearDown() override {
        boost::interprocess::shared_memory_object::remove(MaroPlugin::SHM_NAME);
        boost::interprocess::named_mutex::remove(MaroPlugin::MUTEX_NAME);
    }
};

TEST_F(IpcPipeTest, WriterWritesDataAndReaderReadsSameData) {
    // === 1. Writer 역할 ===
    {
        boost::interprocess::named_mutex mutex(boost::interprocess::create_only, MaroPlugin::MUTEX_NAME);
        boost::interprocess::windows_shared_memory shm(
            boost::interprocess::create_only, MaroPlugin::SHM_NAME,
            boost::interprocess::read_write, MaroPlugin::SHM_SIZE);
        boost::interprocess::mapped_region region(shm, boost::interprocess::read_write);
        boost::interprocess::scoped_lock<boost::interprocess::named_mutex> lock(mutex);

        MaroPlugin::SharedImageHeader* header = static_cast<MaroPlugin::SharedImageHeader*>(region.get_address());
        header->width = 1280;
        header->height = 720;
        header->channels = 4;
        header->frame_index.store(99); // atomic에 값 쓰기
    }
    // === 2. Reader 역할 ===
    uint32_t read_width, read_height, read_channels;
    uint64_t read_frame_index;
    {
        boost::interprocess::named_mutex mutex(boost::interprocess::open_only, MaroPlugin::MUTEX_NAME);
        boost::interprocess::windows_shared_memory shm(
            boost::interprocess::open_only, MaroPlugin::SHM_NAME,
            boost::interprocess::read_only);
        boost::interprocess::mapped_region region(shm, boost::interprocess::read_only);
        boost::interprocess::scoped_lock<boost::interprocess::named_mutex> lock(mutex);

        const MaroPlugin::SharedImageHeader* header_ptr = static_cast<const MaroPlugin::SharedImageHeader*>(region.get_address());
        
        // [수정] 구조체 전체 복사 대신 멤버별로 값을 읽어옵니다.
        read_width = header_ptr->width;
        read_height = header_ptr->height;
        read_channels = header_ptr->channels;
        read_frame_index = header_ptr->frame_index.load(); // atomic에서 값 읽기
    }
    // === 3. 검증 ===
    ASSERT_EQ(read_width, 1280);
    ASSERT_EQ(read_height, 720);
    ASSERT_EQ(read_channels, 4);
    ASSERT_EQ(read_frame_index, 99);
}