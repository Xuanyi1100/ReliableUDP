#pragma once

#include <iostream>
#include <fstream>
#include <filesystem>
#include <vector>
#include "CRC.h"
using namespace std;

namespace udpft
{
    const int PacketSize = 256;
    const int MaxFileNameLength = 128;
    const int ContentSize = PacketSize - sizeof(uint32_t);
    const int FileDataChunkSize = PacketSize - 2 * sizeof(uint32_t);
    // Signal

    // ID for different types of message 
    const uint32_t MDID = 1;
    const uint32_t FCID = 2;
    const uint32_t ENDID = 3;

    enum State {
        CRACKED = 0,
        // for a sender:
        HOLD,
        SENDING,
        // CLOSING,
        // for a receiver 
        READY,
        RECEIVING,
        CHECKING,
        DISCONNECTING,
	};

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

    struct Message {
        uint32_t id;
        unsigned char content[ContentSize];
    };
#pragma pack(pop)

    class FileTransmitter {

    private:

        ifstream inputFile;
        ofstream outputFile;

        vector<bool> chunkReceived; // for the receiver, check if a chunk is received and written.
        vector<bool> ackOfChunks; // for receiver check if received a file chunk ack.
        Message rcMs;

        State state;
        bool sender;

        string fileName;
        int fileSize;
        int totalChunks;
        uint32_t crc;

        uint32_t chunkIndex; // where to set it?
        clock_t disconnectTime;
        
        // map<int, float> sendTimes;
        inline void calculateFileCRC(ifstream& ifs, uint32_t& crc);
        inline void packMessage(unsigned char packet[PacketSize], 
            uint32_t id, const void* content, size_t size);
    public:

        FileTransmitter();
        ~FileTransmitter();
        int Initialize(const string& filePath, bool isSender);
        void Close();
        void PackMetaData(unsigned char packet[PacketSize]);
        bool ReadChunk(unsigned char packet[PacketSize]);
        void PackEOF(unsigned char packet[PacketSize]);
        bool IsEOF() const;
        string GetFileName() const;
        State GetState() const;
        uint32_t GetTotalChunks();
        uint32_t GetChunkIndex();
        void ProcessPacket(unsigned char packet[PacketSize]);
        void Update();

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
}