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


void computeStatistics(const std::vector<int>& data, double& mean, double& stdev, int& minValue, int& maxValue) {
    int size = data.size();
    if (size == 0) return;
    mean = std::accumulate(data.begin(), data.end(), 0.0) / size;
    minValue = *std::min_element(data.begin(), data.end());
    maxValue = *std::max_element(data.begin(), data.end());

    double varianceSum = 0.0;
    for (int value : data) {
        varianceSum += (value - mean) * (value - mean);
    }
    stdev = std::sqrt(varianceSum / size);
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

void saveToFile(double mean, double stdev, int minValue, int maxValue, const std::wstring& filename) {
    HANDLE hFile = CreateFile(
        filename.c_str(),
        GENERIC_WRITE,
        FILE_APPEND_DATA,
        NULL,
        OPEN_ALWAYS,
        FILE_ATTRIBUTE_NORMAL,
        NULL
    );

    if (hFile != INVALID_HANDLE_VALUE) {
        DWORD writtenBytes;
        std::wstring result = L"Mean: " + std::to_wstring(mean) + L", Std Dev: " + std::to_wstring(stdev) +
            L", Min: " + std::to_wstring(minValue) + L", Max: " + std::to_wstring(maxValue) + L"\n";
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
        std::istringstream iss(buffer);
        int value;
        while (iss >> value) {
            data.push_back(value);
        }

        auto processingStart = std::chrono::high_resolution_clock::now();

        double mean, stdev;
        int minValue, maxValue;
        computeStatistics(data, mean, stdev, minValue, maxValue);

        auto processingEnd = std::chrono::high_resolution_clock::now();
        processingDuration = processingEnd - processingStart;

        processingStart = std::chrono::high_resolution_clock::now();
        saveToFile(mean, stdev, minValue, maxValue, L"stats_async.txt");
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
    std::istringstream iss(buffer);
    int value;
    while (iss >> value) {
        data.push_back(value);
    }

    double mean, stdev;
    int minValue, maxValue;
    computeStatistics(data, mean, stdev, minValue, maxValue);

    saveToFile(mean, stdev, minValue, maxValue, L"stats_sync.txt");

    delete[] buffer;
    CloseHandle(hFile);
    return fileSize + 1;
}

int main() {
    createDataFile(L"data.txt", 10000);

    int i = 0;
    bool stop = false;
    std::cout << "Asynchronous Execution:\n";
    while (true) {
        int bufferMultiplier = pow(2, i);
        i++;
        int maxBufferSize = BASE_BUFFER_SIZE * bufferMultiplier;
        if (maxBufferSize >= 1e5) {
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
    std::cout << "Buffer size: " << syncBufferSize << '\t'
        << " Total time: " << syncDuration.count() << '\n';

    return 0;
}
