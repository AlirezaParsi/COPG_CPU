COPG CPU Spoofer

A Zygisk module for Android that dynamically spoofs CPU information for specific applications while maintaining compatibility with sensitive apps.

Features

· Dynamic CPU Spoofing: Masks real CPU information for selected applications
· Dual List System:
  · Blacklist: Apps sensitive to mount operations - prevents crashes and root/mount detection
  · Gamelist: Apps that receive spoofed CPU info
· JSON Configuration: Easy app management via config file
· Real-time Processing: No reboot required for config changes

Configuration

Create /data/adb/modules/COPG_CPU/apps.json:

```json
{
  "blacklist": [
    {
      "package_name": "com.example.bank",
      "app_name": "Example Bank App"
    },
    {
      "package_name": "com.example.payment", 
      "app_name": "Example Payment App"
    }
  ],
  "gamelist": [
    {
      "package_name": "com.example.game1",
      "app_name": "Example Game 1"
    },
    {
      "package_name": "com.example.game2",
      "app_name": "Example Game 2"
    }
  ]
}
```

Purpose of Lists

· Blacklist: For apps that crash or detect root/mount operations
· Gamelist: For apps that should see spoofed CPU information

How It Works

1. Module Loads: Initializes when system starts
2. App Detection: Monitors app launches via Zygisk
3. List Check: Compares app package against configured lists
4. Action:
   · Blacklist: Unmounts spoof, shows real CPU info
   · Gamelist: Mounts spoofed CPU info
   · Others: No action taken

Logging

View logs with:

```bash
su -c logcat -s CPUGUARD
```
