#include "stdafx.h"
#include "aria2_rpc.h"

#include <tlhelp32.h>

#pragma comment(lib, "winhttp.lib")

// ============================================================================
// Singleton
// ============================================================================

Aria2RpcClient& Aria2RpcClient::instance() {
    static Aria2RpcClient inst;
    return inst;
}

Aria2RpcClient::Aria2RpcClient() {
    // Default aria2 path: aria2c.exe next to the component DLL
    char modulePath[MAX_PATH] = {};
    HMODULE hMod = nullptr;
    GetModuleHandleExA(
        GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
        reinterpret_cast<LPCSTR>(&Aria2RpcClient::instance),
        &hMod);
    if (hMod) {
        GetModuleFileNameA(hMod, modulePath, MAX_PATH);
        std::string path(modulePath);
        auto pos = path.find_last_of("\\/");
        if (pos != std::string::npos) path = path.substr(0, pos);
        m_aria2Path = path + "\\aria2c.exe";
    }

    // Default output directory: user's Music folder
    char musicPath[MAX_PATH] = {};
    if (SUCCEEDED(SHGetFolderPathA(nullptr, CSIDL_MYMUSIC, nullptr, 0, musicPath))) {
        m_outputDir = std::string(musicPath) + "\\foo_downloader";
    } else {
        m_outputDir = "C:\\Music\\foo_downloader";
    }

    // Generate a random secret token
    srand(static_cast<unsigned>(GetTickCount64()));
    m_secret = "fb2k_dl_";
    for (int i = 0; i < 8; i++) {
        m_secret += static_cast<char>('a' + rand() % 26);
    }
}

Aria2RpcClient::~Aria2RpcClient() {
    Stop();
}

// ============================================================================
// Lifecycle
// ============================================================================

bool Aria2RpcClient::Start() {
    std::lock_guard<std::mutex> lock(m_mutex);

    if (m_running) return true;

    // Create output directory if it doesn't exist
    CreateDirectoryA(m_outputDir.c_str(), nullptr);

    return SpawnAria2Process();
}

void Aria2RpcClient::Stop() {
    if (!m_running) return;

    std::lock_guard<std::mutex> lock(m_mutex);

    // Just kill aria2 immediately — no need to wait for graceful shutdown
    if (m_aria2Process) {
        KillAria2Process();
        CloseHandle(m_aria2Process);
        m_aria2Process = nullptr;
    }

    m_running = false;
}

bool Aria2RpcClient::IsRunning() const {
    return m_running;
}

bool Aria2RpcClient::DownloadAria2() {
    FB2K_console_formatter() << "[foo_downloader] Downloading aria2c.exe...";

    // Download aria2 zip from GitHub using URLDownloadToFile (urlmon)
    // We use PowerShell for both download and extraction since it handles
    // HTTPS redirects and zip extraction cleanly.
    std::string destDir = m_aria2Path;
    auto pos = destDir.find_last_of("\\/");
    if (pos != std::string::npos) destDir = destDir.substr(0, pos);

    // PowerShell script: download aria2 release zip, extract aria2c.exe
    std::string psScript =
        "$ErrorActionPreference='Stop'; "
        "$url='https://github.com/aria2/aria2/releases/download/release-1.37.0/aria2-1.37.0-win-64bit-build1.zip'; "
        "$zip=Join-Path $env:TEMP 'aria2.zip'; "
        "$ext=Join-Path $env:TEMP 'aria2_extract'; "
        "[Net.ServicePointManager]::SecurityProtocol=[Net.SecurityProtocolType]::Tls12; "
        "Invoke-WebRequest -Uri $url -OutFile $zip -UseBasicParsing; "
        "if(Test-Path $ext){Remove-Item $ext -Recurse -Force}; "
        "Expand-Archive -Path $zip -DestinationPath $ext -Force; "
        "$exe=Get-ChildItem -Path $ext -Filter aria2c.exe -Recurse | Select-Object -First 1; "
        "Copy-Item $exe.FullName '" + destDir + "\\aria2c.exe' -Force; "
        "Remove-Item $zip -Force; "
        "Remove-Item $ext -Recurse -Force";

    std::string cmdLine = "powershell.exe -NoProfile -ExecutionPolicy Bypass -Command \"" + psScript + "\"";

    STARTUPINFOA si = {};
    si.cb = sizeof(si);
    si.dwFlags = STARTF_USESHOWWINDOW;
    si.wShowWindow = SW_HIDE;

    PROCESS_INFORMATION pi = {};
    std::vector<char> cmdBuf(cmdLine.begin(), cmdLine.end());
    cmdBuf.push_back('\0');

    BOOL ok = CreateProcessA(
        nullptr, cmdBuf.data(),
        nullptr, nullptr, FALSE,
        CREATE_NO_WINDOW, nullptr, nullptr,
        &si, &pi);

    if (!ok) {
        FB2K_console_formatter() << "[foo_downloader] Failed to launch PowerShell for aria2 download.";
        return false;
    }

    // Wait for download to complete (up to 60 seconds)
    DWORD waitResult = WaitForSingleObject(pi.hProcess, 60000);
    DWORD exitCode = 1;
    GetExitCodeProcess(pi.hProcess, &exitCode);
    CloseHandle(pi.hProcess);
    CloseHandle(pi.hThread);

    if (waitResult != WAIT_OBJECT_0 || exitCode != 0) {
        FB2K_console_formatter() << "[foo_downloader] aria2 download failed (exit code " << (uint32_t)exitCode << ").";
        return false;
    }

    // Verify the file now exists
    DWORD attrs = GetFileAttributesA(m_aria2Path.c_str());
    if (attrs == INVALID_FILE_ATTRIBUTES) {
        FB2K_console_formatter() << "[foo_downloader] aria2c.exe still not found after download attempt.";
        return false;
    }

    FB2K_console_formatter() << "[foo_downloader] aria2c.exe downloaded successfully.";
    return true;
}

