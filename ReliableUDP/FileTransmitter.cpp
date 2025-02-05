#include "FileTransmitter.h"
using namespace udpft;
FileTransmitter::FileTransmitter()
{
	crc = 0;
	totalChunks = 0;
	sender = true;
	ms = {};
	fc = {};
	buffer[FileDataChunkSize] = { 0 };
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
int FileTransmitter::InitializeSender(const string& filePath)
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
	ackOfChunks.resize(totalChunks, false);

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

	//else // receiver 
	//{
	//	// TODO:
	//	outputFile.open(filePath, std::ios::binary);
	//	if (!outputFile)
	//	{
	//		std::cerr << "Error opening file for writing: " << filePath << std::endl;
	//		return -1;
	//	}
	//}
	return 0;
}

/*
* copy metadata to a Message with ID 0.
* Pack the Message into the packet.
*/
bool FileTransmitter::PackMetaData(unsigned char packet[PacketSize])
{
	// Prepare metadata
	FileMetadata metadata = {};
	strncpy(metadata.fileName, fileName.c_str(), MaxFileNameLength - 1);
	metadata.fileName[MaxFileNameLength - 1] = '\0';
	metadata.fileSize = fileSize;
	metadata.totalChunks = (fileSize + FileDataChunkSize - 1) / FileDataChunkSize;
	metadata.crc32 = crc;

	packMessage(packet, MDID, &metadata,sizeof(metadata));
	return true;
}

bool FileTransmitter::ReadChunk(unsigned char packet[PacketSize])
{
	if (!inputFile.is_open())
	{
		cerr << "Error file not open: " << fileName << endl;
		return false;
	}
	inputFile.read(buffer, FileDataChunkSize);
	if (inputFile)
	{
		memset(fc.data, 0, FileDataChunkSize);
		memcpy(fc.data, buffer, FileDataChunkSize);
		fc.chunkIndex = chunkIndex;
		// sendTimes[chunkIndex] = (float)clock() / CLOCKS_PER_SEC; // 记录发送时间
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

void FileTransmitter::WriteChunk(const std::vector<char>& buffer, int chunkIndex)
{
	if (outputFile)
	{
		outputFile.seekp(chunkIndex * 256);
		outputFile.write(buffer.data(), buffer.size());
	}
}

void FileTransmitter::packMessage(unsigned char packet[PacketSize],
	unsigned char id, const void* content, size_t size)
{
	ms.id = id;
	memset(ms.content, 0, ContentSize);
	memcpy(ms.content, content, size);
	memset(packet, 0, PacketSize);
	memcpy(packet, &ms, PacketSize);
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