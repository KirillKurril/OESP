#include <windows.h>
#include <iostream>
#include <iomanip>
#include <chrono>
#include <cmath>
#include <vector>
#include <sstream>
#include <fstream>
#include <cstdlib>
#include <ctime>
#include <numeric>
#include <algorithm>
#define BASE_BUFFER_SIZE 256
using namespace std;
DWORD asyncIterationCount = 0;
DWORD syncIterationCount = 0;
int asyncReadCount = 0;
std::chrono::duration<double> asyncReadDuration;
std::chrono::duration<double> processingDuration;
std::chrono::duration<double> asyncWriteDuration;


void processFileData(const std::vector<int>& data, std::vector<int>& processedData) {
    int size = data.size();
    if (size == 0) return;

    for (int number : data) {
        processedData.push_back(number + 5);
    }
}

void createDataFile(const std::wstring& filename, int count) {
    std::ofstream outFile(filename);
    if (outFile.is_open()) {
        for (int i = 0; i < count; i++) {
            outFile << count - i << " ";
        }
        outFile.close();
    }
    else {
        std::wcout << L"Error opening file for writing: " << filename << "\n";
    }
}

void saveToFile(const std::vector<int>& processedData, const std::wstring& filename) {
    HANDLE hFile = CreateFile(
        filename.c_str(),
        GENERIC_WRITE,
        0, 
        NULL,
        OPEN_ALWAYS, 
        FILE_ATTRIBUTE_NORMAL,
        NULL
    );

    if (hFile != INVALID_HANDLE_VALUE) {
        SetFilePointer(hFile, 0, NULL, FILE_END);

        DWORD writtenBytes;
        std::wstring result;

        for (int value : processedData) {
            result += std::to_wstring(value) + L" ";
        }
        result += L"\n"; 

        WriteFile(hFile, result.c_str(), result.size() * sizeof(wchar_t), &writtenBytes, NULL);
        CloseHandle(hFile); 
    }
    else {
        std::wcerr << L"Error opening file for writing: " << filename << '\n';
    }
}

void CALLBACK onAsyncReadComplete(DWORD errorCode, DWORD bytesRead, LPOVERLAPPED lpOverlapped) {
    if (errorCode == 0) {
        lpOverlapped->Offset += bytesRead;
        char* buffer = (char*)lpOverlapped->hEvent;
        buffer[bytesRead] = '\0';

        std::vector<int> data;
        std::vector<int> processedData;
        std::istringstream iss(buffer);
        int value;
        while (iss >> value) {
            data.push_back(value);
        }

        auto processingStart = std::chrono::high_resolution_clock::now();

        processFileData(data, processedData);

        auto processingEnd = std::chrono::high_resolution_clock::now();
        processingDuration = processingEnd - processingStart;

        processingStart = std::chrono::high_resolution_clock::now();
        saveToFile(processedData, L"processed_data.txt");
        processingEnd = std::chrono::high_resolution_clock::now();
        asyncWriteDuration = processingEnd - processingStart;
    }
    else {
        if (errorCode == 38) {
            return;
        }
        std::cerr << "Error reading file: " << errorCode << std::endl;
    }
}

void performAsyncFileOperations(int bufferSize) {
    HANDLE hFile = CreateFile(L"data.txt", GENERIC_READ, 0, NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL | FILE_FLAG_OVERLAPPED, NULL);

    if (hFile == INVALID_HANDLE_VALUE) {
        std::cerr << "Error opening file: " << GetLastError() << std::endl;
        return;
    }

    char* buffer = new char[bufferSize];
    OVERLAPPED overlapped = { 0 };
    overlapped.hEvent = (HANDLE)buffer;

    std::ofstream outFile(L"stats_async.txt");
    outFile.close();

    while (true) {
        DWORD previousOffset = overlapped.Offset;
        if (!ReadFileEx(hFile, buffer, bufferSize - 1, &overlapped, onAsyncReadComplete)) {
            std::cerr << "Error initiating read: " << GetLastError() << std::endl;
            CloseHandle(hFile);
            return;
        }
        asyncReadCount++;

        SleepEx(INFINITE, TRUE);

        auto fileSize = GetFileSize(hFile, NULL);
        if (fileSize <= overlapped.Offset) {
            break;
        }
        if (previousOffset == overlapped.Offset) {
            break;
        }
    }

    CloseHandle(hFile);
    delete[] buffer;
}

DWORD performSyncFileOperations() {
    HANDLE hFile = CreateFile(
        L"data.txt",
        GENERIC_READ,
        0,
        NULL,
        OPEN_EXISTING,
        FILE_ATTRIBUTE_NORMAL,
        NULL);

    if (hFile == INVALID_HANDLE_VALUE) {
        std::cerr << "Could not open file (Error: " << GetLastError() << ").\n";
        return -1;
    }

    DWORD fileSize = GetFileSize(hFile, NULL);
    if (fileSize == INVALID_FILE_SIZE) {
        std::cerr << "Could not get file size (Error: " << GetLastError() << ").\n";
        CloseHandle(hFile);
        return -1;
    }

    char* buffer = new char[fileSize + 1];
    if (!buffer) {
        std::cerr << "Memory allocation failed." << std::endl;
        CloseHandle(hFile);
        return -1;
    }

    DWORD bytesRead;
    if (!ReadFile(hFile, buffer, fileSize, &bytesRead, NULL)) {
        std::cerr << "Could not read file (Error: " << GetLastError() << ")." << std::endl;
        delete[] buffer;
        CloseHandle(hFile);
        return -1;
    }

    buffer[bytesRead] = '\0';

    std::vector<int> data;
    std::vector<int> processedData;
    std::istringstream iss(buffer);
    int value;
    while (iss >> value) {
        data.push_back(value);
    }

    processFileData(data, processedData);

    saveToFile(data, L"processed_sync.txt");

    delete[] buffer;
    CloseHandle(hFile);
    return fileSize + 1;
}

int main() {
    createDataFile(L"data.txt", 100000);

    int i = 0;
    bool stop = false;
    std::cout << "Asynchronous Execution:\n";
    while (true) {
        int bufferMultiplier = pow(2, i);
        i++;
        int maxBufferSize = BASE_BUFFER_SIZE * bufferMultiplier;
        if (maxBufferSize >= 1e6) {
            break;
        }
        auto asyncStart = std::chrono::high_resolution_clock::now();
        performAsyncFileOperations(maxBufferSize);
        auto asyncEnd = std::chrono::high_resolution_clock::now();

        std::chrono::duration<double> asyncDuration = asyncEnd - asyncStart;
        std::cout << "Buffer size: " << maxBufferSize << '\t'
            << fixed << setprecision(3) << " Total time: " << asyncDuration.count() << '\t'
            << " Write time: " << asyncWriteDuration.count() * asyncReadCount << "\t"
            << " Read time: " << abs(asyncDuration.count() - (asyncWriteDuration.count() + processingDuration.count()) * asyncReadCount) << "\t"
            << " Processing time: " << processingDuration.count() * asyncReadCount << "\n";

        asyncIterationCount = 0;
        asyncReadCount = 0;
    }

    auto syncStart = std::chrono::high_resolution_clock::now();
    auto syncBufferSize = performSyncFileOperations();
    auto syncEnd = std::chrono::high_resolution_clock::now();

    std::chrono::duration<double> syncDuration = syncEnd - syncStart;
    std::cout << "\nSynchronous Execution:\n";
    std::cout << "File size: " << syncBufferSize << '\t'
        << " Total time: " << syncDuration.count() << '\n';

    return 0;
}
