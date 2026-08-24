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
#include "protocol.h"
#include "net.h"

WUPS_PLUGIN_NAME("Mobile Pro Controller");
WUPS_PLUGIN_DESCRIPTION("Use your phone as a Wii U Pro Controller (Phase 1)");
WUPS_PLUGIN_VERSION("v0.1.0");
WUPS_PLUGIN_AUTHOR("Sucuboy_u");
WUPS_PLUGIN_LICENSE("MIT");

WUPS_USE_WUT_DEVOPTAB();
WUPS_USE_STORAGE("mobileprocontroller");

#define ENABLE_CONFIG_ID           "enabled"
#define DISCONNECT_ALL_CONFIG_ID   "disconnect_all"

static struct {
    bool enable;
} g_settings = {false};

static uint64_t g_applicationTitleId = 0;
static bool     g_configMenuOpen     = false;

static inline bool IsGame(uint64_t id) {
    return (id & 0xFFFFFFFF00000000ULL) == 0x0005000000000000ULL;
}

static inline bool UseReal() {
    return !IsGame(g_applicationTitleId) || g_configMenuOpen;
}

// Ya no hay un "canal configurado" fijo: el canal lo elige cada celular
// (campo `channel` del paquete), y este plugin solo intercepta un canal
// si HAY un celular conectado ahi de verdad. Asi varios celulares pueden
// jugar a la vez, cada uno en su propio canal, sin tener que configurar
// nada a mano en el menu del plugin.
static inline bool IsOurChannel(int chan) {
    return g_settings.enable && !UseReal() && MobileNet_HasClient(chan);
}

static void enableItemChanged(ConfigItemBoolean *item, bool newValue) {
    if (strcmp(item->identifier, ENABLE_CONFIG_ID) == 0) {
        g_settings.enable = newValue;
        WUPSStorageAPI::Store(ENABLE_CONFIG_ID, newValue);
    }
}