bool Aria2RpcClient::SpawnAria2Process() {
    // Kill any orphaned aria2c.exe processes from previous sessions.
    // If foobar2000 crashed or was force-killed, the old aria2 process
    // remains on the RPC port with its old secret, causing "Unauthorized"
    // errors for the new instance.
    {
        HANDLE hSnap = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
        if (hSnap != INVALID_HANDLE_VALUE) {
            PROCESSENTRY32 pe = {};
            pe.dwSize = sizeof(pe);
            if (Process32First(hSnap, &pe)) {
                do {
                    if (_wcsicmp(pe.szExeFile, L"aria2c.exe") == 0) {
                        HANDLE hProc = OpenProcess(PROCESS_TERMINATE, FALSE, pe.th32ProcessID);
                        if (hProc) {
                            FB2K_console_formatter() << "[foo_downloader] Killing orphaned aria2c.exe (PID " << (uint32_t)pe.th32ProcessID << ")";
                            TerminateProcess(hProc, 0);
                            CloseHandle(hProc);
                        }
                    }
                } while (Process32Next(hSnap, &pe));
            }
            CloseHandle(hSnap);
            Sleep(200); // Brief wait for port to be released
        }
    }

    // Check if aria2c.exe exists, auto-download if not
    DWORD attrs = GetFileAttributesA(m_aria2Path.c_str());
    if (attrs == INVALID_FILE_ATTRIBUTES) {
        FB2K_console_formatter() << "[foo_downloader] aria2c.exe not found at: " << m_aria2Path.c_str();
        if (!DownloadAria2()) {
            FB2K_console_formatter() << "[foo_downloader] Set the aria2 path manually in Preferences > Tools > Downloader";
            return false;
        }
    }

    // Build command line
    std::string cmdLine = "\"" + m_aria2Path + "\"";
    cmdLine += " --enable-rpc";
    cmdLine += " --rpc-listen-port=" + std::to_string(m_port);
    cmdLine += " --rpc-secret=" + m_secret;
    cmdLine += " --dir=\"" + m_outputDir + "\"";
    cmdLine += " --max-concurrent-downloads=" + std::to_string(m_maxConcurrent);
    cmdLine += " --auto-file-renaming=true";
    cmdLine += " --allow-overwrite=false";
    cmdLine += " --disable-ipv6=true";
    cmdLine += " --check-certificate=false";
    cmdLine += " --console-log-level=warn";
    cmdLine += " --quiet=true";

    STARTUPINFOA si = {};
    si.cb = sizeof(si);
    si.dwFlags = STARTF_USESHOWWINDOW;
    si.wShowWindow = SW_HIDE; // Run hidden

    PROCESS_INFORMATION pi = {};

    // Need a writable buffer for CreateProcess
    std::vector<char> cmdBuf(cmdLine.begin(), cmdLine.end());
    cmdBuf.push_back('\0');

    BOOL ok = CreateProcessA(
        nullptr,
        cmdBuf.data(),
        nullptr, nullptr,
        FALSE,
        CREATE_NO_WINDOW,
        nullptr, nullptr,
        &si, &pi);

    if (!ok) {
        DWORD err = GetLastError();
        FB2K_console_formatter() << "[foo_downloader] Failed to start aria2c.exe (error " << (uint32_t)err << "). Path: " << m_aria2Path.c_str();
        return false;
    }

    m_aria2Process = pi.hProcess;
    CloseHandle(pi.hThread);
    m_running = true;

    // Wait for aria2 to initialize and bind to the RPC port
    Sleep(300);

    return true;
}

