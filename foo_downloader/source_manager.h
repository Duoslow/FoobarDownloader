#pragma once

#include "source_provider.h"
#include <vector>
#include <memory>
#include <string>

// ============================================================================
// Source manager — registry of all source providers
// ============================================================================
class SourceManager {
public:
    static SourceManager& instance();

    // Register a new source provider
    void Register(std::unique_ptr<ISourceProvider> provider);

    // Get all registered providers (unfiltered)
    const std::vector<std::unique_ptr<ISourceProvider>>& GetAll() const;

    // Get enabled providers only
    std::vector<ISourceProvider*> GetEnabled() const;

    // Find a provider by its ID
    ISourceProvider* GetById(const std::string& id) const;

    // Check if a provider is enabled
    bool IsEnabled(const std::string& id) const;

    // Get provider count
    size_t GetCount() const;

private:
    SourceManager();
    ~SourceManager() = default;
    SourceManager(const SourceManager&) = delete;
    SourceManager& operator=(const SourceManager&) = delete;

    std::vector<std::unique_ptr<ISourceProvider>> m_providers;
};
