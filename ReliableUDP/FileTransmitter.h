#pragma once

#include <iostream>
#include <fstream>
#include <filesystem>
#include <vector>
#include "CRC.h"
using namespace std;

const int PacketSize = 256;
const int MaxFileNameLength = 128;
const int ContentSize = PacketSize -sizeof(uint32_t);
const int FileDataChunkSize = PacketSize - 2 * sizeof(uint32_t);

#pragma pack(push, 1) // for serialize structs
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

struct MESSAGE {
	uint32_t id;
	unsigned char content[ContentSize];
};
#pragma pack(pop)

class FileTransmitter {

private:

    ifstream inputFile;
    ofstream outputFile;
    string fileName;
    int fileSize;
    int totalChunks;
    bool sender;
    uint32_t crc;

    void calculateFileCRC(ifstream fs);

public:

    FileTransmitter();
    ~FileTransmitter();
    int InitializeSender(const string& filePath);

    bool ReadNextChunk(std::vector<char>& buffer, int& chunkIndex);
    void WriteChunk(const std::vector<char>& buffer, int chunkIndex);
    bool IsEOF() const;
    string GetFileName() const;

    void PackMessage(uint32_t id, unsigned char content[]);
    

    
    // load a file from disk

    //for sending the file
    // get the metadata of the file
    // metadata: file name, file size, checksum, number of chunks
    // serialize and deserialize metadata.
    // break the File into chunks

    //for receiving the file
    // parse and validate the metadata packet.
    // receive the File Pieces and append each chunk to the correct file.
    // verify integrity after all chunks have been received:
    // compute the checksum/hash of the received file and compare it with the checksum in the metadata.
    // If the checksum doesn’t match, report an error and request the file again.
    // save the file to disk

        // serialize the metadata

};