void Aria2RpcClient::KillAria2Process() {
    if (m_aria2Process) {
        TerminateProcess(m_aria2Process, 0);
    }
}

// ============================================================================
// Configuration setters
// ============================================================================

void Aria2RpcClient::SetPort(int port) { m_port = port; }
void Aria2RpcClient::SetSecret(const std::string& secret) { m_secret = secret; }
void Aria2RpcClient::SetAria2Path(const std::string& path) { m_aria2Path = path; }
void Aria2RpcClient::SetOutputDir(const std::string& dir) { m_outputDir = dir; }
void Aria2RpcClient::SetMaxConcurrent(int max) { m_maxConcurrent = max; }

// ============================================================================
// Download operations
// ============================================================================

std::string Aria2RpcClient::AddUri(const std::string& url,
                                   const std::map<std::string, std::string>& options,
                                   const std::vector<std::string>& headers) {
    // Build params: ["token:SECRET", ["url"], {options}]
    std::string params = "[\"token:" + m_secret + "\", [\"" + url + "\"]";

    if (!options.empty() || !headers.empty()) {
        params += ", {";
        bool first = true;
        for (const auto& kv : options) {
            if (!first) params += ", ";
            params += "\"" + kv.first + "\": \"" + kv.second + "\"";
            first = false;
        }
        // Headers are passed as a JSON array
        if (!headers.empty()) {
            if (!first) params += ", ";
            params += "\"header\": [";
            for (size_t i = 0; i < headers.size(); i++) {
                if (i > 0) params += ", ";
                params += "\"" + headers[i] + "\"";
            }
            params += "]";
        }
        params += "}";
    }

    params += "]";

    std::string response = RpcCall("aria2.addUri", params);
    if (response.empty()) {
        FB2K_console_formatter() << "[foo_downloader] aria2 RPC: no response (is aria2 running?)";
        return "";
    }
    std::string gid = ExtractJsonResult(response);
    if (gid.empty()) {
        // Check for error in response
        std::string errMsg = ExtractJsonString(response, "message");
        if (!errMsg.empty()) {
            FB2K_console_formatter() << "[foo_downloader] aria2 error: " << errMsg.c_str();
        } else {
            FB2K_console_formatter() << "[foo_downloader] aria2 unexpected response: " << response.substr(0, 500).c_str();
        }
    }
    return gid;
}

