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
#include <algorithm>
//#define DEBUG
namespace fs = std::filesystem;

std::map<DWORD, int> foundProcesses;
std::wstring loaderPath; // Use wstring for paths

void CreateConsole()
{
    AllocConsole();
    HANDLE hOut = GetStdHandle(STD_OUTPUT_HANDLE);
    DWORD dwMode = 0;
    GetConsoleMode(hOut, &dwMode);
    dwMode |= ENABLE_VIRTUAL_TERMINAL_PROCESSING;
    SetConsoleMode(hOut, dwMode);
    FILE* stream = nullptr;
    freopen_s(&stream, "CONOUT$", "w", stdout);
}

bool IsRunningAsAdmin() {
    BOOL fIsElevated = FALSE;
    HANDLE hToken = NULL;
    if (OpenProcessToken(GetCurrentProcess(), TOKEN_QUERY, &hToken)) {
        TOKEN_ELEVATION Elevation;
        DWORD dwSize;
        if (GetTokenInformation(hToken, TokenElevation, &Elevation, sizeof(Elevation), &dwSize)) {
            fIsElevated = Elevation.TokenIsElevated;
        }
    }
    if (hToken) {
        CloseHandle(hToken);
    }
    return fIsElevated;
}

void PromptForAdminRights() {
    MessageBox(NULL,
        L"This application requires elevated privileges to function correctly. Please restart it as an administrator.",
        L"Administrator Rights Required",
        MB_OK | MB_ICONEXCLAMATION);
}

std::wstring ReadConfig(const std::wstring& filename) {
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

        wchar_t* cmd = new wchar_t[command.size() + 1];
        std::copy(command.begin(), command.end(), cmd);
        cmd[command.size()] = L'\0';

        bool success = false;
        int attempts = 0;
        while (!success && attempts < 3) { // Retry up to 3 times
            if (CreateProcessW(NULL, cmd, NULL, NULL, FALSE, CREATE_NEW_PROCESS_GROUP, NULL, pathWithoutQuotes.c_str(), &si, &pi)) {
                std::wcout << L"CreateProcessW succeeded." << std::endl;
                success = true;
            }
            else {
                std::wcout << L"CreateProcessW failed (" << GetLastError() << L"), retrying in 3 seconds..." << std::endl;
                std::this_thread::sleep_for(std::chrono::seconds(3));
                ++attempts;
            }
        }

        if (!success) {
            std::wcerr << L"Failed to create process after multiple attempts." << std::endl;
        }

        // Close process and thread handles if they were opened
        if (success) {
            CloseHandle(pi.hProcess);
            CloseHandle(pi.hThread);
        }

        delete[] cmd;
    }
    else {
        std::wcout << L"No .exe file found in directory: " << directoryPath << std::endl;
    }
}

int APIENTRY wWinMain(_In_ HINSTANCE hInstance, _In_opt_ HINSTANCE hPrevInstance, _In_ LPWSTR lpCmdLine, _In_ int nCmdShow) {
#ifdef DEBUG
    CreateConsole();
#endif
    if (!IsRunningAsAdmin()) {
        PromptForAdminRights();
        return 1; //Exit if no admin perms
    }

    std::wcout << L"Running with elevated privileges." << std::endl;

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
