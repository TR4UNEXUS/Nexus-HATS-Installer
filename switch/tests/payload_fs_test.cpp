// Runs the real payload/source/fs.c against an in-memory FatFS implementation.
#include <libs/fatfs/ff.h>
extern "C" {
#include "../payload/source/fs.h"
#include "../payload/source/boot_config.h"
}
#include "../shared/install_protection.h"
#include <algorithm>
#include <cassert>
#include <cstring>
#include <iostream>
#include <map>
#include <string>
#include <vector>

struct Node { bool directory = false; std::string data; };
struct FileHandle { std::string path; size_t offset = 0; };
struct DirHandle { std::vector<std::string> children; size_t index = 0; };
static std::map<std::string, Node> nodes;
static std::vector<std::string> traversed, modified;
static std::string fail_stat;
static bool fail_sync=false;

static std::string path(const char* value) {
    std::string s = mm_sd_relative_path(value);
    for (char& c : s) c = c == '\\' ? '/' : mm_path_lower(c);
    while (!s.empty() && s.back() == '/') s.pop_back();
    return s;
}
static std::string parent(const std::string& s) {
    auto pos = s.find_last_of('/');
    return pos == std::string::npos ? "" : s.substr(0, pos);
}
static void mkdirs(const std::string& s) {
    if (nodes.contains(s)) return;
    if (!s.empty()) mkdirs(parent(s));
    nodes[s] = {true, {}};
}
static void put(const std::string& s, const std::string& data) {
    mkdirs(parent(s));
    nodes[s] = {false, data};
}
extern "C" {
int f_open(FIL* file, const char* name, u8 mode) {
    auto s = path(name);
    if (mode & FA_CREATE_ALWAYS) {
        if (!nodes.contains(parent(s))) return FR_NO_PATH;
        nodes[s] = {false, {}};
        modified.push_back(s);
    }
    if (!nodes.contains(s) || nodes[s].directory) return FR_NO_FILE;
    file->handle = new FileHandle{s};
    return FR_OK;
}
int f_close(FIL* file) { delete static_cast<FileHandle*>(file->handle); return FR_OK; }
u64 f_size(FIL* file) { return nodes.at(static_cast<FileHandle*>(file->handle)->path).data.size(); }
int f_read(FIL* file, void* buffer, UINT length, UINT* read) {
    auto* h = static_cast<FileHandle*>(file->handle);
    auto& data = nodes.at(h->path).data;
    *read = static_cast<UINT>(std::min<size_t>(length, data.size() - h->offset));
    std::memcpy(buffer, data.data() + h->offset, *read);
    h->offset += *read;
    return FR_OK;
}
int f_write(FIL* file, const void* buffer, UINT length, UINT* written) {
    auto* h = static_cast<FileHandle*>(file->handle);
    auto& data = nodes.at(h->path).data;
    data.resize(std::max(data.size(), h->offset + length));
    std::memcpy(data.data() + h->offset, buffer, length);
    h->offset += length;
    *written = length;
    return FR_OK;
}
int f_sync(FIL*) { return fail_sync ? FR_DISK_ERR : FR_OK; }
int f_stat(const char* name, FILINFO* info) {
    const auto s = path(name);
    if (s == fail_stat) return FR_DISK_ERR;
    const auto it = nodes.find(s);
    if (it == nodes.end()) return FR_NO_FILE;
    info->fattrib = it->second.directory ? AM_DIR : 0;
    return FR_OK;
}
int f_opendir(DIR* dir, const char* name) {
    auto s = path(name);
    if (!nodes.contains(s) || !nodes[s].directory) return FR_NO_PATH;
    traversed.push_back(s);
    auto* h = new DirHandle;
    for (const auto& [p, node] : nodes)
        if (p != s && parent(p) == s) h->children.push_back(p);
    dir->handle = h;
    return FR_OK;
}
int f_readdir(DIR* dir, FILINFO* info) {
    auto* h = static_cast<DirHandle*>(dir->handle);
    if (h->index == h->children.size()) { info->fname[0] = 0; return FR_OK; }
    const auto& s = h->children[h->index++];
    std::strcpy(info->fname, s.substr(s.find_last_of('/') + 1).c_str());
    info->fattrib = nodes.at(s).directory ? AM_DIR : 0;
    return FR_OK;
}
int f_closedir(DIR* dir) { delete static_cast<DirHandle*>(dir->handle); return FR_OK; }
int f_unlink(const char* name) {
    auto s = path(name);
    if (!nodes.contains(s)) return FR_NO_FILE;
    for (const auto& [p, node] : nodes) if (p != s && parent(p) == s) return FR_DENIED;
    nodes.erase(s);
    modified.push_back(s);
    return FR_OK;
}
int f_mkdir(const char* name) {
    auto s = path(name);
    if (nodes.contains(s)) return FR_EXIST;
    if (!nodes.contains(parent(s))) return FR_NO_PATH;
    nodes[s] = {true, {}};
    modified.push_back(s);
    return FR_OK;
}
int f_chmod(const char* name, u8, u8) { modified.push_back(path(name)); return FR_OK; }
}