Aria2Status Aria2RpcClient::GetStatus(const std::string& gid) {
    Aria2Status status;
    status.gid = gid;

    std::string params = "[\"token:" + m_secret + "\", \"" + gid + "\"]";
    std::string response = RpcCall("aria2.tellStatus", params);

    if (response.empty()) {
        status.status = "error";
        status.errorMessage = "No response from aria2";
        return status;
    }

    // Extract the result object
    std::string result = ExtractJsonResult(response);

    // Extract file paths from "files" array FIRST, then remove it from result
    // so that nested "status" fields inside files[].uris[] don't interfere
    // with extracting the top-level "status" field.
    auto files = ExtractJsonArray(result, "files");
    for (const auto& f : files) {
        std::string path = ExtractJsonString(f, "path");
        if (!path.empty()) {
            status.files.push_back(path);
        }
    }

    // Strip the "files" array from result to avoid nested field collisions
    std::string topLevel = result;
    auto filesPos = topLevel.find("\"files\"");
    if (filesPos != std::string::npos) {
        // Find the opening bracket
        auto bracketPos = topLevel.find('[', filesPos);
        if (bracketPos != std::string::npos) {
            // Find matching closing bracket
            int depth = 1;
            size_t endPos = bracketPos + 1;
            while (endPos < topLevel.size() && depth > 0) {
                if (topLevel[endPos] == '[') depth++;
                else if (topLevel[endPos] == ']') depth--;
                endPos++;
            }
            // Remove the files field (key + array)
            topLevel.erase(filesPos, endPos - filesPos);
        }
    }

    status.status = ExtractJsonString(topLevel, "status");
    status.totalLength = ExtractJsonNumber(topLevel, "totalLength");
    status.completedLength = ExtractJsonNumber(topLevel, "completedLength");
    status.downloadSpeed = ExtractJsonNumber(topLevel, "downloadSpeed");

    if (status.status == "error") {
        status.errorMessage = ExtractJsonString(topLevel, "errorMessage");
    }

    return status;
}

bool Aria2RpcClient::Pause(const std::string& gid) {
    std::string params = "[\"token:" + m_secret + "\", \"" + gid + "\"]";
    std::string response = RpcCall("aria2.pause", params);
    return !response.empty();
}

bool Aria2RpcClient::Unpause(const std::string& gid) {
    std::string params = "[\"token:" + m_secret + "\", \"" + gid + "\"]";
    std::string response = RpcCall("aria2.unpause", params);
    return !response.empty();
}

bool Aria2RpcClient::Remove(const std::string& gid) {
    std::string params = "[\"token:" + m_secret + "\", \"" + gid + "\"]";
    std::string response = RpcCall("aria2.remove", params);
    return !response.empty();
}

// ============================================================================
// JSON-RPC transport
// ============================================================================

std::string Aria2RpcClient::BuildRequest(const std::string& method, const std::string& params) {
    m_requestId++;
    std::string req = "{\"jsonrpc\":\"2.0\",\"id\":\"fb2k_";
    req += std::to_string(m_requestId);
    req += "\",\"method\":\"";
    req += method;
    req += "\",\"params\":";
    req += params;
    req += "}";
    return req;
}

std::string Aria2RpcClient::RpcCall(const std::string& method, const std::string& params) {
    std::string body = BuildRequest(method, params);
    return HttpPost("localhost", m_port, "/jsonrpc", body);
}

std::string Aria2RpcClient::HttpPost(const std::string& host, int port,
                                      const std::string& path, const std::string& body) {
    std::string result;

    HINTERNET hSession = WinHttpOpen(
        L"foo_downloader/0.1",
        WINHTTP_ACCESS_TYPE_NO_PROXY,
        WINHTTP_NO_PROXY_NAME,
        WINHTTP_NO_PROXY_BYPASS,
        0);

    if (!hSession) return result;

    // Convert host to wide string
    std::wstring wHost(host.begin(), host.end());

    HINTERNET hConnect = WinHttpConnect(hSession, wHost.c_str(), static_cast<INTERNET_PORT>(port), 0);
    if (!hConnect) {
        WinHttpCloseHandle(hSession);
        return result;
    }

    std::wstring wPath(path.begin(), path.end());
    HINTERNET hRequest = WinHttpOpenRequest(
        hConnect, L"POST", wPath.c_str(),
        nullptr, WINHTTP_NO_REFERER,
        WINHTTP_DEFAULT_ACCEPT_TYPES,
        0);

    if (!hRequest) {
        WinHttpCloseHandle(hConnect);
        WinHttpCloseHandle(hSession);
        return result;
    }

    // Set content type
    WinHttpAddRequestHeaders(hRequest,
        L"Content-Type: application/json",
        (DWORD)-1,
        WINHTTP_ADDREQ_FLAG_ADD | WINHTTP_ADDREQ_FLAG_REPLACE);

    // Send request
    BOOL ok = WinHttpSendRequest(
        hRequest,
        WINHTTP_NO_ADDITIONAL_HEADERS, 0,
        (LPVOID)body.c_str(), (DWORD)body.size(),
        (DWORD)body.size(),
        0);

    if (ok) {
        ok = WinHttpReceiveResponse(hRequest, nullptr);
    }

    if (ok) {
        DWORD bytesAvailable = 0;
        while (WinHttpQueryDataAvailable(hRequest, &bytesAvailable) && bytesAvailable > 0) {
            std::vector<char> buffer(bytesAvailable);
            DWORD bytesRead = 0;
            if (WinHttpReadData(hRequest, buffer.data(), bytesAvailable, &bytesRead)) {
                result.append(buffer.data(), bytesRead);
            }
        }
    }

    WinHttpCloseHandle(hRequest);
    WinHttpCloseHandle(hConnect);
    WinHttpCloseHandle(hSession);

    return result;
}

