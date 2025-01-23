#pragma once
#ifndef FILE_TRANSFER_H
#define FILE_TRANSFER_H

#include <string>
#include <vector>
#include "Net.h"

class FileTransfer
{
public:
    FileTransfer();
    ~FileTransfer();

    bool LoadFile(const std::string& filePath);
    bool SaveFile(const std::string& filePath);
    bool SendFile(net::ReliableConnection& connection);
    bool ReceiveFile(net::ReliableConnection& connection);
    bool VerifyFile(const std::string& originalFilePath, const std::string& receivedFilePath);

private:
    std::string filePath;
    std::vector<unsigned char> fileBuffer;
    size_t fileSize;
    // Add metadata members here if needed (e.g., filename, etc.)
};

#endif // FILE_TRANSFER_H