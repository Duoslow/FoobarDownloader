#pragma once

#include "../source_provider.h"
#include <string>
#include <vector>

struct CustomSearchResult {
    std::string id;
    std::string title;
    std::string artist;
    std::string album;
    int duration = 0;
};

class CustomSource : public ISourceProvider {
public:
    const char* GetId() const override { return "custom_source"; }
    const char* GetName() const override { return "Custom Source"; }
    const char* GetDescription() const override { return "Search and download audio from a custom source"; }
    const char* GetActionLabel() const override { return "Search"; }

    bool Resolve(const char* input, std::vector<DownloadItem>& items, std::string& errorMsg) override;

private:
    std::vector<CustomSearchResult> Search(const std::string& query, std::string& errorMsg);
    bool ShowSelectionDialog(const std::vector<CustomSearchResult>& results, std::vector<int>& selectedIndices);
    static std::string UrlEncode(const std::string& str);
    static std::string ExtractJsonString(const std::string& json, const std::string& key);
    static int ExtractJsonInt(const std::string& json, const std::string& key);
    static std::string ExtractNestedString(const std::string& json, const std::string& object, const std::string& key);
    static std::vector<std::string> ExtractJsonArray(const std::string& json, const std::string& key);
};