int main() {
    const std::string gta = "atmosphere/contents/0100b00b51230000";
    const std::string game = "atmosphere/contents/0100123412340000";
    const std::string module = "atmosphere/contents/010000000000bd00";
    const std::string staging = "config/nexus-hats-installer/hats-staging/";
    put("atmosphere/package3", "old core");
    put("atmosphere/config_templates/old.ini", "obsolete");
    put(module + "/exefs.nsp", "old module");
    put(module + "/old.bin", "obsolete");
    put(gta + "/romfs/huge.dat", "GTA data");
    put(game + "/cheats/123.txt", "my cheats");
    put(game + "/romfs/mod.dat", "my mod");
    put("atmosphere/contents/user-notes.txt", "keep");
    put(staging + "atmosphere/package3", "new core");
    put(staging + module + "/exefs.nsp", "new module");
    put(staging + gta + "/romfs/huge.dat", "must not replace");
    put(staging + gta + "/romfs/added.dat", "must not add");
    put(staging + game + "/cheats/123.txt", "must not replace");
    assert(load_contents_policy("sd:/missing.txt") == FR_NO_FILE);
    put("config/nexus-hats-installer/policy.txt", MM_CONTENTS_PROTECTION_MARKER "\nEND\n");
    assert(load_contents_policy("sd:/config/nexus-hats-installer/policy.txt") == FR_OK);
    assert(folder_delete("sd:/atmosphere") == FR_OK);
    assert(!nodes.contains("atmosphere/package3"));
    assert(!nodes.contains(module));
    assert(nodes.at(gta + "/romfs/huge.dat").data == "GTA data");
    assert(folder_copy(("sd:/" + staging + "atmosphere").c_str(), "sd:/") == FR_OK);
    assert(nodes.at("atmosphere/package3").data == "new core");
    assert(nodes.at(module + "/exefs.nsp").data == "new module");
    assert(!nodes.contains(module + "/old.bin"));
    assert(nodes.at(gta + "/romfs/huge.dat").data == "GTA data");
    assert(!nodes.contains(gta + "/romfs/added.dat"));
    assert(nodes.at(game + "/cheats/123.txt").data == "my cheats");
    assert(nodes.at(game + "/romfs/mod.dat").data == "my mod");
    assert(nodes.at("atmosphere/contents/user-notes.txt").data == "keep");
    for (const auto& p : traversed) assert(!mm_is_protected_contents(nullptr, p.c_str()));
    for (const auto& p : modified) assert(!mm_is_protected_contents(nullptr, p.c_str()));

    // Overwrite updates modules without deleting their extra files; mods stay put.
    put(module + "/old.bin", "retain in overwrite mode");
    put(staging + module + "/exefs.nsp", "newer module");
    assert(folder_copy(("sd:/" + staging + "atmosphere").c_str(), "sd:/") == FR_OK);
    assert(nodes.at(module + "/exefs.nsp").data == "newer module");
    assert(nodes.contains(module + "/old.bin"));
    assert(nodes.at(gta + "/romfs/huge.dat").data == "GTA data");

    // Failed destination checks cannot silently overwrite protected data.
    fail_stat = gta + "/romfs/huge.dat";
    assert(file_copy((staging + "atmosphere/package3").c_str(), fail_stat.c_str()) == FR_DISK_ERR);
    fail_stat.clear();

    // Manifest-granted modules are updateable; a corrupt policy fails closed.
    const std::string extra = "atmosphere/contents/4200000000001234";
    put(extra + "/exefs.nsp", "old extra");
    assert(folder_delete(extra.c_str()) == FR_OK && nodes.contains(extra));
    put("config/nexus-hats-installer/policy.txt", MM_CONTENTS_PROTECTION_MARKER "\nS 4200000000001234\nEND\n");
    assert(load_contents_policy("sd:/config/nexus-hats-installer/policy.txt") == FR_OK);
    assert(folder_delete(extra.c_str()) == FR_OK && !nodes.contains(extra));
    put("config/nexus-hats-installer/policy.txt", MM_CONTENTS_PROTECTION_MARKER "\nS 0100b00b51230000\nEND\n");
    assert(load_contents_policy("sd:/config/nexus-hats-installer/policy.txt") == FR_INVALID_PARAMETER);
    assert(folder_delete(gta.c_str()) == FR_OK && nodes.contains(gta));

    // Clean mode removes only recognised Switch utility files, including in
    // folders shared with ports, game NROs, saves and updater ZIP caches.
    put("switch/daybreak.nro", "old daybreak");
    put("switch/dbi_ru/dbi.nro", "old utility");
    put("switch/dbi_ru/dbi.config", "old config");
    put("switch/dbi_ru/ports/gta/game.dat", "nested port");
    put("switch/dbi_ru/saves/player.sav", "save");
    put("switch/dbi_ru/mygame.nro", "nested game");
    put("switch/gta5/gta5.nro", "port executable");
    put("switch/gta5/data/huge.dat", "port assets");
    put("switch/homebrew-game.nro", "standalone game");
    put("switch/nexus-hats-installer/cache/hats/pack.zip", "large cache");
    put("switch/.overlays/ovlmenu.ovl", "old overlay");
    put("switch/.overlays/custom.ovl", "custom overlay");
    put(staging + "switch/daybreak.nro", "new daybreak");
    put(staging + "switch/dbi_ru/dbi.nro", "new utility");
    put(staging + "switch/dbi_ru/dbi.config", "new config");
    put(staging + "switch/dbi_ru/ports/gta/game.dat", "must not replace");
    put(staging + "switch/dbi_ru/ports/gta/new.dat", "must not add");
    put(staging + "switch/dbi_ru/mygame.nro", "must not replace");
    put(staging + "switch/gta5/data/huge.dat", "must not replace");
    put(staging + "switch/gta5/data/new.dat", "must not add");
    put(staging + "switch/homebrew-game.nro", "must not replace");
    put(staging + "switch/.overlays/ovlmenu.ovl", "new overlay");
    put(staging + "switch/.overlays/custom.ovl", "must not replace");
    traversed.clear();
    modified.clear();
    assert(folder_delete("sd:/switch") == FR_OK);
    assert(nodes.contains("switch") && nodes.contains("switch/dbi_ru"));
    assert(!nodes.contains("switch/daybreak.nro"));
    assert(!nodes.contains("switch/dbi_ru/dbi.nro"));
    assert(!nodes.contains("switch/.overlays/ovlmenu.ovl"));
    for (const auto& p : traversed) assert(mm_is_managed_switch_path(p.c_str(), true));
    for (const auto& p : modified) assert(mm_is_managed_switch_path(p.c_str(), false));
    assert(folder_copy((staging + "switch").c_str(), "sd:/") == FR_OK);
    assert(nodes.at("switch/daybreak.nro").data == "new daybreak");
    assert(nodes.at("switch/dbi_ru/dbi.nro").data == "new utility");
    assert(nodes.at("switch/.overlays/ovlmenu.ovl").data == "new overlay");
    assert(nodes.at("switch/gta5/data/huge.dat").data == "port assets");
    assert(nodes.at("switch/gta5/gta5.nro").data == "port executable");
    assert(nodes.at("switch/dbi_ru/ports/gta/game.dat").data == "nested port");
    assert(nodes.at("switch/dbi_ru/saves/player.sav").data == "save");
    assert(nodes.at("switch/dbi_ru/mygame.nro").data == "nested game");
    assert(nodes.at("switch/homebrew-game.nro").data == "standalone game");
    assert(nodes.at("switch/.overlays/custom.ovl").data == "custom overlay");
    assert(nodes.at("switch/nexus-hats-installer/cache/hats/pack.zip").data == "large cache");
    assert(!nodes.contains("switch/gta5/data/new.dat"));
    assert(!nodes.contains("switch/dbi_ru/ports/gta/new.dat"));
    for (const auto& p : traversed) assert(!mm_is_protected_install_path(nullptr, p.c_str(), true));
    for (const auto& p : modified) assert(!mm_is_protected_install_path(nullptr, p.c_str(), nodes.contains(p) && nodes.at(p).directory));

    // Overwrite uses the same protection, without a cleanup first.
    put(staging + "switch/daybreak.nro", "newest daybreak");
    assert(folder_copy((staging + "switch").c_str(), "sd:/") == FR_OK);
    assert(nodes.at("switch/daybreak.nro").data == "newest daybreak");
    assert(nodes.at("switch/homebrew-game.nro").data == "standalone game");
    assert(folder_delete("sd:/switch/daybreak.nro") == FR_OK);
    assert(!nodes.contains("switch/daybreak.nro"));

    // Older HATS packs must not downgrade the running app or its repair files.
    for (const auto path : {"switch/nexus-hats-installer/hats-installer.bin", "switch/nexus-hats-installer/nexus-hats-installer.nro",
        "config/nexus-hats-installer/background.rgba", "config/nexus-hats-installer/config.ini"}) {
        put(path, "current installer file");
        put(staging + path, "old pack file");
    }
    assert(folder_delete("sd:/config") == FR_OK);
    assert(folder_copy((staging + "config").c_str(), "sd:/") == FR_OK);
    assert(folder_copy((staging + "switch").c_str(), "sd:/") == FR_OK);
    for (const auto path : {"switch/nexus-hats-installer/hats-installer.bin", "switch/nexus-hats-installer/nexus-hats-installer.nro",
        "config/nexus-hats-installer/background.rgba", "config/nexus-hats-installer/config.ini"})
        assert(nodes.at(path).data == "current installer file");

    // Core replacement and config cleanup must preserve the live USB library
    // and Manager settings, even when a downloaded pack includes stale copies.
    for (const auto* name : {"atmosphere/usb_content/index.bin", "atmosphere/usb_content/badges/game.dat",
             "config/nexusmanager/network.ini", "config/nexus-manager/settings.ini"}) {
        put(name, "current user data");
        put(staging + name, "pack must not replace this");
    }
    assert(folder_delete("sd:/atmosphere") == FR_OK);
    assert(folder_delete("sd:/config") == FR_OK);
    assert(folder_copy((staging + "atmosphere").c_str(), "sd:/") == FR_OK);
    assert(folder_copy((staging + "config").c_str(), "sd:/") == FR_OK);
    for (const auto* name : {"atmosphere/usb_content/index.bin", "atmosphere/usb_content/badges/game.dat",
             "config/nexusmanager/network.ini", "config/nexus-manager/settings.ini"})
        assert(nodes.at(name).data == "current user data");

    // Staging cleanup is unrestricted, including its copies of protected titles.
    assert(folder_delete(("sd:/" + staging).c_str()) == FR_OK);
    assert(!nodes.contains(staging + gta));
    assert(!nodes.contains(staging + "switch"));
    assert(nodes.contains("switch/gta5/data/huge.dat"));
    assert(nodes.contains(gta));

    // A fresh card can still receive a previously absent title from a pack.
    nodes.clear();
    put(staging + gta + "/romfs/huge.dat", "first install");
    assert(folder_copy(("sd:/" + staging + "atmosphere").c_str(), "sd:/") == FR_OK);
    assert(nodes.at(gta + "/romfs/huge.dat").data == "first install");
    put(staging + "switch/daybreak.nro", "fresh utility");
    put(staging + "switch/new-port/game.nro", "fresh port");
    assert(folder_copy((staging + "switch").c_str(), "sd:/") == FR_OK);
    assert(nodes.at("switch/daybreak.nro").data == "fresh utility");
    assert(nodes.at("switch/new-port/game.nro").data == "fresh port");
    // GitHub exclusions apply outside atmosphere/switch, override recognised
    // utilities/sysmodules, skip absent paths too, and keep parent directories.
    put("config/nexus-hats-installer/policy.txt", MM_CONTENTS_PROTECTION_MARKER "\n"
        "P fallout1\nP saltysd/exceptions.txt\nP bootloader/personal\n"
        "P switch/daybreak.nro\nP atmosphere/contents/010000000000bd00\nEND\n");
    assert(load_contents_policy("config/nexus-hats-installer/policy.txt") == FR_OK);
    put("fallout1/save.dat", "root save");
    put("saltysd/exceptions.txt", "custom exceptions");
    put("saltysd/old.bin", "obsolete");
    put("bootloader/personal/config.ini", "custom boot config");
    put("bootloader/update.bin", "old bootloader");
    put(module + "/exefs.nsp", "preserved module");
    put(staging + "fallout1/save.dat", "do not replace");
    put(staging + "fallout1/new.dat", "do not add");
    put(staging + "saltysd/exceptions.txt", "do not replace");
    put(staging + "saltysd/new.bin", "new file");
    put(staging + "bootloader/personal/config.ini", "do not replace");
    put(staging + "bootloader/update.bin", "new bootloader");
    put(staging + module + "/exefs.nsp", "do not replace");
    traversed.clear();
    modified.clear();
    assert(folder_delete("fallout1") == FR_OK);
    assert(folder_delete("saltysd") == FR_OK);
    assert(folder_delete("bootloader") == FR_OK);
    assert(folder_delete("switch/daybreak.nro") == FR_OK);
    assert(folder_delete(module.c_str()) == FR_OK);
    assert(nodes.contains("saltysd") && !nodes.contains("saltysd/old.bin"));
    assert(nodes.contains("bootloader") && !nodes.contains("bootloader/update.bin"));
    for (const auto& name : {"fallout1", "saltysd", "bootloader", "atmosphere", "switch"})
        assert(folder_copy((staging + name).c_str(), "sd:/") == FR_OK);
    assert(nodes.at("fallout1/save.dat").data == "root save");
    assert(!nodes.contains("fallout1/new.dat"));
    assert(nodes.at("saltysd/exceptions.txt").data == "custom exceptions");
    assert(nodes.at("saltysd/new.bin").data == "new file");
    assert(nodes.at("bootloader/personal/config.ini").data == "custom boot config");
    assert(nodes.at("bootloader/update.bin").data == "new bootloader");
    assert(nodes.at("switch/daybreak.nro").data == "fresh utility");
    assert(nodes.at(module + "/exefs.nsp").data == "preserved module");
    for (const auto& p : traversed) {
        assert(p != "fallout1" && p != staging + "fallout1");
        assert(p != "bootloader/personal" && p != staging + "bootloader/personal");
    }
    nodes.erase("saltysd/exceptions.txt");
    assert(file_copy((staging + "saltysd/exceptions.txt").c_str(), "saltysd/exceptions.txt") == FR_OK);
    assert(!nodes.contains("saltysd/exceptions.txt"));

    // A changed remote list replaces the previous snapshot; removed custom
    // exclusions no longer linger and new ones take effect on the next load.
    put("config/nexus-hats-installer/policy.txt", MM_CONTENTS_PROTECTION_MARKER "\nP saltysd/new.bin\nEND\n");
    assert(load_contents_policy("config/nexus-hats-installer/policy.txt") == FR_OK);
    assert(folder_copy((staging + "fallout1").c_str(), "sd:/") == FR_OK);
    assert(nodes.at("fallout1/save.dat").data == "do not replace");
    assert(folder_delete("saltysd/new.bin") == FR_OK && nodes.contains("saltysd/new.bin"));
    put(staging + "config/nexus-hats-installer/managed-sysmodules.txt", MM_CONTENTS_PROTECTION_MARKER "\nEND\n");
    assert(file_copy((staging + "config/nexus-hats-installer/managed-sysmodules.txt").c_str(), "sd:" MM_CONTENTS_POLICY_PATH) == FR_OK);
    assert(!nodes.contains("config/nexus-hats-installer/managed-sysmodules.txt"));
    put("config/nexus-hats-installer/policy.txt", MM_CONTENTS_PROTECTION_MARKER "\nP fallout1\n");
    assert(load_contents_policy("config/nexus-hats-installer/policy.txt") == FR_INVALID_PARAMETER);
    assert(folder_delete("saltysd/new.bin") == FR_OK && nodes.contains("saltysd/new.bin"));
    // Exercise the operations used by all four install modes with no remote
    // exclusions. Live maps and repair state must survive; foreign maps must
    // never be installed, even if the target folder does not exist yet.
    const std::vector<std::string> library = {
        "atmosphere/usb_content/title_map.bin", "atmosphere/usb_content/title_content_map.bin",
        "atmosphere/usb_content/content_locations.bin", "atmosphere/usb_content/title_map.bin.bak",
        "atmosphere/usb_content/title_content_map.bin.tmp", "atmosphere/usb_content/future/nested.bin",
        "atmosphere/usb_badges/0100000000010000-0.ubg", "atmosphere/usb_badges/disabled",
        "atmosphere/usb_content-repair.pending", "atmosphere/usb_content-repair-0123456789abcdef/journal.bin",
        "atmosphere/usb_content-repair-0123456789abcdef/before/title_map.bin",
        "atmosphere/usb_content-recovery-0123456789abcdef/quarantined.bin"
    };
    for (int mode = 0; mode < 4; ++mode) {
        nodes.clear();
        put("config/nexus-hats-installer/policy.txt", MM_CONTENTS_PROTECTION_MARKER "\nEND\n");
        assert(load_contents_policy("config/nexus-hats-installer/policy.txt") == FR_OK);
        for (const auto& p : library) { put(p, "live USB data"); put(staging + p, "foreign stale data"); }
        put("atmosphere/package3", "old core"); put(staging + "atmosphere/package3", "new core");
        put("bootloader/update.bin", "old bootloader"); put("switch/daybreak.nro", "old utility");
        const std::string absent = "atmosphere/usb_content-recovery-ffffffffffffffff/new.bin";
        put(staging + absent, "foreign recovery");
        traversed.clear(); modified.clear();
        if (mode >= 1) assert(folder_delete("sd:/atmosphere") == FR_OK);
        if (mode >= 2) assert(folder_delete("sd:/bootloader") == FR_OK);
        if (mode >= 3) assert(folder_delete("sd:/switch") == FR_OK);
        assert(folder_copy((staging + "atmosphere").c_str(), "sd:/") == FR_OK);
        for (const auto& p : library) assert(nodes.at(p).data == "live USB data");
        assert(!nodes.contains(absent));
        for (const auto& p : modified) for (const auto& protected_file : library) assert(p != protected_file);
        assert(nodes.at("atmosphere/package3").data == "new core");
        assert(folder_delete(("sd:/" + staging).c_str()) == FR_OK);
        for (const auto& p : library) assert(nodes.at(p).data == "live USB data");
    }
    nodes.clear();
    put("config/nexus-hats-installer/policy.txt", MM_CONTENTS_PROTECTION_MARKER "\nEND\n");
    assert(load_contents_policy("config/nexus-hats-installer/policy.txt") == FR_OK);
    put(staging + "atmosphere/package3", "new core");
    for (const auto& p : library) put(staging + p, "foreign library");
    assert(folder_copy((staging + "atmosphere").c_str(), "sd:/") == FR_OK);
    for (const auto& p : library) {
        assert(!nodes.contains(p));
        assert(file_copy((staging + p).c_str(), p.c_str()) == FR_OK);
        assert(!nodes.contains(p));
    }
    assert(nodes.at("atmosphere/package3").data == "new core");
    const std::string ini="bootloader/hekate_ipl.ini";
    const std::string backup="config/nexus-hats-installer/hekate_ipl.ini.bak";
    const std::string old_ini="[config]\r\nautoboot=2\r\nbootwait=7\r\n# keep my settings\r\n"
        "[My emu]\r\npkg3=atmosphere/package3\r\nkip1=atmosphere/kips/nexus-loader.kip\r\nkip1=atmosphere/kips/hoc.kip\r\nemummcforce=1\r\nicon=bootloader/res/icon_payload.bmp\r\n"
        "[My sys]\r\nfss0=atmosphere/package3\r\nemummc_force_disable=1\r\nicon=old.bmp\r\n"
        "[Stock]\r\npkg3=atmosphere/package3\r\nstock=1\r\nemummc_force_disable=1\r\nicon=stock.bmp\r\n"
        "[Other payload]\r\npayload=bootloader/payloads/tool.bin\r\nicon=custom.bmp\r\n";
    const std::string pack_ini="[CFW (emuMMC)]\npkg3=atmosphere/package3\nemummcforce=1\nicon=bootloader/res/icon_payload_emu.bmp\n"
        "[CFW (sysMMC)]\npkg3=atmosphere/package3\nemummc_force_disable=1\nicon=bootloader/res/atmosphere.bmp\n";
    auto expected=old_ini;
    auto replace=[&](const std::string& from,const std::string& to) { expected.replace(expected.find(from),from.size(),to); };
    replace("icon=bootloader/res/icon_payload.bmp","icon=bootloader/res/icon_payload_emu.bmp");
    replace("icon=old.bmp","icon=bootloader/res/atmosphere.bmp");
    const std::string bmp="BM"+std::string(64,'\0');
    for (int mode=0;mode<4;++mode) {
        nodes.clear();
        put("config/nexus-hats-installer/policy.txt",MM_CONTENTS_PROTECTION_MARKER "\nEND\n");
        assert(load_contents_policy("config/nexus-hats-installer/policy.txt")==FR_OK);
        put(backup,old_ini);put(ini,"temporary installer autoboot");
        put(staging+ini,pack_ini);
        for (const auto& name:{"bootloader/res/icon_payload_emu.bmp","bootloader/res/atmosphere.bmp"}) put(staging+name,bmp);
        if (mode>=2) assert(folder_delete("sd:/bootloader")==FR_OK);
        assert(folder_copy((staging+"bootloader").c_str(),"sd:/")==FR_OK);
        assert(folder_delete(("sd:/"+staging).c_str())==FR_OK);
        unsigned count=0;
        assert(restore_hekate_config(true,&count)==FR_OK && count==2);
        assert(nodes.at(ini).data==expected && !nodes.contains(backup));
        // Repeating the same upgrade is idempotent.
        put(backup,expected);put(ini,pack_ini);
        assert(restore_hekate_config(true,&count)==FR_OK && count==0);
        assert(nodes.at(ini).data==expected);
        // Failure/cancellation must restore the old configuration exactly.
        put(backup,old_ini);put(ini,pack_ini);
        assert(restore_hekate_config(false,&count)==FR_OK && count==0);
        assert(nodes.at(ini).data==old_ini);
        // Standard packs do not apply themed icons.
        put(backup,old_ini);put(ini,"[Standard]\npkg3=atmosphere/package3\nemummcforce=1\nicon=standard.bmp\n");
        assert(restore_hekate_config(true,&count)==FR_OK && count==0);
        assert(nodes.at(ini).data==old_ini);
        // A failed flush retains the original recovery copy for the next launch.
        put(backup,old_ini);put(ini,pack_ini);fail_sync=true;
        assert(restore_hekate_config(true,&count)==FR_DISK_ERR);
        assert(nodes.at(backup).data==old_ini);fail_sync=false;
        assert(restore_hekate_config(false,&count)==FR_OK && nodes.at(ini).data==old_ini);
        // No original config means the newly installed pack stays in place.
        put(ini,pack_ini);assert(restore_hekate_config(true,&count)==FR_NO_FILE);
        assert(nodes.at(ini).data==pack_ini);
    }
    unsigned count=0;
    nodes.clear();put(backup,old_ini);put(ini,pack_ini);
    assert(restore_hekate_config(true,&count)==FR_OK && count==0); // missing artwork
    assert(nodes.at(ini).data==old_ini);
    put(backup,"");assert(restore_hekate_config(false,&count)==FR_INVALID_PARAMETER);
    assert(nodes.contains(backup));nodes.erase(backup);
    put("config/nexus-hats-installer/legacy-hekate_ipl.ini.bak",old_ini);
    assert(restore_hekate_config(false,&count)==FR_OK && nodes.at(ini).data==old_ini);
    std::cout << "Themed Launch icons, all install modes, user settings, standard packs and recovery checks passed.\n";
    std::cout << "Payload copy/delete protection and GitHub exclusion tests passed (all four USB-library install modes).\n";
}
