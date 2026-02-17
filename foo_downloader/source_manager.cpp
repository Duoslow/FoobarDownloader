#include "stdafx.h"
#include "source_manager.h"
#include "sources/source_direct_url.h"
#include "sources/source_custom.h"
#include "sources/source_youtube.h"

// Declared in preferences.cpp
extern bool GetConfigEnableCustomSource();
extern bool GetConfigEnableYoutube();
extern bool GetConfigEnableDirectUrl();

SourceManager& SourceManager::instance() {
    static SourceManager inst;
    return inst;
}

SourceManager::SourceManager() {
    // Custom Source
    Register(std::make_unique<CustomSource>());

    // YouTube source
    Register(std::make_unique<YouTubeSource>());

    // Direct URL download
    Register(std::make_unique<DirectUrlSource>());
}

void SourceManager::Register(std::unique_ptr<ISourceProvider> provider) {
    if (provider) {
        m_providers.push_back(std::move(provider));
    }
}

const std::vector<std::unique_ptr<ISourceProvider>>& SourceManager::GetAll() const {
    return m_providers;
}

std::vector<ISourceProvider*> SourceManager::GetEnabled() const {
    std::vector<ISourceProvider*> result;
    for (const auto& p : m_providers) {
        if (IsEnabled(p->GetId())) {
            result.push_back(p.get());
        }
    }
    return result;
}

ISourceProvider* SourceManager::GetById(const std::string& id) const {
    for (const auto& p : m_providers) {
        if (id == p->GetId()) return p.get();
    }
    return nullptr;
}

bool SourceManager::IsEnabled(const std::string& id) const {
    if (id == "custom_source") return GetConfigEnableCustomSource();
    if (id == "youtube") return GetConfigEnableYoutube();
    if (id == "direct_url") return GetConfigEnableDirectUrl();
    return true; // Unknown sources are enabled by default
}

size_t SourceManager::GetCount() const {
    return m_providers.size();
}
