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
	fileName = "default";
	chunkIndex = 0;
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
	if(sender) state = CLOSED;
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
		if (!inputFile)
		{
			cerr << "Error opening file for reading: " << filePath << endl;
			return false;
		}

		fileSize = inputFile.tellg();
		totalChunks = (fileSize + FileDataChunkSize - 1) / FileDataChunkSize;
		// Calculate CRC32 of the file
		inputFile.seekg(0, ios::beg);
		calculateFileCRC(inputFile, crc);
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
	return true;
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
			if (state == CRACKED) return;
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
				cerr << " File verification failed:" << fileName << endl;
				cerr << " Original File CRC: " << crc << endl;
				cerr << " Received File CRC: " << finalCRC << endl;

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
				disconnectTime = chrono::high_resolution_clock::now();
			}
		}
		break;
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
		crc = CRC::Calculate(data, ifs.gcount(), CRC::CRC_32(), crc);
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
		/******************for test ********************/
		size_t bytesRead = inputFile.gcount();
		if (bytesRead < FileDataChunkSize)
		{
			std::cout << "Read " << bytesRead << " bytes in the last time" << std::endl;
		}
		/***********************/ 
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
	size_t bytesWrite = FileDataChunkSize;
	if (chunkIndex == totalChunks - 1)
	{
		bytesWrite = fileSize % FileDataChunkSize;
	}

	// write to file
	// don't rewrite data having been already written
	if (!chunkReceived[chunkIndex] && outputFile.good())
	{
		outputFile.seekp(chunkIndex * FileDataChunkSize);
		outputFile.write((char*)fc.data, bytesWrite);
		chunkReceived[chunkIndex] = true;
		// to sent an ack with chunkIndex.
	}
	if (!outputFile.good())
	{
		cerr << "Error writing file chunk: " << fileName << endl;
		state = CRACKED;
	}
}
