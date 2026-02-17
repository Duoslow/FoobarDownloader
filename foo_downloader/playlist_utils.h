#pragma once

#include <string>

namespace PlaylistUtils {
    void AddToDownloadedPlaylist(const std::string& filePath);
    size_t FindOrCreateDownloadedPlaylist();
}
