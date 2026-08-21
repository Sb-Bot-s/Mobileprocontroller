/**
 * protocol.h
 * Estructura de datos compartida entre la app móvil y el plugin Wii U.
 * 
 * IMPORTANTE: Esta estructura debe ser IDÉNTICA en ambos lados.
 * No usar bool (tamaño variable). Usar uint8_t para flags.
 * Padding deshabilitado con __attribute__((packed)).
 */

#ifndef MOBILEPRO_PROTOCOL_H
#define MOBILEPRO_PROTOCOL_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define MOBILEPRO_PORT          9100
#define MOBILEPRO_PACKET_MAGIC  0x4D50  // 'MP'

/**
 * Estado del control enviado desde el móvil a la Wii U.
 * 
 * Formato binario little-endian, 14 bytes:
 *   [0-1]  uint16_t buttons      (máscara de botones)
 *   [2-3]  int16_t  leftStickX    (-32768 a 32767)
 *   [4-5]  int16_t  leftStickY
 *   [6-7]  int16_t  rightStickX
 *   [8-9]  int16_t  rightStickY
 *   [10]   uint8_t  battery        (0-100, simulado)
 *   [11]   uint8_t  flags          (bit 0: conectado, bit 1: cableado)
 *   [12-13] uint16_t magic         (0x4D50 para validar paquete)
 */
struct MobileControllerState {
    uint16_t buttons;       // Máscara de botones (ver abajo)
    int16_t  leftStickX;    // Stick izquierdo X
    int16_t  leftStickY;    // Stick izquierdo Y
    int16_t  rightStickX;   // Stick derecho X
    int16_t  rightStickY;   // Stick derecho Y
    uint8_t  battery;       // Nivel de batería (0-100)
    uint8_t  flags;         // Flags de estado
    uint16_t magic;         // Magic number para validación
} __attribute__((packed));

// Tamaño esperado: 14 bytes
#define MOBILEPRO_PACKET_SIZE  sizeof(struct MobileControllerState)

// Flags
#define MOBILEPRO_FLAG_CONNECTED  0x01
#define MOBILEPRO_FLAG_WIRED       0x02

// Máscaras de botones (deben coincidir con WPAD_PRO_BUTTON_*)
#define MP_BTN_A            0x0100
#define MP_BTN_B            0x0008
#define MP_BTN_X            0x0002
#define MP_BTN_Y            0x0400
#define MP_BTN_L            0x0010
#define MP_BTN_R            0x0200
#define MP_BTN_ZL           0x0080
#define MP_BTN_ZR           0x0001
#define MP_BTN_UP           0x0020
#define MP_BTN_DOWN         0x0040
#define MP_BTN_LEFT         0x4000
#define MP_BTN_RIGHT        0x8000
#define MP_BTN_PLUS         0x1000
#define MP_BTN_MINUS        0x0004
#define MP_BTN_HOME         0x0800
#define MP_BTN_LSTICK       0x2000   // Click stick izquierdo
#define MP_BTN_RSTICK       0x1000   // Click stick derecho

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

#ifdef __cplusplus
}
#endif

#endif // MOBILEPRO_PROTOCOL_H
