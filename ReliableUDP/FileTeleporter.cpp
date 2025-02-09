#include "FileTeleporter.h"
using namespace udpft;
FileTeleporter::FileTeleporter()
{
	rcMs = {};
	fc = {};
	sender = false;
	state = CRACKED;
	fileSize = 0;
	crc = 0;
	totalChunks = 0;
	fileName = DefaultFileName;
	chunkIndex = 0;
	resent = false;
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
	fileData.clear();
	if (sender) 
	{
		state = CLOSED;
		std::cout << "Sender Closed" << endl;
	}
}
uint32_t FileTeleporter::GetFileCRC() const
{
	return crc;
}
string FileTeleporter::GetFileName() const
{
	return fileName;
}
uint32_t FileTeleporter::GetFileSize() const
{
	return fileSize;
}
State FileTeleporter::GetState() const
{
	return state;
}

bool FileTeleporter::Initialize(const string& filePath, bool isSender)
{
	sender = isSender;
	if (sender)
	{
		// set file name
		fileName = filesystem::path(filePath).filename().string();

		if (fileName.length() > MaxFileNameLength - 1)
		{
			cerr << "Error: File name out of length limit: " << filePath << endl;
			return false;
		}
		// open the file 
		inputFile.open(filePath, ios::binary | ios::ate);
		if (!inputFile.is_open())
		{
			cerr << "Error opening file for reading: " << fileName << endl;
			return false;
		}
		fileSize = inputFile.tellg();
		inputFile.seekg(0,ios::beg);
		totalChunks = (fileSize + FileDataChunkSize - 1) / FileDataChunkSize;
		ackOfChunks.assign(totalChunks, false);

		// read file to a buffer.
		fileData.assign(fileSize,0);
		inputFile.read(fileData.data(), fileSize);
		int bytesRead = inputFile.gcount();
		if (bytesRead != fileSize)
		{
			cerr << "Error reading the file: " << fileName << endl;
			return false;
		}
		inputFile.close();
		if (inputFile.fail())
		{
			cerr << "Error closing the file: " << filePath << endl;
			return false;
		}
		// Calculate CRC32 of the file
		crc = calculateFileCRC();
		state = WAVING;
		std::cout<< "Waving the file: " << filePath << endl;
	}
	else // receiver 
	{
		rcMs = {};
		fc = {};
		fileSize = 0;
		crc = 0;
		totalChunks = 0;
		fileName = DefaultFileName;
		resent = false;
		fileData.clear();
		chunkReceived.clear();
		state = LISTENING;
		std::cout << "File receiver listening" << endl;
	}
	return true;
}

void FileTeleporter::LoadPacket(unsigned char packet[PacketSize])
{
	if (sender) // client
	{
		switch (state) 
		{
		case WAVING:
			// MDID
			packMetaData(packet);
			break;
		case SENDING:
			if (ackOfChunks.empty() || (chunkIndex == totalChunks && ackOfChunks.back()))
			{
				// ENDID 
				packMessage(packet, ENDID, &crc, sizeof(crc));
			}
			else
			{
				// FCID				
				packMessage(packet, FCID, &fc, sizeof(fc));
			}
			break;
		case CRACKED:
			break;
		default:
			return;
		}
	}
	else // server 
	{
		switch (state)
		{
		case READY:
			if (resent)
			{
				// RSID request file resent
				packMessage(packet, RSID, &crc,sizeof(crc));
			}
			else
			{
				// OKID
				// OK for receving file chunks.
				packMessage(packet, OKID, &crc, sizeof(crc));
			}
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
			memset(packet,0,sizeof(packet));
			return;
		}
	}
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

	if (state == DISCONNECTING)
	{
		// back to ready after being in disconnecting state for 1s
		double duration = chrono::duration<double, milli>(
			chrono::steady_clock::now() - disconnectTime).count();
		if (duration > DISCONNECT_DURATION)
		{
			Initialize(DefaultFileName, false);
		}
	}
	switch (rcMs.id)
	{
	case MDID: // parse metadata
		if (state == LISTENING)
		{
			storeMetadata();
			fileData.assign(fileSize, 0);
			state = READY;
			std::cout << "Receiver is ready" << endl;		
		}
		break;

	case FCID: // file chunk, store file data
		if (state == READY)
		{
			storeChunk();
			state = RECEIVING;
			resent = false;
			std::cout << " Receiving the file" << endl;
		}
		if (state == RECEIVING)
		{
			storeChunk();
		}
		break;

	case ENDID:
		if ((state == READY && fileSize == 0) ||(state == RECEIVING && chunkReceived.back()))
		{
			uint32_t finalCRC = calculateFileCRC();
			if (finalCRC != crc)
			{
				resent = true;
				cerr << " File verification failed:" << fileName << endl;
				cerr << " Original File CRC: " << crc << endl;
				cerr << " Received File CRC: " << finalCRC << endl;

				// not equal to CRC, be prepare for receiving the file from the head.
				chunkReceived.assign(totalChunks, false);				
				fileData.assign(fileSize, 0);
				chunkIndex = 0;
				state = READY;
				std::cout << " Ready for retransmission" << endl;
			}
			else
			{
				writeFile();
				if (state == CRACKED) return;
				printf("%s Received\n", fileName.c_str());
				printf("Received file size: %u bytes\n", fileSize);
				printf("Original CRC claim: 0x%08X\n", crc);
				state = DISCONNECTING;
				std::cout << " Disonnecting " << endl;
				// record current time
				disconnectTime = chrono::high_resolution_clock::now();
			}
		}
		break;

/********************* File SENDER ***************/

	case OKID:
		if (state == WAVING)
		{
			chunkIndex = 0;			
			state = SENDING;
			readChunk();
			std::cout << " Sending the file" << endl;
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
				ackOfChunks[chunkIndex++] = true;
				readChunk();
			}
		}
		break;
	case DISID:
		if (ackOfChunks.empty() || (state == SENDING && ackOfChunks.back()))
		{
			Close();
		}
		break;
	case RSID:
		if (state == SENDING)
		{
			chunkIndex = 0;
			ackOfChunks.assign(totalChunks,false);
			memset(&fc, 0, sizeof(fc));
			memset(&rcMs, 0, sizeof(rcMs));
		}
		break;
	default:
		break;
	}
}

