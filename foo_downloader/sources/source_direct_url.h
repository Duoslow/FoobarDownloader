#pragma once

#include "../source_provider.h"
#include <string>
#include <vector>

// ============================================================================
// Direct URL source — passes URLs through to aria2 as-is
// ============================================================================
// This is the simplest built-in source provider.
// It takes a URL from the user and downloads it directly via aria2.
// No API calls, no URL transformation.
// ============================================================================

class DirectUrlSource : public ISourceProvider {
public:
    const char* GetId() const override { return "direct_url"; }
    const char* GetName() const override { return "Direct URL"; }
    const char* GetDescription() const override { return "Download a file directly from a URL"; }

    bool Resolve(const char* input, std::vector<DownloadItem>& items, std::string& errorMsg) override {
        if (!input || !*input) {
            errorMsg = "URL cannot be empty.";
            return false;
        }

        std::string url(input);

        // Basic URL validation
        if (url.find("://") == std::string::npos) {
            // Try prepending https:// if no scheme
            if (url.find("http") != 0) {
                url = "https://" + url;
            }
        }

        // Extract filename from URL
        std::string filename;
        auto lastSlash = url.find_last_of('/');
        if (lastSlash != std::string::npos && lastSlash + 1 < url.size()) {
            filename = url.substr(lastSlash + 1);
            // Strip query string
            auto queryPos = filename.find('?');
            if (queryPos != std::string::npos) {
                filename = filename.substr(0, queryPos);
            }
            // Strip fragment
            auto fragPos = filename.find('#');
            if (fragPos != std::string::npos) {
                filename = filename.substr(0, fragPos);
            }
        }

        DownloadItem item;
        item.url = url;
        item.filename = filename;
        item.title = filename.empty() ? url : filename;
        items.push_back(std::move(item));

        return true;
    }
};
