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
#include <fcntl.h>

using json = nlohmann::json;

#define LOG_TAG "CPUGUARD"
#define LOGD(...) __android_log_print(ANDROID_LOG_DEBUG, LOG_TAG, __VA_ARGS__)
#define LOGE(...) __android_log_print(ANDROID_LOG_ERROR, LOG_TAG, __VA_ARGS__)

static const std::string config_path = "/data/adb/modules/COPG_CPU/apps.json";
static const std::string spoof_file_path = "/data/adb/modules/COPG_CPU/cpuinfo_spoof";
static const std::string log_path = "/data/adb/modules/COPG_CPU/debug.log";

static void write_log(const std::string& message) {
    int fd = open(log_path.c_str(), O_WRONLY | O_CREAT | O_APPEND, 0644);
    if (fd >= 0) {
        std::string full_msg = message + "\n";
        write(fd, full_msg.c_str(), full_msg.size());
        close(fd);
    }
}

class ConfigManager {
private:
    std::unordered_set<std::string> blacklist_packages;
    std::unordered_set<std::string> gamelist_packages;
    time_t last_mtime = 0;

public:
    bool loadConfig() {
        write_log("ConfigManager: loadConfig called");
        
        struct stat st;
        if (stat(config_path.c_str(), &st) != 0) {
            write_log("ConfigManager: config file not found");
            return false;
        }
        
        if (st.st_mtime == last_mtime) {
            write_log("ConfigManager: config unchanged");
            return true;
        }
        
        std::ifstream file(config_path);
        if (!file.is_open()) {
            write_log("ConfigManager: failed to open config file");
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
            write_log("ConfigManager: loaded " + std::to_string(blacklist_packages.size()) + " blacklist, " + std::to_string(gamelist_packages.size()) + " gamelist");
            return true;
            
        } catch (const std::exception& e) {
            write_log("ConfigManager: JSON error: " + std::string(e.what()));
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
    write_log("Companion: started");
    
    char buffer[256];
    ssize_t bytes = read(fd, buffer, sizeof(buffer)-1);
    
    if (bytes > 0) {
        buffer[bytes] = '\0';
        std::string command = buffer;
        
        write_log("Companion: received command: " + command);
        
        int result = -1;
        
        if (command == "unmount_spoof") {
            write_log("Companion: attempting unmount");
            result = umount2("/proc/cpuinfo", MNT_DETACH);
            if (result == 0) {
                write_log("Companion: unmount successful");
            } else {
                write_log("Companion: unmount failed, errno: " + std::to_string(errno));
            }
            
        } else if (command == "mount_spoof") {
            write_log("Companion: attempting mount");
            if (access(spoof_file_path.c_str(), F_OK) == 0) {
                // اول مطمئن شویم unmount شده
                umount2("/proc/cpuinfo", MNT_DETACH);
                
                result = mount(spoof_file_path.c_str(), "/proc/cpuinfo", nullptr, MS_BIND, nullptr);
                if (result == 0) {
                    write_log("Companion: mount successful");
                } else {
                    write_log("Companion: mount failed, errno: " + std::to_string(errno));
                }
            } else {
                write_log("Companion: spoof file not found");
            }
        }
        
        write_log("Companion: sending result: " + std::to_string(result));
        write(fd, &result, sizeof(result));
    }
    
    close(fd);
    write_log("Companion: finished");
}

class CPUGUARD : public zygisk::ModuleBase {
public:
    void onLoad(zygisk::Api* api, JNIEnv* env) override {
        write_log("CPUGUARD: onLoad called");
        this->api = api;
        config_manager.loadConfig();
    }

    void preAppSpecialize(zygisk::AppSpecializeArgs* args) override {
        write_log("CPUGUARD: preAppSpecialize called");
        
        if (!args || !args->nice_name) {
            write_log("CPUGUARD: no package name, closing module");
            api->setOption(zygisk::DLCLOSE_MODULE_LIBRARY);
            return;
        }
        
        const char* package_name = env->GetStringUTFChars(args->nice_name, nullptr);
        std::string pkg_str = package_name ? package_name : "unknown";
        
        write_log("CPUGUARD: processing package: " + pkg_str);
        
        config_manager.loadConfig();
        
        if (config_manager.isBlacklisted(pkg_str)) {
            write_log("CPUGUARD: blacklisted app - " + pkg_str);
            executeCompanionCommand("unmount_spoof");
            
        } else if (config_manager.isGamelisted(pkg_str)) {
            write_log("CPUGUARD: gamelisted app - " + pkg_str);
            executeCompanionCommand("mount_spoof");
        } else {
            write_log("CPUGUARD: package not in lists - " + pkg_str);
        }
        
        if (package_name) {
            env->ReleaseStringUTFChars(args->nice_name, package_name);
        }
        
        api->setOption(zygisk::DLCLOSE_MODULE_LIBRARY);
        write_log("CPUGUARD: preAppSpecialize finished");
    }

private:
    zygisk::Api* api;
    JNIEnv* env;
    
    bool executeCompanionCommand(const std::string& command) {
        write_log("CPUGUARD: executing companion command: " + command);
        
        int fd = api->connectCompanion();
        if (fd < 0) {
            write_log("CPUGUARD: failed to connect to companion");
            return false;
        }
        
        write(fd, command.c_str(), command.size());
        
        int result = -1;
        read(fd, &result, sizeof(result));
        close(fd);
        
        write_log("CPUGUARD: companion result: " + std::to_string(result));
        return result == 0;
    }
};

REGISTER_ZYGISK_MODULE(CPUGUARD)
REGISTER_ZYGISK_COMPANION(companion)
