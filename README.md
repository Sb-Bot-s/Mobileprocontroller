# Mobile Pro Controller (MPC)

Plugin [WUPS](https://github.com/wiiu-env/WiiUPluginSystem) para Wii U (entorno Aroma) que emula un **Pro Controller** usando un celular o PC como mando, conectado por WiFi.

A diferencia de proyectos como `hid_to_vpad` (limitado a lo que el Controller Patcher permitía), MPC habla un protocolo UDP propio directo con el plugin, lo que permite soporte de rumble real, varios jugadores simultáneos, y clientes en distintas plataformas sin depender de USB ni de un adaptador HID intermedio.

Clientes oficiales del protocolo (repos separados): app de Android, cliente de PC.

## Características

- Emula un Pro Controller completo: los 17 botones reales (`WPAD_PRO_BUTTON_*`), incluidos los clicks de stick
- Sticks analógicos de 16 bits por eje
- Rumble: la Wii U le manda vibración de vuelta al dispositivo que esté conectado
- Hasta **4 controles simultáneos** (uno por dispositivo, cada uno elige su canal 0-3) — no hace falta configurar nada a mano, se detecta solo
- Confirmación de conexión: el dispositivo sabe al instante que quedó conectado y a qué canal
- Desconexión forzada desde el menú del plugin, por si un canal queda "pegado"

## Cómo funciona

```
┌─────────────┐         UDP :9100            ┌───────────────────────┐
│  Dispositivo│ ──── MobileControllerState ──▶│                       │
│  (celular / │◀──── MobileConnAck ───────────│   Plugin WUPS          │
│     PC)     │◀──── MobileRumbleCommand ─────│  (Mobile Pro Controller)│
└─────────────┘                               │                       │
                                               │  intercepta            │
                                               │  WPADRead/KPADReadEx   │
                                               │  vía function patching │
                                               └───────────────────────┘
```

El plugin usa *function patching* de WUPS para interceptar las llamadas que hace un juego a `WPADRead` y `KPADReadEx` (las funciones del sistema que leen el estado de los mandos), y en vez de devolver el estado real, devuelve el último estado recibido por red de un dispositivo conectado en ese canal. El juego no se entera de la diferencia — para él es un Pro Controller real.

Un hilo aparte, corriendo dentro del plugin, escucha UDP en el puerto **9100** y mantiene un slot de estado por cada uno de los 4 canales posibles.

## Requisitos

- Consola con Aroma instalado
- Módulos de Aroma: `WiiUPluginLoaderBackend`, `CURLWrapperModule`, `FunctionPatcherModule`, `NotificationModule`
- Para compilar: Docker, con la imagen `ghcr.io/wiiu-env/devkitppc:20260504` (o más nueva) y las librerías `libnotifications`, `libfunctionpatcher`, `libkernel`, `libmocha`, `wiiumodulesystem`, `wiiupluginsystem`, `libcurlwrapper`

## Instalación

1. Descargá `MobilePro.wps` de la última [release](#)
2. Copialo a `sd:/wiiu/environments/aroma/plugins/`
3. Reiniciá la consola / entrá a Aroma
4. Abrí el menú de plugins (**L + DPAD Down + Minus**), activá **"Enable Mobile Pro Controller"**
5. Conectá tu celular o corré el cliente de PC en la misma red WiFi que la Wii U

## Compilar desde cero

```bash
docker build . -t builder
docker run --rm -v ${PWD}:/app -w /app builder
```

El `.wps` compilado queda en `dist/`.

## El protocolo

Todo el tráfico va por UDP al puerto **9100**, en **network byte order (big-endian)**. La Wii U es PowerPC (big-endian nativo); si programás un cliente en una plataforma little-endian (Android/ARM, x86 de PC), tenés que armar los paquetes en big-endian explícitamente (`ByteBuffer.order(ByteOrder.BIG_ENDIAN)` en Java/Kotlin, `struct.pack(">...")` en Python).

### `MobileControllerState` — celular/PC → Wii U (17 bytes)

| Offset | Campo | Tipo | Descripción |
|---|---|---|---|
| 0-3 | `buttons` | `uint32` | Máscara de botones, valores reales de `WPAD_PRO_BUTTON_*` |
| 4-5 | `leftStickX` | `int16` | -32768 a 32767 |
| 6-7 | `leftStickY` | `int16` | -32768 a 32767 |
| 8-9 | `rightStickX` | `int16` | -32768 a 32767 |
| 10-11 | `rightStickY` | `int16` | -32768 a 32767 |
| 12 | `battery` | `uint8` | 0-100 |
| 13 | `flags` | `uint8` | bit 0: conectado, bit 1: cableado |
| 14 | `channel` | `uint8` | 0-3, qué jugador quiere ser este dispositivo |
| 15-16 | `magic` | `uint16` | `0x4D50` ('MP'), valida que el paquete es nuestro |

Se manda a razón de ~60 paquetes por segundo mientras el cliente está activo (fire-and-forget, sin ack por paquete).

### `MobileConnAck` — Wii U → celular/PC (4 bytes)

Se manda **una sola vez**, cuando la Wii U detecta un paquete válido de un cliente nuevo (primera conexión, o reconexión tras un timeout).

| Offset | Campo | Tipo | Descripción |
|---|---|---|---|
| 0-1 | `magic` | `uint16` | `0x4D50` |
| 2 | `status` | `uint8` | `1` = conectado |
| 3 | `channel` | `uint8` | canal que la Wii U le asignó |

### `MobileRumbleCommand` — Wii U → celular/PC (6 bytes)

Se manda cuando el juego pide vibración en ese canal.

| Offset | Campo | Tipo | Descripción |
|---|---|---|---|
| 0-1 | `magic` | `uint16` | `0x4D50` |
| 2 | `enabled` | `uint8` | 1 = vibrar, 0 = parar |
| 3 | `intensity` | `uint8` | 0-255 |
| 4-5 | `durationMs` | `uint16` | duración en milisegundos |

### Botones (`MP_BTN_*`)

Copiados 1:1 del enum real `WPADProButton` (`padscore/wpad.h`), no son valores arbitrarios:

```
MP_BTN_UP      0x00000001    MP_BTN_R       0x00000200
MP_BTN_LEFT    0x00000002    MP_BTN_PLUS    0x00000400
MP_BTN_ZR      0x00000004    MP_BTN_HOME    0x00000800
MP_BTN_X       0x00000008    MP_BTN_MINUS   0x00001000
MP_BTN_A       0x00000010    MP_BTN_L       0x00002000
MP_BTN_Y       0x00000020    MP_BTN_DOWN    0x00004000
MP_BTN_B       0x00000040    MP_BTN_RIGHT   0x00008000
MP_BTN_ZL      0x00000080    MP_BTN_RSTICK  0x00010000
                              MP_BTN_LSTICK  0x00020000
```

### Timeout / desconexión

Si un canal no recibe un paquete válido durante **500ms**, se considera desconectado automáticamente. El próximo paquete que llegue (de esa IP o de otra) se trata como una conexión nueva y dispara un `MobileConnAck` de nuevo.

## Multijugador

Cada celular/PC elige su canal (0-3) en el campo `channel` de cada paquete — normalmente eligiéndolo en la pantalla de conexión de la app. La Wii U mantiene un estado independiente por canal, así que podés tener hasta 4 dispositivos jugando a la vez sin configurar nada del lado del plugin: se detecta solo con que cada uno mande un canal distinto.

## Menú de configuración

Accedé con **L + DPAD Down + Minus** dentro de un juego:

- **Enable Mobile Pro Controller** — activa/desactiva el plugin
- **Force Disconnect All Controllers** — corta todas las conexiones activas al toque, sin esperar el timeout de 500ms. Útil si un canal quedó "pegado" o querés soltar el lugar para otro dispositivo ya. No queda guardado entre aperturas del menú.

## Limitaciones conocidas

- **Sin autenticación**: el único chequeo es un magic number fijo (`0x4D50`). Cualquier dispositivo en la misma red WiFi podría mandar paquetes falsos. El tráfico nunca sale de la LAN (no hay forwarding a internet), así que el riesgo se limita a alguien que ya esté conectado a tu red — mismo modelo de riesgo que tienen otras herramientas homebrew de Wii U sin auth (ej. FTPiiU)

## Créditos

Inspirado conceptualmente en `hid_to_vpad`, con un protocolo y arquitectura propios pensados específicamente para conexión de red en vez de HID/USB.

## Licencia
#MIT