uint32_t FileTeleporter::calculateFileCRC()
{
	return CRC::Calculate(fileData.data(), fileData.size(), CRC::CRC_32());
}

void FileTeleporter::writeFile()
{
	outputFile.open(fileName, ios::binary);
	if (!outputFile.is_open())
	{
		cerr << "Error opening file for writing: " << fileName << std::endl;
		state = CRACKED;
		return;
	}
	outputFile.write(fileData.data(),fileData.size());
	if (!outputFile.good())
	{
		cerr << "Error opening file for writing: " << fileName << std::endl;
		state = CRACKED;
		return;
	}
	outputFile.close();
	if (inputFile.fail())
	{
		cerr << "Error closing the file: " << fileName << endl;
		state = CRACKED;
		return;
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
/*
* copy metadata to a Message with ID 0.
* Pack the Message into the packet.
*/
void FileTeleporter::packMetaData(unsigned char packet[PacketSize])
{
	// Prepare metadata
	FileMetadata metadata = {};
	memcpy(metadata.fileName, fileName.c_str(), MaxFileNameLength - 1);
	metadata.fileName[MaxFileNameLength - 1] = '\0';
	metadata.fileSize = fileSize;
	metadata.totalChunks = (fileSize + FileDataChunkSize - 1) / FileDataChunkSize;
	metadata.crc32 = crc;

	packMessage(packet, MDID, &metadata, sizeof(metadata));
}

void FileTeleporter::readChunk()
{
	if (chunkIndex < totalChunks)
	{
		fc.chunkIndex = chunkIndex;
		size_t offset = chunkIndex * FileDataChunkSize;
		size_t chunkSize = (((FileDataChunkSize) < (fileSize - offset))
			? (FileDataChunkSize) : (fileSize - offset));
		// Copy from memory buffer instead of re-reading file
		memcpy(fc.data, fileData.data() + offset, chunkSize);
		// Fill remaining space with zeros if needed
		if (chunkSize < FileDataChunkSize)
		{
			memset(fc.data + chunkSize, 0, FileDataChunkSize - chunkSize);
		}
	}
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
void FileTeleporter::storeChunk()
{
	memset(&fc, 0, sizeof(fc));
	memcpy(&fc, rcMs.content, sizeof(fc));
	chunkIndex = fc.chunkIndex;	
	// write to file data buffer
	// don't rewrite data having been already written
	if (!chunkReceived[chunkIndex])
	{
		// Ensure vector is properly sized before writing
		size_t offset = fc.chunkIndex * FileDataChunkSize;
		size_t remaining = fileSize - offset;

		// Only write valid bytes in the final chunk 
		size_t copySize = (((FileDataChunkSize) < (remaining)) ?
			(FileDataChunkSize) : (remaining));

		if (offset + copySize > fileData.size())
		{
			fileData.resize(offset + copySize);
		}
		memcpy(fileData.data() + offset, fc.data, copySize);
		// to sent an ack with chunkIndex.
		chunkReceived[chunkIndex] = true;
	}
}
