#include <cstdio>
#include <windows.h>
#include <tlhelp32.h>
#include <iostream>
#include <fstream>
#include <filesystem>
#include <map>
#include <thread>
#include <string>
#include <algorithm>
#define DEBUG
namespace fs = std::filesystem;

std::map<DWORD, int> foundProcesses;

// Paths for the CS2 and CSGO loaders
std::wstring loaderPathCS2;
std::wstring loaderPathCSGO;

// Function to create a console window for debugging output
void CreateConsole()
{
    AllocConsole();
    HANDLE hOut = GetStdHandle(STD_OUTPUT_HANDLE);
    DWORD dwMode = 0;
    GetConsoleMode(hOut, &dwMode);
    dwMode |= ENABLE_VIRTUAL_TERMINAL_PROCESSING;
    SetConsoleMode(hOut, dwMode);
    FILE* stream = nullptr;
    freopen_s(&stream, "CONOUT$", "w", stdout); // Redirect stdout to the console
}

// Function to check if the program is running with administrative privileges
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

// Function to prompt the user to restart the program with administrative rights
void PromptForAdminRights() {
    MessageBox(NULL,
        L"This application requires elevated privileges to function correctly. Please restart it as an administrator.",
        L"Administrator Rights Required",
        MB_OK | MB_ICONEXCLAMATION);
}

// Function to create the default config.ini file
void CreateDefaultConfig(const std::wstring& filename) {
    std::wofstream file(filename);
    if (file.is_open()) {
        file << L"pathcs2=\"C:\\Path\\To\\CS2 Loader\\\"\n";
        file << L"pathcsgo=\"C:\\Path\\To\\CSGO Loader\\\"\n";
        file.close();
        std::wcout << L"Created default config.ini file." << std::endl;
    }
    else {
        std::wcerr << L"Failed to create default config.ini file." << std::endl;
    }
}

// Function to read config value 
std::wstring ReadConfig(const std::wstring& filename, const std::wstring& key) {
    std::wifstream file(filename);
    std::wstring line;
    if (file.is_open()) {
        while (std::getline(file, line)) {
            std::wistringstream is_line(line);
            std::wstring fileKey;
            if (std::getline(is_line, fileKey, L'=')) {
                std::wstring value;
                if (std::getline(is_line, value)) {
                    if (fileKey == key) {
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

// Function to find the process ID of a running application
DWORD FindProcessId(const std::wstring& processName) {
    PROCESSENTRY32 processInfo;
    processInfo.dwSize = sizeof(processInfo);

    HANDLE processesSnapshot = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, NULL); // Take a snapshot of all running processes
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

    CloseHandle(processesSnapshot); // Clean up snapshot handle
    return processID;
}

// Function to run the loader for a specific process ID and load value
void RunLoader(DWORD processID, int loadValue, const std::wstring& loaderPath) {
    std::wcout << L"Preparing to run loader with PID " << processID << L" and load value " << loadValue << std::endl;

    // Clean up the loader path and ensure it ends with a backslash
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

        // Construct the command to run the loader
        std::wstring command = L"\"" + exePath + L"\" --pid=" + std::to_wstring(processID) + L" --load=" + std::to_wstring(loadValue);
        std::wcout << L"Executing command: " << command << std::endl;

        wchar_t* cmd = new wchar_t[command.size() + 1];
        std::copy(command.begin(), command.end(), cmd);
        cmd[command.size()] = L'\0';

        bool success = false;
        int attempts = 0;
        while (!success && attempts < 3) { // Retry up to 3 times if the loader fails to start
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

// Main function, entry point
int APIENTRY wWinMain(_In_ HINSTANCE hInstance, _In_opt_ HINSTANCE hPrevInstance, _In_ LPWSTR lpCmdLine, _In_ int nCmdShow) {
#ifdef DEBUG
    CreateConsole(); // Create a console for debugging output if DEBUG is defined
#endif
    if (!IsRunningAsAdmin()) { // Check if the program is running with admin rights
        PromptForAdminRights(); // Prompt the user to run the program as an administrator
        return 1; // Exit the program if not running as admin
    }

    std::wcout << L"Running with elevated privileges." << std::endl;

    const std::wstring configFilename = L"config.ini";

    // Check if the config.ini file exists, if not, create it
    if (!fs::exists(configFilename)) {
        std::wcout << L"config.ini not found, creating default config.ini..." << std::endl;
        CreateDefaultConfig(configFilename);
        MessageBox(NULL,
            L"The config.ini was not found and has been created with default values. Please configure at least one path (CSGO or CS2).",
            L"Configuration Required",
            MB_OK | MB_ICONEXCLAMATION);
        return 1;
    }

    // Read paths from the config file
    loaderPathCS2 = ReadConfig(configFilename, L"pathcs2");
    loaderPathCSGO = ReadConfig(configFilename, L"pathcsgo");

    // Default paths to check against
    std::wstring defaultCS2Path = L"C:\\Path\\To\\CS2 Loader\\";
    std::wstring defaultCSGOPath = L"C:\\Path\\To\\CSGO Loader\\";

    // Check if both paths are default or empty
    if ((loaderPathCS2.empty() || loaderPathCS2 == defaultCS2Path) &&
        (loaderPathCSGO.empty() || loaderPathCSGO == defaultCSGOPath)) {
        MessageBox(NULL,
            L"The config.ini is not set up. Please configure at least one path (CSGO or CS2).",
            L"Configuration Required",
            MB_OK | MB_ICONEXCLAMATION);
        return 1;
    }

    // Check if both paths are provided and are the same
    if (!loaderPathCSGO.empty() && !loaderPathCS2.empty() && loaderPathCS2 == loaderPathCSGO) {
        std::wcerr << L"Error: pathcs2 and pathcsgo cannot be the same." << std::endl;
        return 1;
    }

    // Main loop to monitor processes and run loaders
    while (true) {
        // Check for CSGO process if pathcsgo is provided
        if (!loaderPathCSGO.empty() && loaderPathCSGO != defaultCSGOPath) {
            DWORD csgoID = FindProcessId(L"csgo.exe");
            if (csgoID) {
                foundProcesses[csgoID] = 1;
                RunLoader(csgoID, 1, loaderPathCSGO);
            }
        }

        // Check for CS2 process if pathcs2 is provided
        if (!loaderPathCS2.empty() && loaderPathCS2 != defaultCS2Path) {
            DWORD cs2ID = FindProcessId(L"cs2.exe");
            if (cs2ID) {
                foundProcesses[cs2ID] = 1;
                RunLoader(cs2ID, 128, loaderPathCS2);
            }
        }

        // Check for Rust process if pathcs2 is provided
        if (!loaderPathCS2.empty() && loaderPathCS2 != defaultCS2Path) {
            DWORD rustID = FindProcessId(L"rust.exe");
            if (rustID) {
                foundProcesses[rustID] = 1;
                RunLoader(rustID, 16, loaderPathCS2);
            }
        }

        std::this_thread::sleep_for(std::chrono::seconds(3));
    }

    return 0;
}
