# Estado del proyecto

Última actualización: noviembre de 2026. Primera versión.

## Qué funciona

* **Arranque**: arranque HLE desde el descriptor de la cabecera de la ROM (`0x10000400 -> 0x0C010000`,
  3 MB, entrada `0x0C010000`). Logos, título, demo, monedas, selección de personaje, juego, tienda (STAR SHOP).
* **Partida**: un guion de pruebas sin ventana (60.000 fotogramas, unos 16 minutos de juego) recorre las fases
  1, 2 y 3 con sus jefes y la tienda entre fases.
* **Recompilación**: 27.456 bloques (26.754 del SH-4 principal, 702 del secundario) generados a partir de
  18.602 puntos de entrada. En el arranque + primera fase el 99,99 % de los bloques ejecutados son
  recompilados. Verificación en paralelo contra el intérprete (`-verify`) durante 6.000 fotogramas (arranque,
  selección e inicio de la fase 1): 328 millones de bloques iguales; la única diferencia es el signo de un NaN
  en el bloque `0C0E5540` (`fsub fr11,fr11` con un registro sin inicializar).
* **Sonido**: las dos placas AICA ejecutan el driver de Sega (música y efectos).
* **Controles**: palanca analógica (teclado con recorrido progresivo o stick del mando), disparo, misil,
  bomba, cambio de vista, start, moneda, test y service.
* **País**: Japón / EE. UU. / Exportación / Australia / Corea (byte 5 de la EEPROM del MIE). Con un chip en
  blanco se escriben los ajustes de fábrica antes de que la BIOS lo lea.

## Cambios respecto al motor de Star Wars Racer

* Placa de ROM de Planet Harriers: 8 EPROM de 4 MB (cuatro parejas, una por página de 16 MB) y 16 mask ROM
  de 16 MB (ocho parejas de 32 MB); clave 315-5881 `0x2912C68A`. Las páginas vacías ya no se leen como ceros
  (la BIOS compara páginas entre sí para localizar las EPROM).
* El juego exige la segunda placa de sonido: el bit 2 de la primera palabra de la página de sonido la anuncia
  (sin él: "ERROR !! NO AICA EXTENSION BOARD.").
* La BIOS deja a 1 el byte GBR+20 durante su arranque y el juego no cuenta monedas ni tiempo mientras siga
  así; el arranque HLE lo pone a 0 al entregar el control.
* Bucles de espera propios del juego (contador de vblank en `0C0125CE` y espera activa leyendo TCNT0 en
  `0C01C14A`): el 90 % de los ciclos se saltan.
* Procesador de comandos: niveles de detalle (005, 055, 095, 561 y saltos/llamadas condicionales; el juego
  elige así uno de tres modelos por objeto), matriz local (161 L), filas de matriz independientes.
* Capas 2D de 32 bits (A2BGR10) y bits de "capa con transparencia" del registro 0x1BC.
* Formato de textura 2 como intensidad de 4 bits (antes ABGR1111). Tablas de materiales y texturas con
  índices y bases de 16 bits.
* Tipos de polígono "fondo" (mezcla sin escribir profundidad) y sombras (opacos).

## Pendiente / fallos conocidos

* **Vulkan sin probar en GPU real con este juego.** El entorno de desarrollo sólo tiene el renderizador por
  software de pruebas; todas las capturas de verificación salen de él. Los binarios de Windows se compilan
  de forma cruzada y los prueba el usuario. Los shaders se han recompilado con otra versión de glslang.
* Capas 2D de 32 bits reducidas a 5 bits por canal (se nota en degradados del HUD y de los logos).
* El mezclador de fotogramas con una capa 2D como entrada (fundidos de los logos) no está hecho.
* **Texturas equivocadas en la segunda mitad de la fase 2** (agua y hojas con ruido de colores): el juego
  sustituye texturas en TEXRAM a mitad de fase (fotograma 25.077 del guion) y parte de la geometría sigue
  usando cabeceras de textura con el formato anterior. Causa sin localizar.
* Sesgo de profundidad (101) sin aplicar: calcomanías, halos y estrellas pueden parpadear.
* Desplazamiento de texturas (0D1) y modos U de combinación texel/material sin aplicar.
* La sombra del jugador usa el patrón de puntos 501; su color aún no es el correcto.
* Arranque con la BIOS real: se queda esperando tras las comprobaciones; se usa siempre el arranque HLE.
* Cobertura de bloques: fases 4 y 5, final y menú de test aún sin recorrer (el intérprete los cubre).
  Para ampliar: `hikaru_run -seen fichero` y añadir las direcciones a `builder/seeds_hikaru.txt`.
* Placa de comunicaciones (juego en red de dos muebles) ausente.
