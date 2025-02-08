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
    const uint32_t OKID = 4;
    const uint32_t ACKID = 5;
    const uint32_t DISID = 6;

    const double DISCONNECT_DURATION = 1000; // milliseconds for saying goodbye to the sender.
    enum State {
        CRACKED = 0,
        // for a receiver 
        LISTENING,
        READY,
        RECEIVING,
        DISCONNECTING,
        // for a sender:
        WAVING,
        SENDING,
        CLOSED,
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

    class FileTeleporter {

    private:

        ifstream inputFile;
        ofstream outputFile;

        vector<bool> chunkReceived; // for the receiver, check if a chunk is received.
        vector<bool> ackOfChunks; // for the sender, check if received a file chunk ack.
        Message rcMs;      // store the received message.
        FileChunk fc;

        State state; 
        bool sender;
       
        /***** metadata of the transfering file *****/
        string fileName;
        int fileSize;
        int totalChunks;
        uint32_t crc;

        /*************/

        uint32_t chunkIndex;                // for sending or writing a file chunk
        chrono::steady_clock::time_point disconnectTime;
        
        
        inline void calculateFileCRC(ifstream& ifs, uint32_t& crc);
        inline void openFileForWriting();
        inline void packMessage(unsigned char packet[PacketSize], 
            uint32_t id, const void* content, size_t size);
        void packMetaData(unsigned char packet[PacketSize]);
        void readChunk(unsigned char packet[PacketSize]);
        void storeMetadata(); // for receiver 
        void writeChunk();

    public:

        FileTeleporter();
        ~FileTeleporter();
        void Close();

        uint32_t GetFileCRC() const;
        string GetFileName() const;
        uint32_t GetFileSize() const;
        
        State GetState() const;
        bool Initialize(const string& filePath, bool isSender);
        void LoadPacket(unsigned char packet[PacketSize]);
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