#include "net.h"

#include <string.h>
#include <unistd.h>
#include <sys/socket.h>
#include <sys/select.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <coreinit/thread.h>
#include <coreinit/fastmutex.h>
#include <coreinit/time.h>
#include <coreinit/debug.h>

#define MOBILEPRO_STACK_SIZE  (16 * 1024)

// El protocolo va en network byte order (big-endian), como es estandar.
// En la Wii U (PowerPC, big-endian nativo) htons/ntohs no hacen nada en
// la practica, pero los llamamos igual para que el codigo sea correcto
// y portable. El lado ARM (Android, little-endian) SI necesita convertir.
static inline int16_t ntohs_s16(int16_t v) {
    return (int16_t)ntohs((uint16_t)v);
}
static inline int16_t htons_s16(int16_t v) {
    return (int16_t)htons((uint16_t)v);
}

static void FixEndianness(MobileControllerState *s) {
    s->buttons     = ntohl(s->buttons);  // buttons es uint32_t
    s->leftStickX  = ntohs_s16(s->leftStickX);
    s->leftStickY  = ntohs_s16(s->leftStickY);
    s->rightStickX = ntohs_s16(s->rightStickX);
    s->rightStickY = ntohs_s16(s->rightStickY);
    s->magic       = ntohs(s->magic);
    // battery, flags y channel son de 1 byte, no necesitan conversion
}

static OSThread   s_thread;
static uint8_t    s_stack[MOBILEPRO_STACK_SIZE] __attribute__((aligned(8)));
static bool       s_threadCreated = false;
static volatile bool s_running = false;

static int         s_socket = -1;
static OSFastMutex  s_mutex; // protege los 4 slots de abajo, uno por canal

// Un slot de estado por cada canal WPAD/KPAD posible (0-3). Cada celular
// le dice a la Wii U que canal quiere ser (campo `channel` del paquete),
// asi que varios celulares pueden estar conectados al mismo tiempo, cada
// uno en su propio slot, sin pisarse entre si.
struct ChannelSlot {
    MobileControllerState state;
    OSTime                lastPacketTime;
    struct sockaddr_in    clientAddr;
    bool                  haveClient;
};
static ChannelSlot s_channels[MOBILEPRO_MAX_CONTROLLERS] = {};

static void SendAckTo(const struct sockaddr_in &addr, uint8_t channel) {
    MobileConnAck ack;
    ack.magic   = htons(MOBILEPRO_PACKET_MAGIC);
    ack.status  = MOBILEPRO_ACK_CONNECTED;
    ack.channel = channel;
    sendto(s_socket, &ack, sizeof(ack), 0, (const struct sockaddr *)&addr, sizeof(addr));
}

static int MobileNetThreadEntry(int argc, const char **argv) {
    OSReport("[MobilePro] Hilo de red arrancado, escuchando UDP:%d (hasta %d controles)\n",
             MOBILEPRO_PORT, MOBILEPRO_MAX_CONTROLLERS);

    uint8_t recvBuf[64];

    while (s_running) {
        fd_set readSet;
        FD_ZERO(&readSet);
        FD_SET(s_socket, &readSet);

        struct timeval tv;
        tv.tv_sec  = 0;
        tv.tv_usec = 200 * 1000; // 200ms, para poder revisar s_running seguido

        int ret = select(s_socket + 1, &readSet, nullptr, nullptr, &tv);
        if (ret <= 0) {
            continue; // timeout o error, seguimos revisando s_running
        }

        struct sockaddr_in fromAddr;
        socklen_t fromLen = sizeof(fromAddr);
        int received = recvfrom(s_socket, recvBuf, sizeof(recvBuf), 0,
                                 (struct sockaddr *)&fromAddr, &fromLen);

        if (received != (int)sizeof(MobileControllerState)) {
            continue; // paquete de tamaño raro, lo ignoramos
        }

        auto *pkt = (MobileControllerState *)recvBuf;
        FixEndianness(pkt); // los bytes que llegaron todavia estan en network byte order

        if (pkt->magic != MOBILEPRO_PACKET_MAGIC) {
            continue; // no es nuestro protocolo, lo ignoramos
        }

        if (pkt->channel >= MOBILEPRO_MAX_CONTROLLERS) {
            continue; // canal invalido, el celular pidio algo fuera de rango
        }

        ChannelSlot &slot = s_channels[pkt->channel];

        OSFastMutex_Lock(&s_mutex);

        // Es un cliente "nuevo" en este canal si no habia nadie, si es
        // una IP/puerto distinto al ultimo que vimos en ese canal, o si
        // el ultimo ya habia expirado por timeout (se daba por
        // desconectado).
        bool isNewClient = true;
        if (slot.haveClient) {
            int64_t elapsedMs = OSTicksToMilliseconds(OSGetSystemTime() - slot.lastPacketTime);
            bool sameClient = (fromAddr.sin_addr.s_addr == slot.clientAddr.sin_addr.s_addr) &&
                               (fromAddr.sin_port        == slot.clientAddr.sin_port);
            isNewClient = (elapsedMs > MOBILEPRO_TIMEOUT_MS) || !sameClient;
        }

        slot.state          = *pkt;
        slot.lastPacketTime = OSGetSystemTime();
        slot.clientAddr     = fromAddr;
        slot.haveClient      = true;

        OSFastMutex_Unlock(&s_mutex);

        if (isNewClient) {
            SendAckTo(fromAddr, pkt->channel);
            OSReport("[MobilePro] Cliente conectado en canal %d: %s:%d\n",
                     pkt->channel, inet_ntoa(fromAddr.sin_addr), ntohs(fromAddr.sin_port));
        }
    }

    OSReport("[MobilePro] Hilo de red terminado\n");
    return 0;
}

