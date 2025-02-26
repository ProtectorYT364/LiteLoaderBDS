﻿#include "pch.h"
using std::vector;
Logger<stdio_commit> LOG(stdio_commit{"[LL] "});

static void printErrorMessage() {
    DWORD error_message_id = ::GetLastError();
    if (error_message_id == 0) {
        std::wcerr << "Error\n";
        return;
    }
    std::cerr << "[Error] error_message_id: " << error_message_id << std::endl;
    LPWSTR message_buffer = nullptr;
    FormatMessage(
        FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_IGNORE_INSERTS | FORMAT_MESSAGE_FROM_SYSTEM,
        NULL, error_message_id, MAKELANGID(0x09, SUBLANG_DEFAULT), (LPWSTR)&message_buffer, 0, NULL);
    std::wcerr << "[Error] " << message_buffer;
    LocalFree(message_buffer);
}

static void fixPluginsLibDir() {  // add plugins folder to path to fix dependent problem
    WCHAR *buffer = new WCHAR[8192];
    auto sz       = GetEnvironmentVariableW(TEXT("PATH"), buffer, 8192);
    std::wstring PATH{buffer, sz};
    sz = GetCurrentDirectoryW(8192, buffer);
    std::wstring CWD{buffer, sz};
    SetEnvironmentVariableW(TEXT("PATH"), (CWD + L"\\plugins;" + PATH).c_str());
    delete[] buffer;
}

static vector<std::wstring> getPreloadList() {
    //若在preload.conf中，则不加载
    vector<std::wstring> preload_list{};

    if (std::filesystem::exists(std::filesystem::path(TEXT(".\\plugins\\preload.conf")))) {
        std::wifstream dllList(TEXT(".\\plugins\\preload.conf"));
        if (dllList) {
            std::wstring dllName;
            while (getline(dllList, dllName)) {
                if (dllName.back() == TEXT('\n'))
                    dllName.pop_back();
                if (dllName.back() == TEXT('\r'))
                    dllName.pop_back();

                if (dllName.empty() || dllName.front() == TEXT('#'))
                    continue;
                preload_list.push_back(dllName);
            }
            dllList.close();
        }
    }
    return preload_list;
}
static std::vector<std::pair<std::wstring, HMODULE>> libs;
LIAPI std::vector<std::pair<std::wstring, HMODULE>> liteloader::getAllLibs() {
    return libs;
}
static void loadPlugins() {
    fixPluginsLibDir();
    std::filesystem::create_directory("plugins");
    std::filesystem::directory_iterator ent("plugins");
    short plugins                    = 0;
    vector<std::wstring> preload_list = getPreloadList();

    LOG("Loading plugins");
    for (auto &i : ent) {
        if (i.is_regular_file() && i.path().extension().u8string() == ".dll") {
            bool loaded = false;
            for (auto &p : preload_list)
                if (p.find(std::wstring(i.path())) != std::wstring::npos) {
                    loaded = true;
                    break;
                }
            if (loaded)
                continue;
            auto lib = LoadLibrary(i.path().c_str());
            if (lib) {
                plugins++;
                LOG("Plugin " + canonical(i.path()).filename().u8string() + " loaded");
                libs.push_back({std::wstring{i.path().c_str()}, lib});
            } else {
                LOG("Error when loading " + i.path().filename().u8string() + "");
                printErrorMessage();
            }
        }
    }
    for (auto &[name, h] : libs) {
        auto fn = GetProcAddress(h, "onPostInit");
        if (!fn) {
            // std::wcerr << "Warning!!! mod" << name << " doesnt have a onPostInit\n";
        } else {
            try {
                ((void (*)())fn)();
            } catch (...) {
                std::wcerr << "[Error] plugin " << name << " throws an exception when onPostInit\n";
                std::this_thread::sleep_for(std::chrono::seconds(10));
                exit(1);
            }
        }
    }
    // libs.clear();
    LOG(std::to_string(plugins) + " plugin(s) loaded");
}

vector<function<void(PostInitEV)>> Post_init_call_backs;
LIAPI void Event::addEventListener(function<void(PostInitEV)> callback) {
    Post_init_call_backs.push_back(callback);
}

void FixUpCWD() {
    string buf;
    buf.assign(8192, '\0');
    GetModuleFileNameA(nullptr, buf.data(), 8192);
    buf = buf.substr(0, buf.find_last_of('\\'));
    SetCurrentDirectoryA(buf.c_str());
}

void startWBThread();
void checkUpdate();
void registerCommands();

static void entry(bool fix_cwd) {
    //Prohibit pop-up windows to facilitate automatic restart
    SetErrorMode(SEM_FAILCRITICALERRORS | SEM_NOGPFAULTERRORBOX | SEM_NOALIGNMENTFAULTEXCEPT);
    if (fix_cwd) {
        FixUpCWD();
    }
    loadPlugins();
    XIDREG::initAll();  // Initialize the xuid database
    registerCommands(); // Register built-in commands
    Event::addEventListener([](ServerStartedEV) {  // Server started event
        startWBThread();
        LOG("LiteLoader is distributed under the GPLv3 License");
        LOG(u8"感谢旋律云(rhymc.com)对本项目的支持 | Thanks to [rhymc.com] for supporting this "
            u8"project");
        checkUpdate();
    });

    PostInitEV post_init_ev;  // Register PostInit event
    for (size_t count = 0; count < Post_init_call_backs.size(); count++) {
        Post_init_call_backs[count](post_init_ev);
    }
}

THook(int, "main", int a, void *b) {
    std::ios::sync_with_stdio(false);
    // system("chcp 65001");
    entry(a > 1);
    return original(a, b);
}