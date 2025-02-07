#include "FileTransmitter.h"
using namespace udpft;
FileTransmitter::FileTransmitter()
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
FileTransmitter::~FileTransmitter()
{
	if (inputFile.is_open())
	{
		inputFile.close();
	}
	if (outputFile.is_open())
	{
		outputFile.close();
	}
}
// return 0 
int FileTransmitter::Initialize(const string& filePath, bool isSender)
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
		ackOfChunks.assign(totalChunks, false);

		// Calculate CRC32 of the file
		inputFile.seekg(0, ios::beg);
		char data[FileDataChunkSize] = { 0 };
		while (inputFile.good())
		{
			inputFile.read(data, FileDataChunkSize);
			crc = CRC::Calculate(data, FileDataChunkSize, CRC::CRC_32(), crc);
		}
		// go back to the start of the file
		inputFile.seekg(0, ios::beg);
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

void FileTransmitter::LoadPacket(unsigned char packet[PacketSize])
{
	if (sender) 
	{
		switch (state) //
		{
		case WAVING:
			// MDID
			PackMetaData(packet);
			break;
		case SENDING:	
			if (inputFile.eof())
			{
				// Content: ENDID 
				packMessage(packet, ENDID, NULL, 0);
			}
			else
			{
				//FCID
				ReadChunk(packet);
			}
			break;
		}
	}
	else
	{
		switch (state)
		{
		case READY:
			// OK for receving file chunks.
			packMessage(packet, OKID, NULL, 0);
			break;
		case RECEIVING:
			// ACK for a chunk
			packMessage(packet, ACKID, &chunkIndex, sizeof(chunkIndex));
			break;
		case DISCONNECTING:
			packMessage(packet, DISID, &crc, sizeof(crc));
			break;
		}
	}
}
/*
* copy metadata to a Message with ID 0.
* Pack the Message into the packet.
*/
void FileTransmitter::PackMetaData(unsigned char packet[PacketSize])
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

bool FileTransmitter::ReadChunk(unsigned char packet[PacketSize])
{
	// TODO: implement resent logic.
	// reduce file reading time for resending.
	FileChunk fc = {};
	if (!inputFile.is_open())
	{
		cerr << "Error file not open: " << fileName << endl;
		return false;
	}
	char buffer[FileDataChunkSize];
	inputFile.read(buffer, FileDataChunkSize);
	if (inputFile)
	{
		memset(fc.data, 0, FileDataChunkSize);
		memcpy(fc.data, buffer, FileDataChunkSize);
		fc.chunkIndex = chunkIndex;

		// chunkIndex++;
		packMessage(packet, FCID, &fc, sizeof(fc));
	}
	if (inputFile.eof())
	{
		cout << "Reached end of file after reading" << endl;
	}
	return inputFile.good();
}

bool FileTransmitter::IsEOF() const
{
	return inputFile.eof();
}

string FileTransmitter::GetFileName() const
{
	return fileName;
}
State FileTransmitter::GetState() const
{
	return state; 
}


void FileTransmitter::ProcessPacket(unsigned char packet[PacketSize])
{
	memset(&rcMs, 0, sizeof(rcMs));
	memcpy(&rcMs, packet, sizeof(rcMs));
}
// call update after received a new message
void FileTransmitter::Update()
{
	switch (rcMs.id)
	{
	case MDID: // parse metadata
		if (state == LISTENING)
		{
			FileMetadata fm = { 0 };
			// deserialize packet to FileMetadata
			memcpy(&fm, rcMs.content, sizeof(fm));

			// store metadata, open output file and start to receive the file.
			fileName = fm.fileName;
			fileSize = fm.fileSize;
			totalChunks = fm.totalChunks;
			crc = fm.crc32;
			chunkReceived.assign(totalChunks,false);

			outputFile.open(fileName, std::ios::binary);
			if (outputFile.is_open())
			{
				std::cerr << "Error opening file for writing: " << fileName << std::endl;
				state = CRACKED;
				return;
			}	
			state = READY;
		}
		break;

	case FCID: // file chunk, write file data
		if (state == READY)
		{
			writeChunk();
			state = RECEIVING;
		}
		if (state == RECEIVING)
		{
			writeChunk();			
		}
		break;

	case ENDID:
		if (!sender && state == RECEIVING && chunkReceived.back())
		{
			outputFile.close();
			//state = CHECKING;
			// checke crc
			inputFile.open(fileName, ios::binary);
			if (!inputFile)
			{
				cerr << "Error opening file for computing CRC: " << fileName << endl;
				state = CRACKED;
				return;
			}
			uint32_t endCRC = 0;
			calculateFileCRC(inputFile,endCRC);
			if (endCRC != crc)
			{
				// 1.2 not equal to CRC, sent a request to resend the file.
				return;
			}
			
			state = DISCONNECTING;	
			// record current time 
			disconnectTime = clock();
		}
		break;

	default:

		break;
		// for the receiver 
		if (state = DISCONNECTING)
		{
			// back to ready after being in disconnecting state for 1s
			double duration = double(clock() - disconnectTime) / CLOCKS_PER_SEC;
			if (duration > 1)
			{
				Initialize("",false);
			}
		}


	}
	// 1.if all the element in chunkReceived == true, and receivd an END message, then 
	//	 close file and calculate CRC.
	//   1.1 result CRC is equal to crc. then switch back to ready state, waiting for the next file.
	//	 
	// 2. not all true :
}
void FileTransmitter::calculateFileCRC(ifstream& ifs, uint32_t& crc)
{
	char data[FileDataChunkSize] = { 0 };
	while (ifs.good())
	{
		ifs.read(data, FileDataChunkSize);
		crc = CRC::Calculate(data, FileDataChunkSize, CRC::CRC_32(), crc);
	}
}
void FileTransmitter::packMessage(unsigned char packet[PacketSize],
	uint32_t id, const void* content, size_t size)
{
	Message ms = {};
	ms.id = id;
	memset(ms.content, 0, ContentSize);
	memcpy(ms.content, content, size);
	memset(packet, 0, PacketSize);
	memcpy(packet, &ms, sizeof(ms));
}
void FileTransmitter::writeChunk()
{
	FileChunk fc = {};
	// deserialize packet 
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
		state = CRACKED;
	}
}
void FileTransmitter::read()
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

	//static std::ofstream outputFile;
	static std::vector<unsigned char> receivedFile;
	static FileMetadata receivedMetadata;
	static uint32_t receivedChunks = 0;

	while (true) {
		// deserialize the received data 
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
			// receivedFile.reserve(receivedMetadata.fileSize); // Pre-allocate exact size
		}
		else if (bytes_read == sizeof(FileChunk)) {
			// Received file chunk
			FileChunk chunk;
			memcpy(&chunk, packet, sizeof(FileChunk));

			// Ensure vector is properly sized before writing
			// not necessary 
			size_t offset = chunk.chunkIndex * FileDataChunkSize;
			size_t remaining = receivedMetadata.fileSize - offset;
			size_t copySize = (((FileDataChunkSize) < (remaining)) ? (FileDataChunkSize) : (remaining));

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
}