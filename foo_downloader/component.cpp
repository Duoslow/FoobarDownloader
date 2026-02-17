#include "stdafx.h"
#include "guids.h"
#include "aria2_rpc.h"
#include "source_manager.h"
#include "download_manager.h"

DECLARE_COMPONENT_VERSION(
    "Downloader",
    "1.0.0",
    "foo_downloader \xe2\x80\x94 foobar2000 Download Manager\n"
    "Downloads audio files from configurable sources using aria2.\n\n"
    "Features:\n"
    "- Plugin-style source provider architecture\n"
    "- aria2-powered download engine\n"
    "- Automatic playlist integration\n"
    "- Configurable via Preferences > Tools > Downloader"
);

VALIDATE_COMPONENT_FILENAME("foo_downloader.dll");

FOOBAR2000_IMPLEMENT_CFG_VAR_DOWNGRADE;

namespace {

class DownloaderInitQuit : public initquit {
public:
    void on_init() override {
        FB2K_console_formatter() << "[foo_downloader] Initializing...";

        SourceManager::instance();

        auto& aria2 = Aria2RpcClient::instance();
        if (aria2.Start()) {
            FB2K_console_formatter() << "[foo_downloader] aria2 daemon started.";
        } else {
            FB2K_console_formatter() << "[foo_downloader] WARNING: Failed to start aria2. Set the path in Preferences > Tools > Downloader.";
        }

        DownloadManager::instance();
        FB2K_console_formatter() << "[foo_downloader] Ready.";
    }

    void on_quit() override {
        FB2K_console_formatter() << "[foo_downloader] Shutting down...";
        DownloadManager::instance().Shutdown();
        Aria2RpcClient::instance().Stop();
        FB2K_console_formatter() << "[foo_downloader] Shutdown complete.";
    }
};

FB2K_SERVICE_FACTORY(DownloaderInitQuit);

} // namespace
