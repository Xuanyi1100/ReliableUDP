#include "FileTeleporter.h"
using namespace udpft;
FileTeleporter::FileTeleporter()
{
	rcMs = {};
	sender = false;
	state = CRACKED;
	fileSize = 0;
	crc = 0;
	totalChunks = 0;
	fileName = "default";
	chunkReceived.clear();
}
FileTeleporter::~FileTeleporter()
{
	Close();
}
void FileTeleporter::Close()
{
	if (inputFile.is_open())
	{
		inputFile.close();
	}
	if (outputFile.is_open())
	{
		outputFile.close();
	}
	ackOfChunks.clear();
	chunkReceived.clear();
}
// return 0 
int FileTeleporter::Initialize(const string& filePath, bool isSender)
{
	sender = isSender;
	if (sender)
	{
		// set file name
		fileName = filesystem::path(filePath).filename().string();
		if (fileName.length() > MaxFileNameLength - 1)
		{
			cerr << "Error: File name out of length limit: " << filePath << endl;
			return -1;
		}
		// open the file 
		inputFile.open(filePath, ios::binary | ios::ate);
		if (!inputFile)
		{
			cerr << "Error opening file for reading: " << filePath << endl;
			return -1;
		}

		fileSize = inputFile.tellg();
		totalChunks = (fileSize + 255) / 256;
		// Calculate CRC32 of the file
		inputFile.seekg(0, ios::beg);
		calculateFileCRC(inputFile,crc);
		state = WAVING;
	}
	else // receiver 
	{
		rcMs = {};
		fileSize = 0;
		crc = 0;
		totalChunks = 0;
		fileName = filePath;
		chunkReceived.clear();

		state = LISTENING;
	}
	return 0;
}

void FileTeleporter::LoadPacket(unsigned char packet[PacketSize])
{
	if (sender) 
	{
		switch (state) //
		{
		case WAVING:
			// MDID
			packMetaData(packet);
			break;
		case SENDING:	
			if (inputFile.eof())
			{
				// ENDID 
				packMessage(packet, ENDID, &crc, sizeof(crc));
			}
			else if (ackOfChunks[chunkIndex])
			{
				// FCID ,read nextchunk
				++chunkIndex;
				readChunk(packet);
			}
			else
			{
				// FCID				
				packMessage(packet, FCID, &fc, sizeof(fc));
			}
			break;
		case CRACKED:
		default:
			state = CRACKED;
			return;
		}
	}
	else
	{
		switch (state)
		{
		case READY:
			// OKID
			// OK for receving file chunks.
			packMessage(packet, OKID, &crc, sizeof(crc));
			break;
		case RECEIVING:
			// ACKID
			// ACK for a chunk
			packMessage(packet, ACKID, &chunkIndex, sizeof(chunkIndex));
			break;
		case DISCONNECTING:
			// DISID
			packMessage(packet, DISID, &crc, sizeof(crc));
			break;
		case CRACKED:
		default:
			state = CRACKED;
			return;
		}
	}
}
/*
* copy metadata to a Message with ID 0.
* Pack the Message into the packet.
*/
void FileTeleporter::packMetaData(unsigned char packet[PacketSize])
{
	// Prepare metadata
	FileMetadata metadata = {};
	strncpy(metadata.fileName, fileName.c_str(), MaxFileNameLength - 1);
	metadata.fileName[MaxFileNameLength - 1] = '\0';
	metadata.fileSize = fileSize;
	metadata.totalChunks = (fileSize + FileDataChunkSize - 1) / FileDataChunkSize;
	metadata.crc32 = crc;

	packMessage(packet, MDID, &metadata,sizeof(metadata));
}

void FileTeleporter::readChunk(unsigned char packet[PacketSize])
{
	if (!inputFile.is_open())
	{
		cerr << "Error file not open: " << fileName << endl;
		state = CRACKED;
		return;
	}	
	char buffer[FileDataChunkSize];
	inputFile.read(buffer, FileDataChunkSize);
	if (inputFile)
	{
		// handle the last file chunk.
		//size_t bytesRead = inputFile.gcount();
		//if (bytesRead > 0) {
		//	std::cout << " read last " << bytesRead << " bytes" << std::endl;
		//}
		// the last byte represents the number of valid bytes in the last chunk.
		memset(fc.data, 0, FileDataChunkSize);
		memcpy(fc.data, buffer, FileDataChunkSize);
		fc.chunkIndex = chunkIndex;
		packMessage(packet, FCID, &fc, sizeof(fc));
	}
	if (inputFile.eof())
	{
		cout << "Reached end of file after reading" << endl;
	}
	if (!inputFile.good())
	{
		cerr << "Error reading file Chunk: " << fileName << endl;
		state = CRACKED;
	}
}

string FileTeleporter::GetFileName() const
{
	return fileName;
}
State FileTeleporter::GetState() const
{
	return state; 
}


