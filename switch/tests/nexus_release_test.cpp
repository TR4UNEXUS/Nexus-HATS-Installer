#include "hats_release.hpp"
#include "hats_contents_policy.hpp"
#include "../shared/install_protection.h"
#include <cassert>
#include <cstring>
#include <iostream>
using namespace sphaira::ui::menu::hats;

int main(int argc, char** argv) {
    const char* json = R"({"id":17,"tag_name":"new-name","assets":[
      {"name":"Nexus-1.12.2-N1-HOC.zip","browser_download_url":"https://example.org/hoc.zip"},
      {"name":"Nexus-1.12.2-N1-No-HOC.zip","browser_download_url":"https://example.org/no-hoc.zip"},
      {"name":"Nexus-USB-HATS.Full.zip","browser_download_url":"https://example.org/full.zip"},
      {"name":"Future-Resource-Pack.ZIP","browser_download_url":"https://example.org/new.zip","size":456},
      {"name":"Nexus-USB-Formatter-Windows.zip","browser_download_url":"https://example.org/formatter.zip"},
      {"name":"Nexus-HATS-Installer-1.0.1.zip","browser_download_url":"https://example.org/app.zip"},
      {"name":"Nexus-HATS-Installer-1.0.1.nsp","browser_download_url":"https://example.org/app.nsp"},
      {"name":"Nexus-Manager-0.2.4.nsp","browser_download_url":"https://example.org/manager.nsp"},
      {"name":"Nexus-source.zip","browser_download_url":"https://example.org/src.zip"},
      {"name":"Guide.txt","browser_download_url":"https://example.org/guide.txt"}
    ]})";
    auto* doc = yyjson_read(json, std::strlen(json), 0);
    assert(doc);
    auto* root = yyjson_doc_get_root(doc);
    const auto packs = ParsePackReleases(root);
    assert(packs.size() == 4);
    assert(packs.back().asset_name == "Future-Resource-Pack.ZIP");
    assert(packs.back().size == 456);
    const auto nro = ParseAppReleases(root, false), nsp = ParseAppReleases(root, true);
    assert(nro.size() == 1 && nro[0].asset_name == "Nexus-HATS-Installer-1.0.1.zip");
    assert(nsp.size() == 1 && nsp[0].asset_name == "Nexus-HATS-Installer-1.0.1.nsp");
    yyjson_doc_free(doc);
    assert(UseBundledExclusions(404));
    for (long status : {0, 200, 401, 403, 429, 500, 503}) assert(!UseBundledExclusions(status));
    mm_contents_policy empty{};
    for (const auto* path : {"sd:/ATMOSPHERE/USB_CONTENT", "atmosphere/usb_content/index.bin",
         "config/nexusmanager/network.ini", "config/nexus-manager/settings.ini"}) {
        assert(mm_is_protected_install_path(&empty, path, false));
        assert(mm_is_protected_install_path(&empty, path, true));
    }
    assert(mm_keep_install_parent(&empty, "config"));
    assert(mm_keep_install_parent(&empty, "atmosphere"));
    assert(!mm_is_protected_install_path(&empty, "atmosphere/usb_content-old/index.bin", false));
    assert(!mm_is_protected_install_path(&empty, "switch/nexusmanager/NexusManager.nro", false));
    assert(!mm_is_protected_install_path(&empty, "switch/.overlays/usb-content-status.ovl", false));
    if (argc > 1) {
        doc = yyjson_read_file(argv[1], 0, nullptr, nullptr);
        assert(doc);
        const auto live = ParsePackReleases(yyjson_doc_get_root(doc));
        assert(!live.empty());
        for (const auto& pack : live) {
            assert(pack.download_url.starts_with("https://github.com/TR4UNEXUS/Nexus-USB-HATS/releases/download/"));
            assert(pack.size > 0);
            std::cout << "Live pack: " << pack.asset_name << '\n';
        }
        yyjson_doc_free(doc);
    }
    std::cout << "Nexus release and protection checks passed\n";
}
