/**
 * net.h
 * Servidor UDP que corre dentro del plugin (lado Wii U).
 *
 * Escucha en MOBILEPRO_PORT, recibe paquetes MobileControllerState
 * del celular y los guarda en un estado global protegido por mutex.
 * También permite mandar de vuelta un comando de rumble al último
 * cliente que mandó un paquete.
 */

#ifndef MOBILEPRO_NET_H
#define MOBILEPRO_NET_H

#include <stdint.h>
#include "protocol.h"

// Si no llega ningún paquete en este tiempo, se considera desconectado
#define MOBILEPRO_TIMEOUT_MS  500

/**
 * Arranca el hilo del servidor UDP. Llamar una sola vez,
 * idealmente en INITIALIZE_PLUGIN().
 * Devuelve true si el hilo se creó y arrancó bien.
 */
bool MobileNet_Init();

/**
 * Para el hilo y cierra el socket. Llamar en DEINITIALIZE_PLUGIN()
 * (o al menos antes de que el binario se descargue).
 */
void MobileNet_Shutdown();

/**
 * Copia el último estado recibido del celular asignado al canal
 * indicado (0-3) a `out`.
 * Devuelve true si hay un celular conectado en ese canal (llegó un
 * paquete válido hace menos de MOBILEPRO_TIMEOUT_MS).
 * Si devuelve false, `out` queda en ceros.
 */
bool MobileNet_GetState(int channel, MobileControllerState *out);

/**
 * true si hay un celular conectado en ese canal ahora mismo.
 * Util para WPADProbe/WPADGetDataFormat/WPADGetBatteryLevel, que
 * necesitan saber si hay "algo" sin pedir el estado completo.
 */
bool MobileNet_HasClient(int channel);

/**
 * Manda un comando de rumble al celular conectado en ese canal.
 * No hace nada si no hay ningún celular conectado ahí.
 */
void MobileNet_SendRumble(int channel, uint8_t enabled, uint8_t intensity, uint16_t durationMs);

/**
 * Corta la conexión de un canal a la fuerza (por si quedó "pegado"
 * y no querés esperar los MOBILEPRO_TIMEOUT_MS). El próximo paquete
 * que mande ese celular se va a tratar como cliente nuevo otra vez
 * (le vuelve a llegar el ack de conexión).
 */
void MobileNet_ForceDisconnect(int channel);

/**
 * Igual que MobileNet_ForceDisconnect pero para los 4 canales
 * de una sola vez.
 */
void MobileNet_ForceDisconnectAll();

#endif // MOBILEPRO_NET_H
