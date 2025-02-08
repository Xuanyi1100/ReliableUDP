#include "pch.h"
#include "FileTeleporter.h"
#include <fstream>

using namespace udpft;

TEST(FileTeleporterTest, ConstructorTest) {
    FileTeleporter ft;
    EXPECT_EQ(ft.GetState(), CRACKED);
    EXPECT_EQ(ft.GetFileSize(), 0);
}

TEST(FileTeleporterTest, InitializeSender) {

    FileTeleporter ft;
    EXPECT_TRUE(ft.Initialize("E:\\GitRepo\\ReliableUDP\\x64\\Debug\\test.md", true)); 
    EXPECT_EQ(ft.GetState(), WAVING);
    EXPECT_EQ(ft.GetFileName(), "test.md");
    EXPECT_GT(ft.GetFileSize(), 0);
}

TEST(FileTeleporterTest, InitializeReceiver) {
    FileTeleporter ft;
    EXPECT_TRUE(ft.Initialize("received.txt", false)); 
    EXPECT_EQ(ft.GetState(), LISTENING);
}

TEST(FileTeleporterTest, LoadPacketTest) {
    FileTeleporter ft;
    ft.Initialize("E:\\GitRepo\\ReliableUDP\\x64\\Debug\\test.md", true);
    unsigned char packet[PacketSize] = { 0 };
    ft.LoadPacket(packet);
    EXPECT_NE(packet[0], 0);  
}


int main(int argc, char** argv) {
    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}