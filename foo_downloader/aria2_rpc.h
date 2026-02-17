#pragma once

#include <string>
#include <vector>
#include <map>
#include <mutex>
#include <atomic>
#include <cstdint>

struct Aria2Status {
    std::string gid;
    std::string status;      // "active", "waiting", "paused", "error", "complete", "removed"
    uint64_t totalLength = 0;
    uint64_t completedLength = 0;
    uint64_t downloadSpeed = 0;
    std::string errorMessage;
    std::vector<std::string> files;

    double GetProgress() const {
        if (totalLength == 0) return 0.0;
        return static_cast<double>(completedLength) / static_cast<double>(totalLength) * 100.0;
    }

    bool IsComplete() const { return status == "complete"; }
    bool IsError() const { return status == "error"; }
    bool IsActive() const { return status == "active" || status == "waiting"; }
};

class Aria2RpcClient {
public:
    static Aria2RpcClient& instance();

    bool Start();
    void Stop();
    bool IsRunning() const;

    std::string AddUri(const std::string& url,
                       const std::map<std::string, std::string>& options = {},
                       const std::vector<std::string>& headers = {});
    Aria2Status GetStatus(const std::string& gid);
    bool Pause(const std::string& gid);
    bool Unpause(const std::string& gid);
    bool Remove(const std::string& gid);

    void SetPort(int port);
    void SetSecret(const std::string& secret);
    void SetAria2Path(const std::string& path);
    void SetOutputDir(const std::string& dir);
    void SetMaxConcurrent(int max);

private:
    Aria2RpcClient();
    ~Aria2RpcClient();
    Aria2RpcClient(const Aria2RpcClient&) = delete;
    Aria2RpcClient& operator=(const Aria2RpcClient&) = delete;

    std::string RpcCall(const std::string& method, const std::string& params);
    std::string BuildRequest(const std::string& method, const std::string& params);
    std::string ExtractJsonString(const std::string& json, const std::string& key);
    uint64_t ExtractJsonNumber(const std::string& json, const std::string& key);
    std::string ExtractJsonResult(const std::string& json);
    std::vector<std::string> ExtractJsonArray(const std::string& json, const std::string& key);
    std::string HttpPost(const std::string& host, int port, const std::string& path, const std::string& body);

    bool SpawnAria2Process();

public:
    // Public HTTP utility for use by source providers
    static std::string HttpGetUrl(const std::string& url);

private:
    void KillAria2Process();
    bool DownloadAria2();

    HANDLE m_aria2Process = nullptr;
    int m_port = 6800;
    std::string m_secret;
    std::string m_aria2Path;
    std::string m_outputDir;
    int m_maxConcurrent = 3;
    std::atomic<bool> m_running{ false };
    std::mutex m_mutex;
    int m_requestId = 0;
};
