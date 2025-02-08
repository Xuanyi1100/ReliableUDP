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

#include "FileTeleporter.h"

using namespace std;
using namespace net;
using namespace udpft;

const int ServerPort = 30000;
const int ClientPort = 30001;
const int ProtocolId = 0x11223344;
const float DeltaTime = 1.0f / 30.0f;
const float SendRate = 1.0f / 30.0f;
const float TimeOut = 10.0f;

const float AckWaitTime = 2.0f;  // Time to wait for final acks

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
	// parse command line
	if (argc >= 2)
	{
		int a, b, c, d;
		if (sscanf(argv[1], "%d.%d.%d.%d", &a, &b, &c, &d))
		{
			mode = Client;
			address = Address(a, b, c, d, ServerPort);
			// fetch file path if provided
			if (argc >= 3) {
				filePath = argv[2];

				// TODO: Validate the filepath
				if (!filesystem::exists(filePath) && std::filesystem::is_regular_file(filePath))
				{
					printf("Specified file doesn't exist.\n");
					return 1;
				}
			}
		}
		else
		{
			printf("client mode usage:\n"
			"%s <ip> [file] \n", argv[0]);
			return 1;
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
	FileTeleporter ftp;

	bool isSender = (mode == Client);
	if (!ftp.Initialize(filePath, isSender))
	{
		return 1;
	}
	auto startTime = chrono::high_resolution_clock::now();

	unsigned char test[PacketSize] = {0};
	ftp.LoadPacket(test);

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

		sendAccumulator += DeltaTime;
		//

		if (mode == Client && !filePath.empty()) {
			static bool fileSent = false;

			// TODO: instance a file transmitter

			if (!fileSent) {
				auto startTime = std::chrono::high_resolution_clock::now();
			}
		}

		// send packets at a fixed rate
		while (sendAccumulator > 1.0f / sendRate)
		{
			unsigned char packet[PacketSize];
			ftp.LoadPacket(packet);
			connection.SendPacket(packet, sizeof(packet));
			sendAccumulator -= 1.0f / sendRate;
		}

		while (true) // receiving a packet
		{
			// in the server mode, receive packets of the file 
			// invoke methods in the filetransmitter to save the file back to listening state after having verified the file
			unsigned char packet[PacketSize];
			int bytes_read = connection.ReceivePacket(packet, sizeof(packet));
			if (bytes_read == 0)
				break;
			ftp.ProcessPacket(packet);	
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
		// process received message.
		// update the file transfer

		ftp.Update();	
		if (ftp.GetState() == CRACKED)
		{
			printf("File tramsmitter cracked\n");
			printf("%s\n", ftp.GetFileName().c_str());
			printf("file size: %u bytes\n", ftp.GetFileSize());
			printf("Original CRC claim: 0x%08X\n", ftp.GetFileCRC());
			return 1;
		}
		if (ftp.GetState() == CLOSED)
		{
			// Calculate actual transfer time
			auto endTime = chrono::high_resolution_clock::now();
			float transferTime = chrono::duration<float>(endTime - startTime).count();
			uint32_t fileSize = ftp.GetFileSize();

			// Calculate speed in Mbps (1 megabit = 1,000,000 bits)
			float fileSizeBits = fileSize * 8.0f;
			float speedMbps = (fileSizeBits / transferTime) / 1000000.0f;

			printf("Transfer complete!\n");
			printf("File size: %.2f KB\n", fileSize / 1024.0f);
			printf("Transfer time: %.2fs\n", transferTime);
			printf("Effective speed: %.2f Mbps\n", speedMbps);
			break;
		}
		net::wait(DeltaTime);
	}
	ftp.Close();
	ShutdownSockets();
	return 0;
}