void FileTeleporter::ProcessPacket(unsigned char packet[PacketSize])
{
	memset(&rcMs, 0, sizeof(rcMs));
	memcpy(&rcMs, packet, sizeof(rcMs));
}
// call update after received a new message
void FileTeleporter::Update()
{
	if (state == CRACKED) return;
	/***************** File Receiver *****************/
	if (state = DISCONNECTING)
	{
		// back to ready after being in disconnecting state for 1s
		double duration = double(clock() - disconnectTime) / CLOCKS_PER_SEC;
		if (duration > 1)
		{
			Initialize("default", false);
		}
	}
	switch (rcMs.id)
	{
	case MDID: // parse metadata
		if (state == LISTENING)
		{
			storeMetadata();
			openFileForWriting();
			if (state = CRACKED) return;
			state = READY;
		}
		break;

	case FCID: // file chunk, write file data
		if (state == READY)
		{
			writeChunk();
			if (state == CRACKED) return;
			state = RECEIVING;
		}
		if (state == RECEIVING)
		{
			writeChunk();
			if (state == CRACKED) return;
		}
		break;

	case ENDID:
		if (state == RECEIVING && chunkReceived.back())
		{
			outputFile.close();
			// checke crc
			inputFile.open(fileName, ios::binary);
			if (!inputFile)
			{
				cerr << "Error opening file for computing CRC: " << fileName << endl;
				state = CRACKED;
				return;
			}
			uint32_t finalCRC = 0;
			calculateFileCRC(inputFile, finalCRC);
			inputFile.close();
			if (finalCRC != crc)
			{
				// not equal to CRC, be prepare for receiving the file from the head.
				chunkReceived.assign(totalChunks, false);
				openFileForWriting();
				if (state == CRACKED) return;
				state = READY;
			}
			else
			{
				state = DISCONNECTING;
				// record current time 
				disconnectTime = clock();
			}
		}
	/********************* File SENDER ***************/
	case OKID:
		if (state == WAVING)
		{
			// go back to the start of the file
			inputFile.seekg(0, ios::beg);
			ackOfChunks.assign(totalChunks, false);
			state = SENDING;
		}
		break;
	case ACKID:
		if (state == SENDING)
		{
			// read the index in the message.
			uint32_t ackedChunkIndex = 0;
			memcpy(&ackedChunkIndex, rcMs.content, sizeof(ackedChunkIndex));
			if (ackedChunkIndex == chunkIndex)
			{
				ackOfChunks[chunkIndex] = true;
			}
		}
		break;
	case DISID:
		if (state == SENDING && ackOfChunks.back())
		{
			Close();
		}
		break;
	default:
		break;
	}
}

void FileTeleporter::calculateFileCRC(ifstream& ifs, uint32_t& crc)
{
	char data[FileDataChunkSize] = { 0 };
	while (ifs.good())
	{
		ifs.read(data, FileDataChunkSize);
		crc = CRC::Calculate(data, FileDataChunkSize, CRC::CRC_32(), crc);
	}
}
void FileTeleporter::openFileForWriting()
{
	outputFile.open(fileName, std::ios::binary);
	if (outputFile.is_open())
	{
		std::cerr << "Error opening file for writing: " << fileName << std::endl;
		state = CRACKED;
	}
}
void FileTeleporter::packMessage(unsigned char packet[PacketSize],
	uint32_t id, const void* content, size_t size)
{
	Message ms = {};
	ms.id = id;
	memset(ms.content, 0, ContentSize);
	memcpy(ms.content, content, size);
	memset(packet, 0, PacketSize);
	memcpy(packet, &ms, sizeof(ms));
}
void FileTeleporter::storeMetadata()
{
	FileMetadata fm = {};
	// deserialize packet to FileMetadata
	memcpy(&fm, rcMs.content, sizeof(fm));

	// store metadata, open output file and start to receive the file.
	fileName = fm.fileName;
	fileSize = fm.fileSize;
	totalChunks = fm.totalChunks;
	crc = fm.crc32;
	chunkReceived.assign(totalChunks, false);
}
void FileTeleporter::writeChunk()
{
	// deserialize packet
	memset(&fc, 0, sizeof(fc));
	memcpy(&fc, rcMs.content, sizeof(fc));
	chunkIndex = fc.chunkIndex;
	// write to file
	// don't rewrite data having been already written
	if (!chunkReceived[chunkIndex] && outputFile.good())
	{
		outputFile.seekp(chunkIndex * FileDataChunkSize);
		outputFile.write((char*)fc.data, FileDataChunkSize);
		chunkReceived[chunkIndex] = true;
		// to sent an ack with chunkIndex.
	}
	if (!outputFile.good())
	{
		cerr << "Error writing file chunk: " << fileName << endl;
		state = CRACKED;
	}
}
void FileTeleporter::read()
{
	// Wait for all acks with timeout
	// ????
	// 
	//float ackWaitTime = 0.0f;
	//while (ackWaitTime < AckWaitTime &&
	//	connection.GetReliabilitySystem().GetAckedPackets() < metadata.totalChunks + 1) {
	//	connection.Update(DeltaTime);
	//	ackWaitTime += DeltaTime;
	//	net::wait(DeltaTime);
	//}

	// Calculate actual transfer time
	auto endTime = chrono::high_resolution_clock::now();
	float transferTime = chrono::duration<float>(endTime - startTime).count();

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
}