#include <jni.h>
#include <string>
#include <zygisk.hpp>
#include <unistd.h>
#include <android/log.h>
#include <fcntl.h>
#include <cstring>
#include <fstream>
#include <unordered_set>
#include <sys/stat.h>

#define LOG_TAG "CPUGUARD"
#define LOGD(...) __android_log_print(ANDROID_LOG_DEBUG, LOG_TAG, __VA_ARGS__)
#define LOGE(...) __android_log_print(ANDROID_LOG_ERROR, LOG_TAG, __VA_ARGS__)

static const char* config_path = "/data/adb/modules/COPG_CPU/apps.json";
static const char* spoof_file_path = "/data/adb/modules/COPG_CPU/cpuinfo_spoof";

class ConfigManager {
private:
    std::unordered_set<std::string> blacklist_packages;
    std::unordered_set<std::string> gamelist_packages;
    time_t last_mtime = 0;

public:
    bool loadConfig() {
        struct stat st;
        if (stat(config_path, &st) != 0) {
            LOGE("Config file not found: %s", config_path);
            return false;
        }
        
        if (st.st_mtime == last_mtime) {
            return true;
        }
        
        std::ifstream file(config_path);
        if (!file.is_open()) {
            LOGE("Failed to open config file: %s", config_path);
            return false;
        }
        
        try {
            std::string content((std::istreambuf_iterator<char>(file)), 
                               std::istreambuf_iterator<char>());
            file.close();
            
            blacklist_packages.clear();
            gamelist_packages.clear();
            
            size_t blacklist_start = content.find("\"blacklist\"");
            if (blacklist_start != std::string::npos) {
                size_t array_start = content.find('[', blacklist_start);
                size_t array_end = content.find(']', array_start);
                
                if (array_start != std::string::npos && array_end != std::string::npos) {
                    std::string blacklist_section = content.substr(array_start, array_end - array_start + 1);
                    parsePackageList(blacklist_section, blacklist_packages);
                }
            }
            
            size_t gamelist_start = content.find("\"gamelist\"");
            if (gamelist_start != std::string::npos) {
                size_t array_start = content.find('[', gamelist_start);
                size_t array_end = content.find(']', array_start);
                
                if (array_start != std::string::npos && array_end != std::string::npos) {
                    std::string gamelist_section = content.substr(array_start, array_end - array_start + 1);
                    parsePackageList(gamelist_section, gamelist_packages);
                }
            }
            
            last_mtime = st.st_mtime;
            LOGD("Config loaded: %zu blacklist, %zu gamelist packages", 
                 blacklist_packages.size(), gamelist_packages.size());
            return true;
            
        } catch (const std::exception& e) {
            LOGE("Error parsing config: %s", e.what());
            return false;
        }
    }
    
    bool isTargetApp(const char* package_name, bool* is_blacklist, bool* is_gamelist) {
        std::string pkg_str(package_name);
        
        *is_blacklist = (blacklist_packages.find(pkg_str) != blacklist_packages.end());
        *is_gamelist = (gamelist_packages.find(pkg_str) != gamelist_packages.end());
        
        return *is_blacklist || *is_gamelist;
    }

private:
    void parsePackageList(const std::string& section, std::unordered_set<std::string>& packages) {
        size_t pos = 0;
        while ((pos = section.find("\"package_name\"", pos)) != std::string::npos) {
            size_t colon_pos = section.find(':', pos);
            size_t quote_start = section.find('"', colon_pos);
            size_t quote_end = section.find('"', quote_start + 1);
            
            if (colon_pos != std::string::npos && quote_start != std::string::npos && quote_end != std::string::npos) {
                std::string package = section.substr(quote_start + 1, quote_end - quote_start - 1);
                packages.insert(package);
                LOGD("Loaded package: %s", package.c_str());
            }
            
            pos = quote_end + 1;
        }
    }
};

static ConfigManager config_manager;

static void companion(int fd) {
    LOGD("[COMPANION] Started");
    
    char buffer[256];
    ssize_t bytes = read(fd, buffer, sizeof(buffer)-1);
    
    if (bytes > 0) {
        buffer[bytes] = '\0';
        
        LOGD("[COMPANION] Command: %s", buffer);
        
        int result = -1;
        
        if (strcmp(buffer, "unmount_spoof") == 0) {
            result = system("/system/bin/umount /proc/cpuinfo 2>/dev/null");
            if (result == 0) {
                LOGD("[COMPANION] Unmount successful");
            } else {
                LOGD("[COMPANION] Unmount failed or not mounted");
            }
            
        } else if (strcmp(buffer, "mount_spoof") == 0) {
            if (access(spoof_file_path, F_OK) == 0) {
                system("/system/bin/umount /proc/cpuinfo 2>/dev/null");
                
                char mount_cmd[512];
                snprintf(mount_cmd, sizeof(mount_cmd), 
                        "/system/bin/mount --bind %s /proc/cpuinfo", spoof_file_path);
                
                result = system(mount_cmd);
                if (result == 0) {
                    LOGD("[COMPANION] Mount successful");
                } else {
                    LOGE("[COMPANION] Mount failed");
                }
            } else {
                LOGE("[COMPANION] Spoof file not found: %s", spoof_file_path);
            }
        }
        
        write(fd, &result, sizeof(result));
    }
    
    close(fd);
    LOGD("[COMPANION] Finished");
}

class CPUGUARD : public zygisk::ModuleBase {
public:
    void onLoad(zygisk::Api* api, JNIEnv* env) override {
        this->api = api;
        this->env = env;
        LOGD("Module loaded");
        
        config_manager.loadConfig();
    }

    void preAppSpecialize(zygisk::AppSpecializeArgs* args) override {
        if (!args || !args->nice_name) {
            api->setOption(zygisk::DLCLOSE_MODULE_LIBRARY);
            return;
        }
        
        const char* package_name = env->GetStringUTFChars(args->nice_name, nullptr);
        if (!package_name) {
            api->setOption(zygisk::DLCLOSE_MODULE_LIBRARY);
            return;
        }
        
        LOGD("Processing package: %s", package_name);
        
        config_manager.loadConfig();
        
        bool is_blacklist = false;
        bool is_gamelist = false;
        
        if (config_manager.isTargetApp(package_name, &is_blacklist, &is_gamelist)) {
            if (is_blacklist) {
                LOGD("Blacklisted app - unmounting spoof: %s", package_name);
                executeCompanionCommand("unmount_spoof");
            } else if (is_gamelist) {
                LOGD("Gamelisted app - mounting spoof: %s", package_name);
                executeCompanionCommand("mount_spoof");
            }
        } else {
            LOGD("Package not in lists: %s", package_name);
        }
        
        env->ReleaseStringUTFChars(args->nice_name, package_name);
        api->setOption(zygisk::DLCLOSE_MODULE_LIBRARY);
    }

private:
    zygisk::Api* api;
    JNIEnv* env;
    
    bool executeCompanionCommand(const char* command) {
        int fd = api->connectCompanion();
        if (fd < 0) {
            LOGE("Failed to connect to companion");
            return false;
        }
        
        write(fd, command, strlen(command) + 1);
        
        int result = -1;
        read(fd, &result, sizeof(result));
        close(fd);
        
        LOGD("Companion result: %d", result);
        return result == 0;
    }
};

REGISTER_ZYGISK_MODULE(CPUGUARD)
REGISTER_ZYGISK_COMPANION(companion)
