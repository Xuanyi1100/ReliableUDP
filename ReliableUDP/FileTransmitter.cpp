#include "FileTransmitter.h"
using namespace udpft;
FileTransmitter::FileTransmitter()
{
	crc = 0;
	totalChunks = 0;
	sender = true;
}
int FileTransmitter::InitializeSender(const string& filePath)
{
	// set file name
	fileName = filesystem::path(filePath).filename().string();
	if (fileName.length() > MaxFileNameLength -1)
	{
		cerr << "Error: File name out of length limit: " << filePath << std::endl;
		return -1;
	}
		// open the file 
		inputFile.open(filePath, ios::binary | ios::ate);
		if (!inputFile)
		{
			cerr << "Error opening file for reading: " << filePath << std::endl;
			return -1;
		}

		fileSize = inputFile.tellg();
		totalChunks = (fileSize + 255) / 256;

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
bool FileTransmitter::PackMetaData(unsigned char packet[], int size)
{
	if (size > PacketSize)
	{
		return false;
	}
	// Prepare metadata
	FileMetadata metadata = {};
	Message ms = {};
	strncpy(metadata.fileName, fileName.c_str(), MaxFileNameLength - 1);
	metadata.fileName[MaxFileNameLength - 1] = '\0';
	metadata.fileSize = fileSize;
	metadata.totalChunks = (fileSize + FileDataChunkSize - 1) / FileDataChunkSize;
	metadata.crc32 = crc;

	ms.id = MDID;
	memcpy(ms.content, &metadata, sizeof(FileMetadata));
	// clear the packet
	memset(packet, 0, size);
	memcpy(packet,&ms,size);
	return true;
}
void FileTransmitter::read()
{



	// Read file contents
	std::ifstream ifs(filePath, std::ios::binary | std::ios::ate);
	if (!ifs.is_open()) {
		printf("Error opening file: %s\n", filePath.c_str());
		return 1;
	}

	uint32_t fileSize = ifs.tellg();
	if (fileSize <= 0) {
		printf("Error getting file size: %s\n", filePath.c_str());
		return 1;
	}
	ifs.seekg(0);
	std::vector<unsigned char> fileData(fileSize);
	ifs.read(reinterpret_cast<char*>(fileData.data()), fileSize);
	ifs.close();

	// Calculate CRC32
	uint32_t fileCRC = CRC::Calculate(fileData.data(), fileData.size(), CRC::CRC_32());

	
	// Send metadata

	// TODO: serialize metadata

	connection.SendPacket(reinterpret_cast<unsigned char*>(&metadata), sizeof(metadata));

	// Splits file into chunks and sends them
	for (uint32_t i = 0; i < metadata.totalChunks; i++) {
		FileChunk chunk;
		chunk.chunkIndex = i;
		size_t offset = i * FileDataChunkSize; // offset for fileData
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