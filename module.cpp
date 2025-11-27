#include <jni.h>
#include <string>
#include <zygisk.hpp>
#include <nlohmann/json.hpp>
#include <fstream>
#include <unordered_set>
#include <unistd.h>
#include <android/log.h>
#include <sys/stat.h>

using json = nlohmann::json;

#define LOG_TAG "CPUGUARD"
#define LOGD(...) __android_log_print(ANDROID_LOG_DEBUG, LOG_TAG, __VA_ARGS__)
#define LOGE(...) __android_log_print(ANDROID_LOG_ERROR, LOG_TAG, __VA_ARGS__)

static const std::string config_path = "/data/adb/modules/COPG_CPU/apps.json";
static const std::string spoof_file_path = "/data/adb/modules/COPG_CPU/cpuinfo_spoof";

class ConfigManager {
private:
    std::unordered_set<std::string> blacklist_packages;
    std::unordered_set<std::string> gamelist_packages;
    time_t last_mtime = 0;

public:
    bool loadConfig() {
        struct stat st;
        if (stat(config_path.c_str(), &st) != 0) {
            return false;
        }
        
        if (st.st_mtime == last_mtime) {
            return true;
        }
        
        std::ifstream file(config_path);
        if (!file.is_open()) {
            return false;
        }
        
        try {
            json j = json::parse(file);
            
            blacklist_packages.clear();
            gamelist_packages.clear();
            
            if (j.contains("blacklist") && j["blacklist"].is_array()) {
                for (const auto& item : j["blacklist"]) {
                    if (item.contains("package_name") && item["package_name"].is_string()) {
                        blacklist_packages.insert(item["package_name"].get<std::string>());
                    }
                }
            }
            
            if (j.contains("gamelist") && j["gamelist"].is_array()) {
                for (const auto& item : j["gamelist"]) {
                    if (item.contains("package_name") && item["package_name"].is_string()) {
                        gamelist_packages.insert(item["package_name"].get<std::string>());
                    }
                }
            }
            
            last_mtime = st.st_mtime;
            LOGD("Config loaded: %zu blacklist, %zu gamelist", 
                 blacklist_packages.size(), gamelist_packages.size());
            return true;
            
        } catch (const std::exception& e) {
            return false;
        }
    }
    
    bool isBlacklisted(const std::string& pkg) const {
        return blacklist_packages.count(pkg) > 0;
    }
    
    bool isGamelisted(const std::string& pkg) const {
        return gamelist_packages.count(pkg) > 0;
    }
};

static ConfigManager config_manager;

static void companion(int fd) {
    char buffer[256];
    ssize_t bytes = read(fd, buffer, sizeof(buffer)-1);
    
    if (bytes > 0) {
        buffer[bytes] = '\0';
        std::string command = buffer;
        
        int result = -1;
        
        if (command == "unmount_spoof") {
            struct stat st;
            if (stat("/proc/cpuinfo", &st) == 0) {
                result = umount2("/proc/cpuinfo", MNT_DETACH);
                if (result == 0) {
                    LOGD("[COMPANION] Unmount successful");
                }
            } else {
                result = 0; 
            }
            
        } else if (command == "mount_spoof") {
            if (access(spoof_file_path.c_str(), F_OK) == 0) {
                umount2("/proc/cpuinfo", MNT_DETACH);
                
                result = mount(spoof_file_path.c_str(), "/proc/cpuinfo", nullptr, MS_BIND, nullptr);
                if (result == 0) {
                    LOGD("[COMPANION] Mount successful");
                }
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
