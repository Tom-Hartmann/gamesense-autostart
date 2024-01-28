#include <cstdio>
#include <windows.h>
#include <tlhelp32.h>
#include <iostream>
#include <fstream>
#include <filesystem>
#include <map>
#include <thread>
#include <string>
#include <fstream>
#include <sstream>

namespace fs = std::filesystem;

std::map<DWORD, int> foundProcesses;
std::wstring loaderPath; // Use wstring for paths

std::wstring ReadConfig(const std::wstring& filename) { // Use wstring for filenames
    std::wifstream file(filename);
    std::wstring line;
    if (file.is_open()) {
        while (std::getline(file, line)) {
            std::wistringstream is_line(line);
            std::wstring key;
            if (std::getline(is_line, key, L'=')) {
                std::wstring value;
                if (std::getline(is_line, value)) {
                    if (key == L"path") {
                        value.erase(std::remove(value.begin(), value.end(), L'\"'), value.end());
                        if (!value.empty() && value.back() != L'\\') {
                            value += L'\\';
                        }

                        std::wcout << L"Config read: " << value << std::endl;
                        return value;
                    }
                }
            }
        }
        file.close();
    }
    else {
        std::wcout << L"Failed to open config file: " << filename << std::endl;
    }
    return L"";
}

DWORD FindProcessId(const std::wstring& processName) {
    PROCESSENTRY32 processInfo;
    processInfo.dwSize = sizeof(processInfo);

    HANDLE processesSnapshot = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, NULL);
    if (processesSnapshot == INVALID_HANDLE_VALUE) {
        std::wcout << L"Failed to create snapshot of processes." << std::endl;
        return 0;
    }

    DWORD processID = 0;
    if (Process32First(processesSnapshot, &processInfo)) {
        do {
            if (!processName.compare(processInfo.szExeFile) && foundProcesses.find(processInfo.th32ProcessID) == foundProcesses.end()) {
                processID = processInfo.th32ProcessID;
                std::wcout << L"Found process ID: " << processID << std::endl;
                break;
            }
        } while (Process32Next(processesSnapshot, &processInfo));
    }

    CloseHandle(processesSnapshot);
    return processID;
}

void RenameRandomExe(const std::wstring& directoryPathStr) {
    std::wstring pathWithoutQuotes = directoryPathStr;
    pathWithoutQuotes.erase(std::remove(pathWithoutQuotes.begin(), pathWithoutQuotes.end(), L'\"'), pathWithoutQuotes.end());

    fs::path directoryPath = fs::path(pathWithoutQuotes);

    std::wcout << L"Renaming first .exe in directory: " << directoryPath << std::endl;

    try {
        for (const auto& entry : fs::directory_iterator(directoryPath)) {
            if (entry.is_regular_file() && entry.path().extension() == L".exe") {
                fs::path newFileName = directoryPath / L"loader.exe";
                std::wcout << L"Renaming " << entry.path() << L" to " << newFileName << std::endl;
                fs::rename(entry.path(), newFileName);
                break;
            }
        }
    }
    catch (const fs::filesystem_error& e) {
        std::wcerr << L"Filesystem error: " << e.what() << std::endl;
    }
}

void RunLoader(DWORD processID, int loadValue) {
    std::wcout << L"Preparing to run loader with PID " << processID << L" and load value " << loadValue << std::endl;

    // Remove quotes and ensure the path ends with a backslash
    std::wstring pathWithoutQuotes = loaderPath;
    pathWithoutQuotes.erase(std::remove(pathWithoutQuotes.begin(), pathWithoutQuotes.end(), L'\"'), pathWithoutQuotes.end());
    if (!pathWithoutQuotes.empty() && pathWithoutQuotes.back() != L'\\') {
        pathWithoutQuotes += L'\\';
    }

    fs::path directoryPath = fs::path(pathWithoutQuotes);
    std::wstring exePath;

    // Find the first .exe file in the directory
    try {
        for (const auto& entry : fs::directory_iterator(directoryPath)) {
            if (entry.is_regular_file() && entry.path().extension() == L".exe") {
                exePath = entry.path().wstring();
                std::wcout << L"Found .exe file: " << exePath << std::endl;
                break;
            }
        }
    }
    catch (const fs::filesystem_error& e) {
        std::wcerr << L"Filesystem error while searching for .exe file: " << e.what() << std::endl;
        return;
    }

    if (!exePath.empty()) {
        STARTUPINFO si;
        PROCESS_INFORMATION pi;
        ZeroMemory(&si, sizeof(si));
        si.cb = sizeof(si);
        ZeroMemory(&pi, sizeof(pi));

        // Construct the command
        std::wstring command = L"\"" + exePath + L"\" --pid=" + std::to_wstring(processID) + L" --load=" + std::to_wstring(loadValue);
        std::wcout << L"Executing command: " << command << std::endl;

        // Convert command to LPWSTR (non-const wchar_t*)
        wchar_t* cmd = new wchar_t[command.size() + 1];
        std::copy(command.begin(), command.end(), cmd);
        cmd[command.size()] = L'\0';

        // Start the child process with the specified working directory
        if (!CreateProcessW(NULL,   // No module name (use command line)
            cmd,                   // Command line
            NULL,                  // Process handle not inheritable
            NULL,                  // Thread handle not inheritable
            FALSE,                 // Set handle inheritance to FALSE
            0,                     // No creation flags
            NULL,                  // Use parent's environment block
            pathWithoutQuotes.c_str(), // Set working directory
            &si,                   // Pointer to STARTUPINFO structure
            &pi)                   // Pointer to PROCESS_INFORMATION structure
            )
        {
            std::wcout << L"CreateProcessW failed (" << GetLastError() << L")" << std::endl;
        }

        // Close process and thread handles
        CloseHandle(pi.hProcess);
        CloseHandle(pi.hThread);

        delete[] cmd;
    }
    else {
        std::wcout << L"No .exe file found in directory: " << directoryPath << std::endl;
    }
}

int main() {
    std::wcout << L"Starting program..." << std::endl;

    loaderPath = ReadConfig(L"config.ini");
    if (loaderPath.empty()) {
        std::wcerr << L"Loader path not found in config.ini" << std::endl;
        return 1;
    }

    while (true) {
        DWORD csgoID = FindProcessId(L"csgo.exe");
        if (csgoID) {
            foundProcesses[csgoID] = 1;
            RunLoader(csgoID, 1);
        }

        DWORD cs2ID = FindProcessId(L"cs2.exe");
        if (cs2ID) {
            foundProcesses[cs2ID] = 1;
            RunLoader(cs2ID, 128);
        }

        DWORD rustID = FindProcessId(L"rust.exe");
        if (rustID) {
            foundProcesses[rustID] = 1;
            RunLoader(rustID, 16);
        }

        std::this_thread::sleep_for(std::chrono::seconds(3));
    }

    return 0;
}
