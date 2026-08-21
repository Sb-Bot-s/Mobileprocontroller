#include <malloc.h>
#include <string.h>
#include <exception>
#include <wups.h>
#include <wups/config/WUPSConfigItemBoolean.h>
#include <wups/config/WUPSConfigItemIntegerRange.h>
#include <wups/config_api.h>
#include <padscore/wpad.h>
#include <padscore/kpad.h>
#include <coreinit/title.h>
#include <coreinit/debug.h>

WUPS_PLUGIN_NAME("Mobile Pro Controller");
WUPS_PLUGIN_DESCRIPTION("Use your phone as a Wii U Pro Controller (Phase 1)");
WUPS_PLUGIN_VERSION("v0.1.0");
WUPS_PLUGIN_AUTHOR("TuNombre");
WUPS_PLUGIN_LICENSE("MIT");

WUPS_USE_WUT_DEVOPTAB();
WUPS_USE_STORAGE("mobileprocontroller");

#define ENABLE_CONFIG_ID    "enabled"
#define CHANNEL_CONFIG_ID   "channel"

static struct {
    bool enable;
    int  channel;
} g_settings = {false, 0};

static uint64_t g_applicationTitleId = 0;
static bool     g_configMenuOpen     = false;

static inline bool IsGame(uint64_t id) {
    return (id & 0xFFFFFFFF00000000ULL) == 0x0005000000000000ULL;
}

static inline bool UseReal() {
    return !IsGame(g_applicationTitleId) || g_configMenuOpen;
}

static inline bool IsOurChannel(int chan) {
    return g_settings.enable && (g_settings.channel == chan) && !UseReal();
}

static void channelItemChanged(ConfigItemIntegerRange *item, int newValue) {
    if (strcmp(item->identifier, CHANNEL_CONFIG_ID) == 0) {
        g_settings.channel = newValue;
        WUPSStorageAPI::Store(CHANNEL_CONFIG_ID, newValue);
    }
}

static void enableItemChanged(ConfigItemBoolean *item, bool newValue) {
    if (strcmp(item->identifier, ENABLE_CONFIG_ID) == 0) {
        g_settings.enable = newValue;
        WUPSStorageAPI::Store(ENABLE_CONFIG_ID, newValue);
    }
}

WUPSConfigAPICallbackStatus ConfigMenuOpenedCallback(WUPSConfigCategoryHandle rootHandle) {
    g_configMenuOpen = true;
    WUPSConfigCategory root = WUPSConfigCategory(rootHandle);

    try {
        root.add(WUPSConfigItemBoolean::Create(
            ENABLE_CONFIG_ID,
            "Enable Mobile Pro Controller",
            false,
            g_settings.enable,
            &enableItemChanged));

        root.add(WUPSConfigItemIntegerRange::Create(
            CHANNEL_CONFIG_ID,
            "Emulated Channel",
            0,
            g_settings.channel,
            0, 3,
            &channelItemChanged));
    } catch (std::exception &e) {
        return WUPSCONFIG_API_CALLBACK_RESULT_ERROR;
    }

    return WUPSCONFIG_API_CALLBACK_RESULT_SUCCESS;
}

static void ConfigMenuClosedCallback() {
    WUPSStorageAPI::SaveStorage();
    g_configMenuOpen = false;
}

INITIALIZE_PLUGIN() {
    WUPSConfigAPIOptionsV1 configOptions = {.name = "Mobile Pro Controller"};
    WUPSConfigAPI_Init(configOptions, ConfigMenuOpenedCallback, ConfigMenuClosedCallback);

    WUPSStorageAPI::GetOrStoreDefault(ENABLE_CONFIG_ID,  g_settings.enable,  false);
    WUPSStorageAPI::GetOrStoreDefault(CHANNEL_CONFIG_ID, g_settings.channel, 0);
    WUPSStorageAPI::SaveStorage();

    OSReport("[MobilePro] Plugin initialized. Enabled=%d Channel=%d\n",
             g_settings.enable, g_settings.channel);
}

ON_APPLICATION_START() {
    g_applicationTitleId = OSGetTitleID();
    OSReport("[MobilePro] App started: 0x%016llX\n", g_applicationTitleId);
}

ON_APPLICATION_REQUESTS_EXIT() {
    g_applicationTitleId = 0;
}

