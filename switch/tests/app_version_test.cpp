#include "hats_release.hpp"
#include <cassert>
#include <cstring>
#include <iostream>
using namespace sphaira::ui::menu::hats;

int main(int argc, char** argv) {
    for (const auto* bad : {"", "1", "1.0", "1.0.1.2", "1.0.-1", "1.0.x", "1.0.01",
            "1.0.1-preview", "1.0.1 ", "999999999999999999.0.1", "1..1"})
        assert(!ParseAppVersion(bad));
    assert(ParseAppVersion("v1.0.1") == ParseAppVersion("1.0.1"));
    assert(ParseAppVersion("V1.0.1") == ParseAppVersion("1.0.1"));
    for (const auto* extension : {".nsp", ".zip", ".NSP", ".ZIP"}) {
        ReleaseEntry entry;
        entry.tag_name = "v1.0.1";
        entry.asset_name = std::string("Nexus-HATS-Installer-1.0.1") + extension;
        // A fresh manual install has no receipt. Neither app format should offer
        // itself again or downgrade a locally installed newer build.
        assert(!IsNewerAppRelease(entry, "1.0.1"));
        assert(!IsNewerAppRelease(entry, "1.0.2"));
        assert(IsNewerAppRelease(entry, "1.0.0"));
        entry.release_id = 123; entry.asset_id = 456; entry.asset_updated_at = "later";
        entry.asset_digest = "changed-upload"; entry.name = "New release announcement";
        assert(!IsNewerAppRelease(entry, "1.0.1"));
        entry.asset_name = std::string("Nexus-HATS-Installer-1.0.10") + extension;
        entry.tag_name = "v1.0.10";
        assert(IsNewerAppRelease(entry, "1.0.9"));
        assert(!IsNewerAppRelease(entry, "1.1.0"));
        entry.tag_name = "library-protection"; // Filename still declares the version.
        assert(IsNewerAppRelease(entry, "1.0.9"));
        entry.tag_name = "v2.0.0"; // Conflicting release and asset identities.
        assert(!IsNewerAppRelease(entry, "1.0.9"));
        entry.tag_name = "v1.0.10"; entry.prerelease = true;
        assert(!IsNewerAppRelease(entry, "1.0.9"));
        entry.prerelease = false;
        entry.asset_name = std::string("Nexus-HATS-Installer") + extension;
        assert(IsNewerAppRelease(entry, "1.0.9"));
        entry.tag_name = "unknown";
        assert(!IsNewerAppRelease(entry, "1.0.9"));
        entry.asset_name = std::string("Nexus-HATS-Installer-1.0.10-preview") + extension;
        entry.tag_name = "v1.0.10";
        assert(!IsNewerAppRelease(entry, "1.0.9"));
    }
    ReleaseEntry pack;
    pack.name = "An extremely long release announcement that must not appear in the list";
    pack.tag_name = "v1.12.2-lr1";
    for (const auto& [file, display] : {
        std::pair{"Nexus-1.12.2-LR1-Standard-No-HOC.zip", "1.12.2 LR1 - Standard No-HOC"},
        std::pair{"Nexus-1.12.2-LR1-Standard-HOC.zip", "1.12.2 LR1 - Standard HOC"},
        std::pair{"Nexus-1.12.2-LR1-Nexus-Themed-HOC.zip", "1.12.2 LR1 - Nexus-themed HOC"},
        std::pair{"Nexus-1.12.2-N4R2-No-HOC.ZIP", "1.12.2 N4R2 - No-HOC"},
        std::pair{"Future-Resource-Pack.zip", "Future Resource Pack"}}) {
        pack.asset_name = file;
        assert(PackDisplayName(pack) == display);
        assert(pack.asset_name == file); // Display never changes the download/cache path.
    }
    if (argc > 1) {
        auto* doc = yyjson_read_file(argv[1], 0, nullptr, nullptr); assert(doc);
        for (bool nsp : {false, true}) {
            const auto entries = ParseAppReleases(yyjson_doc_get_root(doc), nsp);
            assert(entries.size() == 1);
            assert(IsNewerAppRelease(entries[0], "1.0.0"));
            assert(!IsNewerAppRelease(entries[0], "1.0.1"));
            assert(!IsNewerAppRelease(entries[0], "1.0.2"));
        }
        yyjson_doc_free(doc);
    }
    if (argc > 2) {
        auto* doc = yyjson_read_file(argv[2], 0, nullptr, nullptr); assert(doc);
        const auto entries = ParsePackReleases(yyjson_doc_get_root(doc));
        assert(entries.size() == 3);
        for (const auto& entry : entries) {
            const auto label = PackDisplayName(entry);
            assert(label.starts_with("1.12.2 LR1 - ") && label.size() < 45);
            std::cout << label << " | " << entry.size << " bytes\n";
        }
        yyjson_doc_free(doc);
    }
    std::cout << "PASS: same/older versions suppressed for NRO and NSP, newer versions detected, concise pack identities.\n";
}
