/**
 * protocol.h
 * Estructura de datos compartida entre la app móvil y el plugin Wii U.
 * 
 * IMPORTANTE: Esta estructura debe ser IDÉNTICA en ambos lados.
 * No usar bool (tamaño variable). Usar uint8_t para flags.
 * Padding deshabilitado con __attribute__((packed)).
 *
 * ENDIANNESS: todos los campos multi-byte van en NETWORK BYTE ORDER
 * (big-endian), como es estándar en protocolos de red. La Wii U
 * (PowerPC) es big-endian nativo; el celular (ARM) es little-endian,
 * así que del lado Android hay que armar el paquete con
 * ByteBuffer.order(ByteOrder.BIG_ENDIAN) (o equivalente) antes de
 * mandarlo. Del lado Wii U se usa htons/ntohs/htonl/ntohl (ver
 * net.cpp) aunque en la práctica no hagan nada ahí, para que el
 * código sea correcto y portable igual.
 */

#ifndef MOBILEPRO_PROTOCOL_H
#define MOBILEPRO_PROTOCOL_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define MOBILEPRO_PORT          9100
#define MOBILEPRO_PACKET_MAGIC  0x4D50  // 'MP'
#define MOBILEPRO_MAX_CONTROLLERS  4    // canales WPAD/KPAD validos: 0,1,2,3

/**
 * Estado del control enviado desde el móvil a la Wii U.
 * 
 * Formato binario NETWORK BYTE ORDER (big-endian), 17 bytes:
 *   [0-3]   uint32_t buttons      (máscara de botones, ver WPAD_PRO_BUTTON_*)
 *   [4-5]   int16_t  leftStickX    (-32768 a 32767)
 *   [6-7]   int16_t  leftStickY
 *   [8-9]   int16_t  rightStickX
 *   [10-11] int16_t  rightStickY
 *   [12]    uint8_t  battery        (0-100, simulado)
 *   [13]    uint8_t  flags          (bit 0: conectado, bit 1: cableado)
 *   [14]    uint8_t  channel        (0-3: que jugador/canal quiere ser este celular)
 *   [15-16] uint16_t magic          (0x4D50 para validar paquete)
 */
struct MobileControllerState {
    uint32_t buttons;       // Máscara de botones (ver abajo) - ancho de 32 bits
    int16_t  leftStickX;    // Stick izquierdo X
    int16_t  leftStickY;    // Stick izquierdo Y
    int16_t  rightStickX;   // Stick derecho X
    int16_t  rightStickY;   // Stick derecho Y
    uint8_t  battery;       // Nivel de batería (0-100)
    uint8_t  flags;         // Flags de estado
    uint8_t  channel;       // 0-3: que jugador quiere ser este celular
    uint16_t magic;         // Magic number para validación
} __attribute__((packed));

// Tamaño esperado: 17 bytes
#define MOBILEPRO_PACKET_SIZE  sizeof(struct MobileControllerState)

// Flags
#define MOBILEPRO_FLAG_CONNECTED  0x01
#define MOBILEPRO_FLAG_WIRED       0x02

// Máscaras de botones — COPIADAS TAL CUAL del enum real WPADProButton
// en /opt/devkitpro/wut/include/padscore/wpad.h (confirmado por grep,
// no adivinado). No cambiar estos valores sin volver a chequear el
// header real: la vez pasada que se pusieron a ojo, ninguno coincidía.
#define MP_BTN_UP           0x00000001  // WPAD_PRO_BUTTON_UP
#define MP_BTN_LEFT         0x00000002  // WPAD_PRO_BUTTON_LEFT
#define MP_BTN_ZR           0x00000004  // WPAD_PRO_BUTTON_ZR
#define MP_BTN_X            0x00000008  // WPAD_PRO_BUTTON_X
#define MP_BTN_A            0x00000010  // WPAD_PRO_BUTTON_A
#define MP_BTN_Y            0x00000020  // WPAD_PRO_BUTTON_Y
#define MP_BTN_B            0x00000040  // WPAD_PRO_BUTTON_B
#define MP_BTN_ZL           0x00000080  // WPAD_PRO_BUTTON_ZL
// 0x00000100 esta reservado (WPAD_PRO_RESERVED), no usar
#define MP_BTN_R            0x00000200  // WPAD_PRO_BUTTON_R
#define MP_BTN_PLUS         0x00000400  // WPAD_PRO_BUTTON_PLUS
#define MP_BTN_HOME         0x00000800  // WPAD_PRO_BUTTON_HOME
#define MP_BTN_MINUS        0x00001000  // WPAD_PRO_BUTTON_MINUS
#define MP_BTN_L            0x00002000  // WPAD_PRO_BUTTON_L
#define MP_BTN_DOWN         0x00004000  // WPAD_PRO_BUTTON_DOWN
#define MP_BTN_RIGHT        0x00008000  // WPAD_PRO_BUTTON_RIGHT
#define MP_BTN_RSTICK       0x00010000  // WPAD_PRO_BUTTON_STICK_R (click stick derecho)
#define MP_BTN_LSTICK       0x00020000  // WPAD_PRO_BUTTON_STICK_L (click stick izquierdo)

/**
 * Comando de rumble enviado desde la Wii U al móvil.
 * 
 * El móvil recibe este paquete y activa vibración.
 */
struct MobileRumbleCommand {
    uint16_t magic;         // 0x4D50
    uint8_t  enabled;       // 1 = vibrar, 0 = detener
    uint8_t  intensity;     // 0-255 (intensidad de vibración)
    uint16_t durationMs;    // Duración en milisegundos
} __attribute__((packed));

/**
 * Confirmación de conexión enviada desde la Wii U al móvil.
 * Se manda una sola vez, apenas la Wii U detecta un paquete
 * válido de un cliente nuevo (o de uno que se había desconectado
 * y volvió). Sirve para que la app muestre "conectado" en pantalla
 * en vez de asumirlo, y confirme en que canal/jugador quedó.
 */
struct MobileConnAck {
    uint16_t magic;   // 0x4D50
    uint8_t  status;  // 1 = conexión establecida
    uint8_t  channel; // canal/jugador que la Wii U le asignó (0-3)
} __attribute__((packed));

#define MOBILEPRO_ACK_CONNECTED  1

#ifdef __cplusplus
}
#endif

#endif // MOBILEPRO_PROTOCOL_H
