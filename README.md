# Mobile Pro Controller — Plugin WUPS para Wii U

Plugin que permite usar un móvil (Android/iOS) como **Wii U Pro Controller** vía WiFi.

## Fase 1: Prueba estática (actual)

El plugin emula un Pro Controller en el canal configurado con:
- **Botón A siempre presionado**
- Sticks centrados
- Batería al 100%

Esto sirve para verificar que el patching de funciones funciona correctamente antes de agregar networking.

## Fase 2: Conexión con móvil (próxima)

Se agregará un servidor UDP en la Wii U que recibe el estado del control desde una app móvil.

---

## Requisitos

- Wii U con **Aroma** instalado
- SD card con acceso a `sd:/wiiu/environments/aroma/plugins/`
- **Docker** (para compilar sin instalar devkitPro)

## Compilación con Docker

### Paso 1: Build de la imagen (solo la primera vez)

```bash
docker build . -t mobileprocontroller-builder
```

### Paso 2: Compilar el plugin

```bash
docker run --rm -v "$PWD":/project mobileprocontroller-builder
```

O en un solo paso:

```bash
make docker-run
```

### Limpiar

```bash
make clean          # Borra build/ y .wps
make docker-clean   # Borra la imagen Docker
```

## Instalación

1. Copiar `mobileprocontroller.wps` a:
   ```
   sd:/wiiu/environments/aroma/plugins/mobileprocontroller.wps
   ```
2. Encender la Wii U con Aroma.
3. Abrir el menú de plugins: **L + D-Pad Down + Minus**
4. Configurar:
   - **Enable**: Activar el plugin
   - **Emulated Channel**: Canal Wiimote a ocupar (0-3)
5. Iniciar un juego.

## Prueba de Fase 1

Con el plugin activado en el **canal 0**:
- Inicia un juego que soporte Pro Controller (ej. *Mario Kart 8*, *Super Smash Bros.*)
- El juego debería detectar un Pro Controller conectado
- El botón A estará "presionado" constantemente (útil para verificar que funciona)
- Para salir de menús donde A se queda presionado, desactiva el plugin temporalmente

## Arquitectura del plugin

Basado en `gamepadtopro` de capitalistspz. Intercepta estas funciones de `padscore.rpl`:

| Función | Qué hace el plugin |
|---|---|
| `KPADReadEx` | Devuelve datos de Pro Controller (API de alto nivel) |
| `KPADRead` | Wrapper de KPADReadEx |
| `WPADProbe` | Reporta `WPAD_EXT_PRO_CONTROLLER` en nuestro canal |
| `WPADRead` | Devuelve datos en formato `WPADStatusProController` |
| `WPADControlMotor` | Captura rumble (Fase 2: enviar al móvil) |
| `WPADGetDataFormat` | Reporta formato Pro Controller |
| `WPADGetBatteryLevel` | Devuelve batería simulada |

## Próximos pasos (Fase 2)

1. Agregar servidor UDP en la Wii U (puerto 9100)
2. Crear app Android que envíe `MobileControllerState` por UDP
3. Reemplazar `FillStaticProControllerData_*` por datos de red
4. Agregar vibración (rumble) del móvil

## Licencia

MIT