// ============================================================================
// Simple JSON parsing helpers
// These are intentionally minimal — sufficient for aria2 RPC responses.
// No external JSON library dependency needed.
// ============================================================================

std::string Aria2RpcClient::ExtractJsonString(const std::string& json, const std::string& key) {
    std::string search = "\"" + key + "\":\"";
    auto pos = json.find(search);
    if (pos == std::string::npos) {
        // Try with space after colon
        search = "\"" + key + "\": \"";
        pos = json.find(search);
        if (pos == std::string::npos) return "";
    }
    pos += search.size();

    // Find closing quote, respecting escaped characters
    std::string result;
    while (pos < json.size()) {
        char c = json[pos];
        if (c == '"') break;
        if (c == '\\' && pos + 1 < json.size()) {
            char next = json[pos + 1];
            if (next == '\\') { result += '\\'; pos += 2; continue; }
            if (next == '"')  { result += '"';  pos += 2; continue; }
            if (next == '/')  { result += '/';  pos += 2; continue; }
            if (next == 'n')  { result += '\n'; pos += 2; continue; }
            if (next == 'r')  { result += '\r'; pos += 2; continue; }
            if (next == 't')  { result += '\t'; pos += 2; continue; }
            // Pass through unknown escapes
            result += next; pos += 2; continue;
        }
        result += c;
        pos++;
    }
    return result;
}

uint64_t Aria2RpcClient::ExtractJsonNumber(const std::string& json, const std::string& key) {
    // aria2 returns numbers as strings: "totalLength":"12345"
    std::string val = ExtractJsonString(json, key);
    if (val.empty()) return 0;
    try {
        return std::stoull(val);
    } catch (...) {
        return 0;
    }
}

std::string Aria2RpcClient::ExtractJsonResult(const std::string& json) {
    // Find "result": and extract everything until matching close
    auto pos = json.find("\"result\":");
    if (pos == std::string::npos) {
        pos = json.find("\"result\" :");
        if (pos == std::string::npos) return "";
    }

    pos = json.find(':', pos) + 1;
    // Skip whitespace
    while (pos < json.size() && (json[pos] == ' ' || json[pos] == '\t')) pos++;

    if (pos >= json.size()) return "";

    // If result is a string (like a GID)
    if (json[pos] == '"') {
        auto end = json.find('"', pos + 1);
        if (end == std::string::npos) return "";
        return json.substr(pos + 1, end - pos - 1);
    }

    // If result is an object or array, find matching close bracket
    if (json[pos] == '{' || json[pos] == '[') {
        char open = json[pos];
        char close = (open == '{') ? '}' : ']';
        int depth = 1;
        size_t start = pos;
        pos++;
        while (pos < json.size() && depth > 0) {
            if (json[pos] == open) depth++;
            else if (json[pos] == close) depth--;
            pos++;
        }
        return json.substr(start, pos - start);
    }

    return "";
}