static void FillStaticProControllerData_KPAD(KPADStatus *kpad) {
    memset(kpad, 0, sizeof(KPADStatus));
    kpad->pro.hold    = WPAD_PRO_BUTTON_A;
    kpad->pro.trigger = WPAD_PRO_BUTTON_A;
    kpad->pro.leftStick.x  = 0.0f;
    kpad->pro.leftStick.y  = 0.0f;
    kpad->pro.rightStick.x = 0.0f;
    kpad->pro.rightStick.y = 0.0f;
    kpad->format         = KPADDataFormat::WPAD_FMT_PRO_CONTROLLER;
    kpad->extensionType  = KPADExtensionType::WPAD_EXT_PRO_CONTROLLER;
    kpad->error          = KPADError::KPAD_ERROR_OK;
    kpad->pro.charging   = false;
    kpad->pro.wired      = false;
}

static void FillStaticProControllerData_WPAD(WPADStatusProController *wpad) {
    memset(wpad, 0, sizeof(WPADStatusProController));
    wpad->err           = 0;
    wpad->extensionType = WPAD_EXT_PRO_CONTROLLER;
    wpad->dataFormat    = WPAD_FMT_PRO_CONTROLLER;
    wpad->buttons = WPAD_PRO_BUTTON_A;
    wpad->leftStick.x  = 0;
    wpad->leftStick.y  = 0;
    wpad->rightStick.x = 0;
    wpad->rightStick.y = 0;
}

DECL_FUNCTION(int32_t, KPADReadEx, KPADChan channel, KPADStatus *data, uint32_t size, KPADError *outError) {
    if (!IsOurChannel((int)channel)) {
        return real_KPADReadEx(channel, data, size, outError);
    }
    if (data == nullptr || size == 0) {
        *outError = KPAD_ERROR_NO_SAMPLES;
        return 0;
    }
    FillStaticProControllerData_KPAD(&data[0]);
    *outError = KPAD_ERROR_OK;
    return 1;
}

DECL_FUNCTION(int32_t, KPADRead, KPADChan channel, KPADStatus *data, uint32_t size) {
    KPADError errorOut;
    return my_KPADReadEx(channel, data, size, &errorOut);
}

DECL_FUNCTION(int32_t, WPADProbe, WPADChan chan, WPADExtensionType *outExtensionType) {
    if (!IsOurChannel((int)chan)) {
        return real_WPADProbe(chan, outExtensionType);
    }
    *outExtensionType = WPADExtensionType::WPAD_EXT_PRO_CONTROLLER;
    return 0;
}

DECL_FUNCTION(void, WPADRead, WPADChan chan, void *buffer) {
    if (!IsOurChannel((int)chan)) {
        real_WPADRead(chan, buffer);
        return;
    }
    auto *wpadStatus = (WPADStatusProController *)buffer;
    FillStaticProControllerData_WPAD(wpadStatus);
}

DECL_FUNCTION(void, WPADControlMotor, WPADChan chan, BOOL enabled) {
    if (!IsOurChannel((int)chan)) {
        real_WPADControlMotor(chan, enabled);
        return;
    }
    OSReport("[MobilePro] Rumble requested: channel=%d enabled=%d\n", (int)chan, enabled);
}

DECL_FUNCTION(WPADDataFormat, WPADGetDataFormat, WPADChan chan) {
    if (!IsOurChannel((int)chan)) {
        return real_WPADGetDataFormat(chan);
    }
    return WPADDataFormat::WPAD_FMT_PRO_CONTROLLER;
}

DECL_FUNCTION(uint8_t, WPADGetBatteryLevel, WPADChan chan) {
    if (!IsOurChannel((int)chan)) {
        return real_WPADGetBatteryLevel(chan);
    }
    return 0x04;
}

WUPS_MUST_REPLACE(KPADReadEx,      WUPS_LOADER_LIBRARY_PADSCORE, KPADReadEx);
WUPS_MUST_REPLACE(KPADRead,        WUPS_LOADER_LIBRARY_PADSCORE, KPADRead);
WUPS_MUST_REPLACE(WPADProbe,       WUPS_LOADER_LIBRARY_PADSCORE, WPADProbe);
WUPS_MUST_REPLACE(WPADRead,        WUPS_LOADER_LIBRARY_PADSCORE, WPADRead);
WUPS_MUST_REPLACE(WPADControlMotor, WUPS_LOADER_LIBRARY_PADSCORE, WPADControlMotor);
WUPS_MUST_REPLACE(WPADGetDataFormat, WUPS_LOADER_LIBRARY_PADSCORE, WPADGetDataFormat);
WUPS_MUST_REPLACE(WPADGetBatteryLevel, WUPS_LOADER_LIBRARY_PADSCORE, WPADGetBatteryLevel);
