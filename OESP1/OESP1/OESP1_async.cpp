#include <windows.h>
#include <iostream>
#include <vector>
#include <string>
#include <fstream>

struct ThreadParams {
    HANDLE hFile;
    DWORD startOffset;
    DWORD bytesToRead;
    std::vector<char>* buffer;
    int threadId;
    OVERLAPPED overlapped;
};

std::vector<int> threads;
std::vector<double> times;

void CALLBACK ReadComplete(DWORD dwErrorCode, DWORD dwNumberOfBytesTransferred, LPOVERLAPPED lpOverlapped) {
    ThreadParams* params = reinterpret_cast<ThreadParams*>(lpOverlapped->hEvent);
    delete params;
}

DWORD WINAPI readFileChunk(LPVOID lpParams) {
    ThreadParams* params = static_cast<ThreadParams*>(lpParams);
    params->overlapped.hEvent = reinterpret_cast<HANDLE>(params); // Store the params for the callback
    params->overlapped.Offset = params->startOffset; // Set the starting offset
    params->overlapped.OffsetHigh = 0; // High order part of the offset is zero
    params->buffer->resize(params->bytesToRead);

    BOOL result = ReadFileEx(params->hFile, params->buffer->data(), params->bytesToRead, &params->overlapped, ReadComplete);
    if (!result) {
        DWORD error = GetLastError();
        std::cerr << "Error initiating asynchronous read for thread " << params->threadId << ". Error code: " << error << "\n";
        delete params;
        return 1;
    }
    return 0;
}

bool fileExists(const std::string& fileName) {
    std::wstring wideFileName(fileName.begin(), fileName.end());
    DWORD fileAttributes = GetFileAttributes(wideFileName.c_str());
    return fileAttributes != INVALID_FILE_ATTRIBUTES;
}

void read_file_multithread(std::string fileName, int numThreads) {
    HANDLE hFile = CreateFileA(fileName.c_str(), GENERIC_READ, /*FILE_SHARE_READ*/ 0, NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
    if (hFile == INVALID_HANDLE_VALUE) {
        std::cerr << "Error opening file.\n";
        return;
    }
    else
    {
        std::cout << "\nfile descriptor received\n";
    }

    LARGE_INTEGER fileSize;
    GetFileSizeEx(hFile, &fileSize);

    DWORD chunkSize = static_cast<DWORD>(fileSize.QuadPart / numThreads);
    DWORD lastChunkSize = chunkSize + static_cast<DWORD>(fileSize.QuadPart % numThreads);

    std::vector<HANDLE> threadHandles(numThreads);
    std::vector<std::vector<char>> buffers(numThreads);

    LARGE_INTEGER frequency, start, end;
    QueryPerformanceFrequency(&frequency);
    QueryPerformanceCounter(&start);

    for (int i = 0; i < numThreads; ++i) {
        DWORD currentChunkSize = (i == numThreads - 1) ? lastChunkSize : chunkSize;
        ThreadParams* params = new ThreadParams{ hFile, i * chunkSize, currentChunkSize, &buffers[i], i + 1, {} };

        threadHandles[i] = CreateThread(NULL, 0, readFileChunk, params, 0, NULL);
        if (threadHandles[i] == NULL) {
            std::cerr << "Error while creating thread.\n";
            delete params; // Clean up allocated memory
            CloseHandle(hFile);
            return;
        }
    }
    
    WaitForMultipleObjects(numThreads, threadHandles.data(), TRUE, INFINITE);

    for (HANDLE threadHandle : threadHandles) {
        CloseHandle(threadHandle);
    }
    
    CloseHandle(hFile);

    QueryPerformanceCounter(&end);
    double elapsedTime = static_cast<double>(end.QuadPart - start.QuadPart) / frequency.QuadPart;

    std::vector<char> finalData;
    for (const auto& buffer : buffers) {
        finalData.insert(finalData.end(), buffer.begin(), buffer.end());
    }

    //std::cout << "File reading time: " << elapsedTime << " seconds\n";
    //std::cout << "Reading file size: " << finalData.size() << " bytes\n";
    times.push_back(elapsedTime);
    threads.push_back(numThreads);
}

void writeDataToCSV(const std::string& filename, const std::vector<int>& threadCounts, const std::vector<double>& times) {
    std::ofstream outFile(filename);

    if (!outFile.is_open()) {
        std::cerr << "File open error\n";
        return;
    }

    outFile << "Количество потоков,Время\n";

    for (size_t i = 0; i < threadCounts.size(); ++i) {
        outFile << threadCounts[i] << "," << times[i] << "\n";
    }

    outFile.close(); // Закрытие файла
    std::cout << "Data successfully writed" << filename << "\n";
}

int main() {
    std::string fileName = "file4.txt";
    int numThreads;


    for (numThreads = 1; numThreads < 60; ++numThreads) {
        //std::cout << "Enter file name to read: ";
        //std::cin >> fileName;
        //std::cout << "Enter thread count: ";
        //std::cin >> numThreads;
        if (!fileExists(fileName)) {
            std::cout << "File does not exist. Please enter a valid file name.\n";
            continue;
        }

        read_file_multithread(fileName, numThreads);
        writeDataToCSV("result.csv", threads, times);
    }

    return 0;
}














