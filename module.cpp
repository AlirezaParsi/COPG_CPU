#include <jni.h>
#include <string>
#include <zygisk.hpp>
#include <nlohmann/json.hpp>
#include <fstream>
#include <unordered_set>
#include <unistd.h>
#include <android/log.h>
#include <sys/stat.h>
#include <sys/mount.h>

using json = nlohmann::json;

#define LOG_TAG "CPUGUARD"
#define LOGD(...) __android_log_print(ANDROID_LOG_DEBUG, LOG_TAG, __VA_ARGS__)
#define LOGE(...) __android_log_print(ANDROID_LOG_ERROR, LOG_TAG, __VA_ARGS__)

struct AppInfo {
    std::string package_name;
    std::string app_name;
};

struct ModuleConfig {
    std::vector<AppInfo> blacklist;
    std::vector<AppInfo> gamelist;
    std::unordered_set<std::string> blacklist_packages;
    std::unordered_set<std::string> gamelist_packages;
};

static const std::string config_path = "/data/adb/modules/COPG_CPU/apps.json";
static const std::string spoof_file_path = "/data/adb/modules/COPG_CPU/cpuinfo_spoof";
static bool spoof_active = false;

class ConfigManager {
private:
    ModuleConfig config;
    time_t last_mtime = 0;

public:
    bool loadConfig() {
        struct stat st;
        if (stat(config_path.c_str(), &st) != 0) {
            LOGE("Config file not found");
            return false;
        }
        
        if (st.st_mtime == last_mtime) {
            return true;
        }
        
        std::ifstream file(config_path);
        if (!file.is_open()) {
            LOGE("Failed to open config file");
            return false;
        }
        
        try {
            json j = json::parse(file);
            
            config.blacklist = j["blacklist"].get<std::vector<AppInfo>>();
            config.gamelist = j["gamelist"].get<std::vector<AppInfo>>();
            
            config.blacklist_packages.clear();
            config.gamelist_packages.clear();
            
            for (const auto& app : config.blacklist) {
                config.blacklist_packages.insert(app.package_name);
            }
            
            for (const auto& app : config.gamelist) {
                config.gamelist_packages.insert(app.package_name);
            }
            
            last_mtime = st.st_mtime;
            LOGD("Config loaded: %zu blacklist, %zu gamelist", 
                 config.blacklist.size(), config.gamelist.size());
            return true;
            
        } catch (const std::exception& e) {
            LOGE("JSON error: %s", e.what());
            return false;
        }
    }
    
    bool isBlacklisted(const std::string& pkg) const {
        return config.blacklist_packages.count(pkg) > 0;
    }
    
    bool isGamelisted(const std::string& pkg) const {
        return config.gamelist_packages.count(pkg) > 0;
    }
};

static ConfigManager config_manager;

static void companion(int fd) {
    LOGD("[COMPANION] Started");
    
    char buffer[256];
    ssize_t bytes = read(fd, buffer, sizeof(buffer)-1);
    
    if (bytes > 0) {
        buffer[bytes] = '\0';
        std::string command = buffer;
        
        LOGD("[COMPANION] Received command: %s", command.c_str());
        
        int result = -1;
        
        if (command == "mount_spoof") {
            if (access(spoof_file_path.c_str(), F_OK) == 0) {
                chmod(spoof_file_path.c_str(), 0444);
                chown(spoof_file_path.c_str(), 0, 0);
                system("chcon u:object_r:system_file:s0 /data/adb/modules/COPG_CPU/cpuinfo_spoof");
                
                result = mount(spoof_file_path.c_str(), "/proc/cpuinfo", nullptr, MS_BIND, nullptr);
                if (result == 0) {
                    spoof_active = true;
                    LOGD("[COMPANION] Mount successful with proper permissions");
                } else {
                    LOGE("[COMPANION] Mount failed: %s", strerror(errno));
                }
            } else {
                LOGE("[COMPANION] Spoof file not found: %s", spoof_file_path.c_str());
            }
            
        } else if (command == "unmount_spoof") {
            if (spoof_active) {
                result = umount2("/proc/cpuinfo", MNT_DETACH);
                if (result == 0) {
                    spoof_active = false;
                    LOGD("[COMPANION] Unmount successful");
                } else {
                    LOGE("[COMPANION] Unmount failed: %s", strerror(errno));
                }
            } else {
                result = 0; 
            }
        }
        
        write(fd, &result, sizeof(result));
    }
    
    close(fd);
}

class CPUGUARD : public zygisk::ModuleBase {
public:
    void onLoad(zygisk::Api* api, JNIEnv* env) override {
        this->api = api;
        config_manager.loadConfig();
    }

    void preAppSpecialize(zygisk::AppSpecializeArgs* args) override {
        if (!args || !args->nice_name) {
            api->setOption(zygisk::DLCLOSE_MODULE_LIBRARY);
            return;
        }
        
        const char* package_name = env->GetStringUTFChars(args->nice_name, nullptr);
        config_manager.loadConfig();
        
        if (config_manager.isBlacklisted(package_name)) {
            LOGD("Blacklisted app: %s - unmounting spoof", package_name);
            executeCompanionCommand("unmount_spoof");
            
        } else if (config_manager.isGamelisted(package_name)) {
            LOGD("Gamelisted app: %s - mounting spoof", package_name);
            executeCompanionCommand("mount_spoof");
        }
        
        env->ReleaseStringUTFChars(args->nice_name, package_name);
        api->setOption(zygisk::DLCLOSE_MODULE_LIBRARY);
    }

private:
    zygisk::Api* api;
    JNIEnv* env;
    
    bool executeCompanionCommand(const std::string& command) {
        int fd = api->connectCompanion();
        if (fd < 0) {
            LOGE("Failed to connect to companion");
            return false;
        }
        
        write(fd, command.c_str(), command.size());
        
        int result = -1;
        read(fd, &result, sizeof(result));
        close(fd);
        
        return result == 0;
    }
};

REGISTER_ZYGISK_MODULE(CPUGUARD)
REGISTER_ZYGISK_COMPANION(companion)