std::vector<std::string> Aria2RpcClient::ExtractJsonArray(const std::string& json, const std::string& key) {
    std::vector<std::string> result;
    std::string search = "\"" + key + "\":";
    auto pos = json.find(search);
    if (pos == std::string::npos) {
        search = "\"" + key + "\": ";
        pos = json.find(search);
        if (pos == std::string::npos) return result;
    }

    pos = json.find('[', pos);
    if (pos == std::string::npos) return result;

    // Extract each object in the array
    pos++; // skip '['
    while (pos < json.size()) {
        // Skip whitespace and commas
        while (pos < json.size() && (json[pos] == ' ' || json[pos] == ',' || json[pos] == '\n' || json[pos] == '\r')) pos++;

        if (pos >= json.size() || json[pos] == ']') break;

        if (json[pos] == '{') {
            int depth = 1;
            size_t start = pos;
            pos++;
            while (pos < json.size() && depth > 0) {
                if (json[pos] == '{') depth++;
                else if (json[pos] == '}') depth--;
                pos++;
            }
            result.push_back(json.substr(start, pos - start));
        } else {
            pos++;
        }
    }

    return result;
}

// ============================================================================
// Public HTTP GET utility for source providers
// ============================================================================

std::string Aria2RpcClient::HttpGetUrl(const std::string& url) {
    std::string result;

    // Parse URL: scheme://host[:port]/path
    std::string host, path;
    int port = 443;
    bool useSSL = true;

    size_t schemeEnd = url.find("://");
    if (schemeEnd == std::string::npos) return result;

    std::string scheme = url.substr(0, schemeEnd);
    if (scheme == "http") { port = 80; useSSL = false; }

    size_t hostStart = schemeEnd + 3;
    size_t pathStart = url.find('/', hostStart);
    if (pathStart == std::string::npos) {
        host = url.substr(hostStart);
        path = "/";
    } else {
        host = url.substr(hostStart, pathStart - hostStart);
        path = url.substr(pathStart);
    }

    // Check for port in host
    size_t colonPos = host.find(':');
    if (colonPos != std::string::npos) {
        port = std::stoi(host.substr(colonPos + 1));
        host = host.substr(0, colonPos);
    }

    HINTERNET hSession = WinHttpOpen(
        L"Mozilla/5.0 (Windows NT 10.0; Win64; x64) AppleWebKit/537.36",
        WINHTTP_ACCESS_TYPE_NO_PROXY,
        WINHTTP_NO_PROXY_NAME,
        WINHTTP_NO_PROXY_BYPASS,
        0);

    if (!hSession) return result;

    std::wstring wHost(host.begin(), host.end());
    HINTERNET hConnect = WinHttpConnect(hSession, wHost.c_str(),
        static_cast<INTERNET_PORT>(port), 0);
    if (!hConnect) {
        WinHttpCloseHandle(hSession);
        return result;
    }

    std::wstring wPath(path.begin(), path.end());
    DWORD flags = useSSL ? WINHTTP_FLAG_SECURE : 0;
    HINTERNET hRequest = WinHttpOpenRequest(
        hConnect, L"GET", wPath.c_str(),
        nullptr, WINHTTP_NO_REFERER,
        WINHTTP_DEFAULT_ACCEPT_TYPES,
        flags);

    if (!hRequest) {
        WinHttpCloseHandle(hConnect);
        WinHttpCloseHandle(hSession);
        return result;
    }

    // Ignore certificate errors for self-signed certs
    if (useSSL) {
        DWORD secFlags = SECURITY_FLAG_IGNORE_UNKNOWN_CA |
                         SECURITY_FLAG_IGNORE_CERT_DATE_INVALID |
                         SECURITY_FLAG_IGNORE_CERT_CN_INVALID;
        WinHttpSetOption(hRequest, WINHTTP_OPTION_SECURITY_FLAGS, &secFlags, sizeof(secFlags));
    }

    BOOL ok = WinHttpSendRequest(hRequest,
        WINHTTP_NO_ADDITIONAL_HEADERS, 0,
        WINHTTP_NO_REQUEST_DATA, 0, 0, 0);

    if (ok) ok = WinHttpReceiveResponse(hRequest, nullptr);

    if (ok) {
        DWORD bytesAvailable = 0;
        while (WinHttpQueryDataAvailable(hRequest, &bytesAvailable) && bytesAvailable > 0) {
            std::vector<char> buffer(bytesAvailable);
            DWORD bytesRead = 0;
            if (WinHttpReadData(hRequest, buffer.data(), bytesAvailable, &bytesRead)) {
                result.append(buffer.data(), bytesRead);
            }
        }
    }

    WinHttpCloseHandle(hRequest);
    WinHttpCloseHandle(hConnect);
    WinHttpCloseHandle(hSession);

    return result;
}