bool MobileNet_Init() {
    if (s_running) {
        return true; // ya estaba corriendo
    }

    s_socket = socket(AF_INET, SOCK_DGRAM, 0);
    if (s_socket < 0) {
        OSReport("[MobilePro] ERROR: no se pudo crear el socket UDP\n");
        return false;
    }

    struct sockaddr_in bindAddr = {};
    bindAddr.sin_family      = AF_INET;
    bindAddr.sin_addr.s_addr = INADDR_ANY;
    bindAddr.sin_port        = htons(MOBILEPRO_PORT);

    if (bind(s_socket, (struct sockaddr *)&bindAddr, sizeof(bindAddr)) < 0) {
        OSReport("[MobilePro] ERROR: no se pudo hacer bind en el puerto %d\n", MOBILEPRO_PORT);
        close(s_socket);
        s_socket = -1;
        return false;
    }

    OSFastMutex_Init(&s_mutex, "MobileProNet");
    memset(s_channels, 0, sizeof(s_channels));

    s_running = true;
    bool ok = OSCreateThread(&s_thread, MobileNetThreadEntry, 0, nullptr,
                              s_stack + MOBILEPRO_STACK_SIZE, MOBILEPRO_STACK_SIZE,
                              16, OS_THREAD_ATTRIB_AFFINITY_ANY);
    if (!ok) {
        OSReport("[MobilePro] ERROR: no se pudo crear el hilo de red\n");
        s_running = false;
        close(s_socket);
        s_socket = -1;
        return false;
    }

    s_threadCreated = true;
    OSResumeThread(&s_thread);
    return true;
}

void MobileNet_Shutdown() {
    if (!s_running) {
        return;
    }

    s_running = false;

    if (s_threadCreated) {
        int exitValue = 0;
        OSJoinThread(&s_thread, &exitValue);
        s_threadCreated = false;
    }

    if (s_socket >= 0) {
        close(s_socket);
        s_socket = -1;
    }

    memset(s_channels, 0, sizeof(s_channels));
}

static bool ChannelIsStale(const ChannelSlot &slot) {
    if (!slot.haveClient) {
        return true;
    }
    int64_t elapsedMs = OSTicksToMilliseconds(OSGetSystemTime() - slot.lastPacketTime);
    return elapsedMs > MOBILEPRO_TIMEOUT_MS;
}

bool MobileNet_GetState(int channel, MobileControllerState *out) {
    memset(out, 0, sizeof(MobileControllerState));

    if (channel < 0 || channel >= MOBILEPRO_MAX_CONTROLLERS) {
        return false;
    }

    OSFastMutex_Lock(&s_mutex);
    ChannelSlot &slot = s_channels[channel];
    bool stale = ChannelIsStale(slot);
    MobileControllerState state = slot.state;
    OSFastMutex_Unlock(&s_mutex);

    if (stale) {
        return false; // no hay celular conectado en este canal (o dejo de mandar)
    }

    *out = state;
    return true;
}

bool MobileNet_HasClient(int channel) {
    if (channel < 0 || channel >= MOBILEPRO_MAX_CONTROLLERS) {
        return false;
    }

    OSFastMutex_Lock(&s_mutex);
    bool stale = ChannelIsStale(s_channels[channel]);
    OSFastMutex_Unlock(&s_mutex);

    return !stale;
}

void MobileNet_SendRumble(int channel, uint8_t enabled, uint8_t intensity, uint16_t durationMs) {
    if (s_socket < 0 || channel < 0 || channel >= MOBILEPRO_MAX_CONTROLLERS) {
        return;
    }

    OSFastMutex_Lock(&s_mutex);
    ChannelSlot &slot = s_channels[channel];
    bool haveClient = !ChannelIsStale(slot);
    struct sockaddr_in clientAddr = slot.clientAddr;
    OSFastMutex_Unlock(&s_mutex);

    if (!haveClient) {
        return;
    }

    MobileRumbleCommand cmd;
    cmd.magic      = htons(MOBILEPRO_PACKET_MAGIC);
    cmd.enabled    = enabled;
    cmd.intensity  = intensity;
    cmd.durationMs = htons(durationMs);

    sendto(s_socket, &cmd, sizeof(cmd), 0,
           (struct sockaddr *)&clientAddr, sizeof(clientAddr));
}

void MobileNet_ForceDisconnect(int channel) {
    if (channel < 0 || channel >= MOBILEPRO_MAX_CONTROLLERS) {
        return;
    }

    OSFastMutex_Lock(&s_mutex);
    // Con marcar haveClient=false alcanza: el proximo paquete valido que
    // llegue a este canal (de cualquier IP, incluida la misma) va a ser
    // tratado como cliente nuevo y va a recibir el ack de vuelta.
    s_channels[channel].haveClient = false;
    OSFastMutex_Unlock(&s_mutex);

    OSReport("[MobilePro] Canal %d desconectado a la fuerza\n", channel);
}

void MobileNet_ForceDisconnectAll() {
    OSFastMutex_Lock(&s_mutex);
    for (int i = 0; i < MOBILEPRO_MAX_CONTROLLERS; i++) {
        s_channels[i].haveClient = false;
    }
    OSFastMutex_Unlock(&s_mutex);

    OSReport("[MobilePro] Todos los canales desconectados a la fuerza\n");
}
