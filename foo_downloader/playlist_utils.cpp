#include "stdafx.h"
#include "playlist_utils.h"

// Declared in preferences.cpp
extern const char* GetConfigPlaylistName();
extern bool GetConfigAutoPlaylist();

namespace PlaylistUtils {

size_t FindOrCreateDownloadedPlaylist() {
    const char* playlistName = GetConfigPlaylistName();
    if (!playlistName || !*playlistName) playlistName = "Downloaded";

    auto api = playlist_manager::get();

    size_t count = api->get_playlist_count();
    for (size_t i = 0; i < count; i++) {
        pfc::string8 name;
        if (api->playlist_get_name(i, name)) {
            if (pfc::stringCompareCaseInsensitive(name, playlistName) == 0) {
                return i;
            }
        }
    }

    return api->create_playlist(playlistName, ~size_t(0), ~size_t(0));
}

void AddToDownloadedPlaylist(const std::string& filePath) {
    if (filePath.empty()) return;

    // Check if auto-add is enabled
    if (!GetConfigAutoPlaylist()) {
        FB2K_console_formatter() << "[foo_downloader] Auto-add to playlist disabled, skipping: " << filePath.c_str();
        return;
    }

    FB2K_console_formatter() << "[foo_downloader] Adding to playlist: " << filePath.c_str();

    try {
        size_t playlistIdx = FindOrCreateDownloadedPlaylist();

        // Create metadb handle directly — silent, no popup
        auto mdb = metadb::get();
        playable_location_impl loc;
        loc.set_path(filePath.c_str());
        loc.set_subsong(0);

        metadb_handle_ptr handle;
        mdb->handle_create(handle, loc);

        if (handle.is_valid()) {
            metadb_handle_list items;
            items += handle;

            auto api = playlist_manager::get();
            api->playlist_insert_items(playlistIdx, ~size_t(0), items, pfc::bit_array_false());

            const char* pName = GetConfigPlaylistName();
            if (!pName || !*pName) pName = "Downloaded";
            FB2K_console_formatter() << "[foo_downloader] Added to '" << pName << "' playlist.";
        } else {
            FB2K_console_formatter() << "[foo_downloader] Warning: Could not create handle for: " << filePath.c_str();
        }
    } catch (std::exception const& e) {
        FB2K_console_formatter() << "[foo_downloader] Error adding to playlist: " << e.what();
    } catch (...) {
        FB2K_console_formatter() << "[foo_downloader] Error adding to playlist (unknown error)";
    }
}

} // namespace PlaylistUtils
