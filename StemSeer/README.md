# StemSeer — extractor de stems + análisis de escala y BPM (VST3)

Plugin VST3 (y aplicación independiente) hecho con **JUCE 7 + C++17**.
Carga una canción, detecta **BPM** y **escala/modo** y la separa en 11 stems:

| Grupo | Stems |
|---|---|
| Drums | Kick, Bass, Hi-hat, Percusión |
| Melodías | Guitarras, Pianos, Strings, Flautas, Pads |
| Extras | Voces, FX / Otros |

Reproduce con solo (S) por grupo, volumen por grupo, silenciar un stem (clic en su nombre),
clic/arrastre sobre la forma de onda para moverte, y exporta a **WAV o FLAC** (16 o 24 bits).

---

## 1. Compilar en Windows 10

Necesitas instalar (una sola vez):

1. **Visual Studio 2022 Community** (o *Build Tools*) con la carga de trabajo **"Desarrollo para el escritorio con C++"**.
2. **CMake** ≥ 3.22 — https://cmake.org/download/ (marca "Add to PATH").
3. **Git** — https://git-scm.com/download/win

Luego, dentro de la carpeta del proyecto, haz doble clic en **`build_windows.bat`**
(o ejecuta los comandos manualmente):

```
cmake -S . -B build -G "Visual Studio 17 2022" -A x64
cmake --build build --config Release --parallel 2
```

La primera vez descarga JUCE (~150 MB), así que necesitas internet.
Con 4 GB de RAM la compilación tarda unos minutos; cierra otros programas pesados.

Resultado:

* `build\StemSeer_artefacts\Release\VST3\StemSeer.vst3`  → copia esa carpeta a `C:\Program Files\Common Files\VST3\`
* `build\StemSeer_artefacts\Release\Standalone\StemSeer.exe` → para probar sin DAW.

> Si tienes Visual Studio 2019 usa `-G "Visual Studio 16 2019"`.

## 2. Uso

1. Abre el plugin (en un DAW cárgalo en una pista de instrumento; es un generador de audio).
2. **Abrir archivo** → elige WAV / MP3 / FLAC / AIFF / OGG.
3. Espera a que termine el análisis (barra de progreso). Verás BPM, escala, modo y los 11 stems.
4. **Reproducir**, solos, volúmenes, etc.
5. **Exportar stems** → elige carpeta, formato y calidad.

La reproducción es interna del plugin (no sigue el transporte del DAW).
Para llevar los stems a tu proyecto, exporta y arrastra los archivos al DAW.

## 3. Rendimiento en tu PC (2.6 GHz, 4 GB RAM)

* No usa redes neuronales: es separación espectral con máscaras suaves, pensada para equipos modestos.
* Estimación: una canción de 3–4 min tarda **~30–60 s** en procesarse y usa **~300–500 MB** de RAM.
* Los stems se escriben al vuelo a una carpeta temporal (`%TEMP%\StemSeer`) y se borran al cerrar.
* Límite: 15 minutos por archivo.

## 4. Estructura

```
Source/Core/Fft.h          FFT radix-2
Source/Core/Analysis.*     BPM (flujo espectral + autocorrelación) y escala (cromagrama + Krumhansl)
Source/Core/Separator.*    Separación (HPSS + bandas + imagen estéreo), streaming, sin JUCE
Source/PluginProcessor.*   Carga, hilos de trabajo, reproducción, exportación
Source/PluginEditor.*      Interfaz (basada en tu diseño)
Tests/core_test.cpp        Prueba del núcleo con una canción sintética
```

Probar solo el núcleo (sin JUCE, rápido):

```
cmake -S . -B build_core -DSTEMSEER_CORE_ONLY=ON
cmake --build build_core --config Release
build_core\Release\stemseer_core_test.exe 128
```

## 5. Limitaciones honestas

* **BPM y escala**: fiables en música con pulso claro. Pueden confundir el doble/mitad de tempo
  (p. ej. 174 ↔ 87) o los tresillos; la escala distingue mayor/menor y los modos
  Jónico, Dórico, Frigio, Lidio, Mixolidio y Eólico, pero no cambios de tonalidad dentro del tema.
  El botón **Tap** permite corregir el BPM a mano (solo afecta a lo mostrado).
* **Separación**: Kick, Bass, Hi-hat, Percusión y Voces centradas funcionan razonablemente.
  Guitarras, Pianos, Strings, Flautas y Pads se reparten por **heurística** (banda de frecuencias,
  sostenido vs. percutido, ancho estéreo), no por reconocimiento de instrumento: habrá "sangrado" entre ellos.
  Ningún modelo público separa de forma fiable strings/flautas/pads; para mejorar la calidad
  se podría sustituir `Separator.cpp` por un modelo ONNX (Demucs/BS-Roformer), pero necesita mucha más RAM/CPU que tu equipo.
* **MP3**: se puede *leer*, pero no exportar (JUCE no incluye codificador MP3).
* La suma de los 11 stems reconstruye la mezcla original (error < -100 dB en las pruebas).

## 6. Licencia

JUCE se distribuye bajo AGPLv3 o licencia comercial. Para uso personal no hay problema;
si vas a distribuir el plugin, revisa https://juce.com/legal/juce-8-licence/

## 7. Compilar con GitHub (sin instalar nada en tu PC)

1. Crea un repositorio en GitHub y sube todo el contenido de esta carpeta (incluida `.github`).
2. Ve a la pestaña **Actions** → "Compilar StemSeer (Windows)" → **Run workflow** (también corre solo en cada push).
3. Al terminar (10–20 min la primera vez), abre la ejecución y descarga **StemSeer-Windows** en "Artifacts".
4. Descomprime y copia `StemSeer.vst3` a `C:\Program Files\Common Files\VST3\`.