// Este item no se guarda en storage a proposito: es un "boton" disfrazado
// de toggle. Cada vez que se abre el menu arranca en false; si el usuario
// lo pone en true, desconectamos todo al toque. No hace falta que vuelva
// a false solo, simplemente no persiste entre aperturas del menu.
static void disconnectAllItemChanged(ConfigItemBoolean *item, bool newValue) {
    if (strcmp(item->identifier, DISCONNECT_ALL_CONFIG_ID) == 0 && newValue) {
        MobileNet_ForceDisconnectAll();
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

        root.add(WUPSConfigItemBoolean::Create(
            DISCONNECT_ALL_CONFIG_ID,
            "Force Disconnect All Controllers",
            false,
            false,
            &disconnectAllItemChanged));
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

    WUPSStorageAPI::GetOrStoreDefault(ENABLE_CONFIG_ID, g_settings.enable, false);
    WUPSStorageAPI::SaveStorage();

    OSReport("[MobilePro] Plugin initialized. Enabled=%d\n", g_settings.enable);
}

DEINITIALIZE_PLUGIN() {
    MobileNet_Shutdown(); // por las dudas, si quedó levantado
}

ON_APPLICATION_START() {
    g_applicationTitleId = OSGetTitleID();
    OSReport("[MobilePro] App started: 0x%016llX\n", g_applicationTitleId);

    // La red se levanta acá, no en INITIALIZE_PLUGIN: INITIALIZE_PLUGIN
    // corre muy temprano (durante el boot de Aroma), antes de que el
    // stack de red de la consola este necesariamente listo. Si el
    // socket/bind se cuelgan ahi, se cuelga todo el arranque del juego.
    // Para cuando el juego ya arranco (ON_APPLICATION_START), la red
    // ya esta lista.
    if (!MobileNet_Init()) {
        OSReport("[MobilePro] ERROR: no se pudo levantar el servidor UDP\n");
    }
}

ON_APPLICATION_REQUESTS_EXIT() {
    g_applicationTitleId = 0;
    MobileNet_Shutdown();
}

// Ultimo estado "hold" que vio KPAD, para poder calcular trigger
// (recien presionado) / release (recien soltado) entre lecturas.
static uint32_t s_prevKpadHold = 0;

// Escala el stick del protocolo (-32768..32767) al rango real del
// Pro Controller para WPAD ([-2048, 2047], ver padscore/wpad.h).
static inline int16_t ScaleStickToWPAD(int16_t v) {
    return (int16_t)(v / 16);
}

// Escala el stick del protocolo (-32768..32767) al formato float
// normalizado (-1.0f..1.0f) que espera KPAD.
static inline float ScaleStickToKPAD(int16_t v) {
    return (float)v / 32768.0f;
}

static void FillStaticProControllerData_KPAD(KPADStatus *kpad, const MobileControllerState &state) {
    memset(kpad, 0, sizeof(KPADStatus));

    uint32_t hold = state.buttons;
    kpad->pro.hold    = hold;
    kpad->pro.trigger = hold & ~s_prevKpadHold;   // bits que se acaban de presionar
    kpad->pro.release = s_prevKpadHold & ~hold;   // bits que se acaban de soltar
    s_prevKpadHold    = hold;

    kpad->pro.leftStick.x  = ScaleStickToKPAD(state.leftStickX);
    kpad->pro.leftStick.y  = ScaleStickToKPAD(state.leftStickY);
    kpad->pro.rightStick.x = ScaleStickToKPAD(state.rightStickX);
    kpad->pro.rightStick.y = ScaleStickToKPAD(state.rightStickY);

    kpad->format         = KPADDataFormat::WPAD_FMT_PRO_CONTROLLER;
    kpad->extensionType  = KPADExtensionType::WPAD_EXT_PRO_CONTROLLER;
    kpad->error          = KPADError::KPAD_ERROR_OK;
    kpad->pro.charging   = (state.flags & MOBILEPRO_FLAG_CONNECTED) == 0;
    kpad->pro.wired      = (state.flags & MOBILEPRO_FLAG_WIRED) != 0;
}

static void FillStaticProControllerData_WPAD(WPADStatusProController *wpad, const MobileControllerState &state) {
    memset(wpad, 0, sizeof(WPADStatusProController));
    wpad->core.error         = 0;
    wpad->core.extensionType = WPAD_EXT_PRO_CONTROLLER;

    wpad->buttons = state.buttons;
    wpad->leftStick.x  = ScaleStickToWPAD(state.leftStickX);
    wpad->leftStick.y  = ScaleStickToWPAD(state.leftStickY);
    wpad->rightStick.x = ScaleStickToWPAD(state.rightStickX);
    wpad->rightStick.y = ScaleStickToWPAD(state.rightStickY);

    wpad->wired    = (state.flags & MOBILEPRO_FLAG_WIRED) != 0;
    wpad->charging = FALSE;
}

DECL_FUNCTION(int32_t, KPADReadEx, KPADChan channel, KPADStatus *data, uint32_t size, KPADError *outError) {
    if (!IsOurChannel((int)channel)) {
        return real_KPADReadEx(channel, data, size, outError);
    }
    if (data == nullptr || size == 0) {
        *outError = KPAD_ERROR_NO_SAMPLES;
        return 0;
    }

    MobileControllerState state;
    if (!MobileNet_GetState((int)channel, &state)) {
        // No hay celular conectado todavía en este canal: no hay muestras que dar.
        *outError = KPAD_ERROR_NO_SAMPLES;
        return 0;
    }

    FillStaticProControllerData_KPAD(&data[0], state);
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
    MobileControllerState state;
    MobileNet_GetState((int)chan, &state); // si no hay celular conectado en este canal, queda en ceros

    auto *wpadStatus = (WPADStatusProController *)buffer;
    FillStaticProControllerData_WPAD(wpadStatus, state);
}

DECL_FUNCTION(void, WPADControlMotor, WPADChan chan, BOOL enabled) {
    if (!IsOurChannel((int)chan)) {
        real_WPADControlMotor(chan, enabled);
        return;
    }
    OSReport("[MobilePro] Rumble requested: channel=%d enabled=%d\n", (int)chan, enabled);
    MobileNet_SendRumble((int)chan, enabled ? 1 : 0, enabled ? 200 : 0, enabled ? 150 : 0);
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
