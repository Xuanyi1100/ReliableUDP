/*
	Reliability and Flow Control Example
	From "Networking for Game Programmers" - http://www.gaffer.org/networking-for-game-programmers
	Author: Glenn Fiedler <gaffer@gaffer.org>
*/
#define _CRT_SECURE_NO_WARNINGS  
#include <iostream>
#include <fstream>
#include <cstring> 
#include <vector>
#include <chrono>
#include <numeric>
#include "Net.h"
#include "CRC.h"


using namespace std;
using namespace net;

const int ServerPort = 30000;
const int ClientPort = 30001;
const int ProtocolId = 0x11223344;
const float DeltaTime = 1.0f / 30.0f;
const float SendRate = 1.0f / 30.0f;
const float TimeOut = 10.0f;
const int PacketSize = 256;
const int MaxFileNameLength = 128;
const int FileDataChunkSize = PacketSize - sizeof(uint32_t);
const float AckWaitTime = 2.0f;  // Time to wait for final acks

#pragma pack(push, 1)
struct FileMetadata {
	char fileName[MaxFileNameLength];
	uint32_t fileSize;
	uint32_t totalChunks;
	uint32_t crc32;
};

struct FileChunk {
	uint32_t chunkIndex;
	unsigned char data[FileDataChunkSize];
};
#pragma pack(pop)

class FlowControl
{
public:

	FlowControl()
	{
		printf("flow control initialized\n");
		Reset();
	}

	void Reset()
	{
		mode = Bad;
		penalty_time = 4.0f;
		good_conditions_time = 0.0f;
		penalty_reduction_accumulator = 0.0f;
	}

	void Update(float deltaTime, float rtt)
	{
		const float RTT_Threshold = 250.0f;

		if (mode == Good)
		{
			if (rtt > RTT_Threshold)
			{
				printf("*** dropping to bad mode ***\n");
				mode = Bad;
				if (good_conditions_time < 10.0f && penalty_time < 60.0f)
				{
					penalty_time *= 2.0f;
					if (penalty_time > 60.0f)
						penalty_time = 60.0f;
					printf("penalty time increased to %.1f\n", penalty_time);
				}
				good_conditions_time = 0.0f;
				penalty_reduction_accumulator = 0.0f;
				return;
			}

			good_conditions_time += deltaTime;
			penalty_reduction_accumulator += deltaTime;

			if (penalty_reduction_accumulator > 10.0f && penalty_time > 1.0f)
			{
				penalty_time /= 2.0f;
				if (penalty_time < 1.0f)
					penalty_time = 1.0f;
				printf("penalty time reduced to %.1f\n", penalty_time);
				penalty_reduction_accumulator = 0.0f;
			}
		}

		if (mode == Bad)
		{
			if (rtt <= RTT_Threshold)
				good_conditions_time += deltaTime;
			else
				good_conditions_time = 0.0f;

			if (good_conditions_time > penalty_time)
			{
				printf("*** upgrading to good mode ***\n");
				good_conditions_time = 0.0f;
				penalty_reduction_accumulator = 0.0f;
				mode = Good;
				return;
			}
		}
	}

	float GetSendRate()
	{
		return mode == Good ? 30.0f : 10.0f;
	}


private:

	enum Mode
	{
		Good,
		Bad
	};

	Mode mode;
	float penalty_time;
	float good_conditions_time;
	float penalty_reduction_accumulator;
};

// ----------------------------------------------

int main(int argc, char* argv[])
{


	enum Mode
	{
		Client,
		Server
	};

	Mode mode = Server;
	Address address;
	std::string filePath;

	if (argc >= 2)
	{
		int a, b, c, d;
		if (sscanf(argv[1], "%d.%d.%d.%d", &a, &b, &c, &d))
		{
			mode = Client;
			address = Address(a, b, c, d, ServerPort);
			if (argc >= 3) {
				filePath = argv[2];
			}
		}
	}

	// initialize

	if (!InitializeSockets())
	{
		printf("failed to initialize sockets\n");
		return 1;
	}

	ReliableConnection connection(ProtocolId, TimeOut);

	const int port = mode == Server ? ServerPort : ClientPort;

	if (!connection.Start(port))
	{
		printf("could not start connection on port %d\n", port);
		return 1;
	}

	if (mode == Client)
		connection.Connect(address);
	else
		connection.Listen();

	bool connected = false;
	float sendAccumulator = 0.0f;
	float statsAccumulator = 0.0f;

	FlowControl flowControl;

	while (true)
	{


		if (connection.IsConnected())
			flowControl.Update(DeltaTime, connection.GetReliabilitySystem().GetRoundTripTime() * 1000.0f);

		const float sendRate = flowControl.GetSendRate();

		// detect changes in connection state

		if (mode == Server && connected && !connection.IsConnected())
		{
			flowControl.Reset();
			printf("reset flow control\n");
			connected = false;
		}

		if (!connected && connection.IsConnected())
		{
			printf("client connected to server\n");
			connected = true;
		}

		if (!connected && connection.ConnectFailed())
		{
			printf("connection failed\n");
			break;
		}

		// send and receive packets

		if (mode == Client && !filePath.empty()) {
			static bool fileSent = false;
			if (!fileSent) {
				auto startTime = std::chrono::high_resolution_clock::now();

				// Read file contents
				std::ifstream ifs(filePath, std::ios::binary | std::ios::ate);
				if (!ifs.is_open()) {
					printf("Error opening file: %s\n", filePath.c_str());
					return 1;
				}

				uint32_t fileSize = ifs.tellg();
				ifs.seekg(0);
				std::vector<unsigned char> fileData(fileSize);
				ifs.read(reinterpret_cast<char*>(fileData.data()), fileSize);
				ifs.close();

				// Calculate CRC32
				uint32_t fileCRC = CRC::Calculate(fileData.data(), fileData.size(), CRC::CRC_32());

				// Prepare metadata
				FileMetadata metadata;
				const char* filename = strrchr(filePath.c_str(), '\\');
				filename = filename ? filename + 1 : filePath.c_str();
				strncpy(metadata.fileName, filename, MaxFileNameLength - 1);
				metadata.fileName[MaxFileNameLength - 1] = '\0';
				metadata.fileSize = fileSize;
				metadata.totalChunks = (fileSize + FileDataChunkSize - 1) / FileDataChunkSize;
				metadata.crc32 = fileCRC;

				// Send metadata
				connection.SendPacket(reinterpret_cast<unsigned char*>(&metadata), sizeof(metadata));

				// Splits file into chunks and sends them
				for (uint32_t i = 0; i < metadata.totalChunks; i++) {
					FileChunk chunk;
					chunk.chunkIndex = i;
					size_t offset = i * FileDataChunkSize;
					size_t chunkSize = (((FileDataChunkSize) < (fileSize - offset)) ? (FileDataChunkSize) : (fileSize - offset))
						;

					// Copy from memory buffer instead of re-reading file
					memcpy(chunk.data, fileData.data() + offset, chunkSize);

					// Fill remaining space with zeros if needed
					if (chunkSize < FileDataChunkSize) {
						memset(chunk.data + chunkSize, 0, FileDataChunkSize - chunkSize);
					}

					connection.SendPacket(reinterpret_cast<unsigned char*>(&chunk), sizeof(chunk));
				}

				// Wait for all acks with timeout
				float ackWaitTime = 0.0f;
				while (ackWaitTime < AckWaitTime &&
					connection.GetReliabilitySystem().GetAckedPackets() < metadata.totalChunks + 1) {
					connection.Update(DeltaTime);
					ackWaitTime += DeltaTime;
					net::wait(DeltaTime);
				}

				// Calculate actual transfer time
				auto endTime = std::chrono::high_resolution_clock::now();
				float transferTime = std::chrono::duration<float>(endTime - startTime).count();

				// Calculate speed in Mbps (1 megabit = 1,000,000 bits)
				float fileSizeBits = fileSize * 8.0f;
				float speedMbps = (fileSizeBits / transferTime) / 1000000.0f;

				printf("Transfer complete!\n");
				printf("File size: %.2f KB\n", fileSize / 1024.0f);
				printf("Transfer time: %.2fs\n", transferTime);
				printf("Effective speed: %.2f Mbps\n", speedMbps);

				printf("Calculated File CRC: 0x%08X\n", fileCRC);

				fileSent = true;
			}
		}

		static std::ofstream outputFile;
		static std::vector<unsigned char> receivedFile;
		static FileMetadata receivedMetadata;
		static uint32_t receivedChunks = 0;

		while (true) {
			unsigned char packet[PacketSize];
			int bytes_read = connection.ReceivePacket(packet, sizeof(packet));
			if (bytes_read == 0) break;

			if (bytes_read == sizeof(FileMetadata)) {
				// Received metadata
				memcpy(&receivedMetadata, packet, sizeof(receivedMetadata));
				receivedFile.resize(receivedMetadata.fileSize);
				printf("Receiving file: %s (%u bytes)\n",
					receivedMetadata.fileName, receivedMetadata.fileSize);
				printf("Expecting %u bytes in %u chunks\n",
					receivedMetadata.fileSize, receivedMetadata.totalChunks);
				receivedFile.reserve(receivedMetadata.fileSize); // Pre-allocate exact size
			}
			else if (bytes_read == sizeof(FileChunk)) {
				// Received file chunk
				FileChunk chunk;
				memcpy(&chunk, packet, sizeof(FileChunk));

				// Ensure vector is properly sized before writing
				size_t offset = chunk.chunkIndex * FileDataChunkSize;
				size_t remaining = receivedMetadata.fileSize - offset;
				size_t copySize = (((FileDataChunkSize) < (remaining)) ? (FileDataChunkSize) : (remaining))
					;

				if (offset + copySize > receivedFile.size()) {
					receivedFile.resize(offset + copySize);
				}

				memcpy(receivedFile.data() + offset, chunk.data, copySize);
				receivedChunks++;

				// Check if transfer complete
				if (receivedChunks == receivedMetadata.totalChunks) {
					// Verify CRC
					printf("Finalizing transfer with %u/%u chunks received\n",
						receivedChunks, receivedMetadata.totalChunks);
					printf("Received file size: %zu bytes\n", receivedFile.size());
					printf("Original CRC claim: 0x%08X\n", receivedMetadata.crc32);

					uint32_t calculatedCRC = CRC::Calculate(receivedFile.data(),
						receivedFile.size(), CRC::CRC_32());
					printf("Server Calculated CRC: 0x%08X\n", calculatedCRC);

					if (calculatedCRC == receivedMetadata.crc32) {
						std::ofstream outFile(receivedMetadata.fileName, std::ios::binary);
						outFile.write(reinterpret_cast<char*>(receivedFile.data()),
							receivedFile.size());
						printf("File received and verified successfully!\n");
					}
					else {
						printf("ERROR: File verification failed!\n");
					}
				}

				printf("Received chunk %u/%u (%zu bytes)\n",
					chunk.chunkIndex + 1, receivedMetadata.totalChunks, copySize);
			}
		}


		// show packets that were acked this frame	
#ifdef SHOW_ACKS
		unsigned int* acks = NULL;
		int ack_count = 0;
		connection.GetReliabilitySystem().GetAcks(&acks, ack_count);
		if (ack_count > 0)
		{
			printf("acks: %d", acks[0]);
			for (int i = 1; i < ack_count; ++i)
				printf(",%d", acks[i]);
			printf("\n");
		}
#endif

		// update connection

		connection.Update(DeltaTime);

		// show connection stats

		statsAccumulator += DeltaTime;

		while (statsAccumulator >= 0.25f && connection.IsConnected())
		{
			float rtt = connection.GetReliabilitySystem().GetRoundTripTime();

			unsigned int sent_packets = connection.GetReliabilitySystem().GetSentPackets();
			unsigned int acked_packets = connection.GetReliabilitySystem().GetAckedPackets();
			unsigned int lost_packets = connection.GetReliabilitySystem().GetLostPackets();

			float sent_bandwidth = connection.GetReliabilitySystem().GetSentBandwidth();
			float acked_bandwidth = connection.GetReliabilitySystem().GetAckedBandwidth();

			printf("rtt %.1fms, sent %d, acked %d, lost %d (%.1f%%), sent bandwidth = %.1fkbps, acked bandwidth = %.1fkbps\n",
				rtt * 1000.0f, sent_packets, acked_packets, lost_packets,
				sent_packets > 0.0f ? (float)lost_packets / (float)sent_packets * 100.0f : 0.0f,
				sent_bandwidth, acked_bandwidth);

			statsAccumulator -= 0.25f;
		}

		net::wait(DeltaTime);
	}

	ShutdownSockets();

	return 0;
}
