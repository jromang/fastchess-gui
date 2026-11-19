# Guía de usuario de fastchess-gui

Esta guía describe cada página, ajuste y archivo de fastchess-gui. El [README](../README.md) explica qué es el
programa, dónde descargarlo y cómo compilarlo. En el programa, F1 abre esta guía junto a la página, en la sección de
lo que está bajo el ratón, en el idioma de la interfaz (consulta [La ayuda del programa](#la-ayuda-del-programa)).

## Inicio rápido
<!-- en: quick-start 92353f2c -->

### Tu primer torneo
<!-- en: your-first-tournament 75e9a891 -->

1. **Motores.** En la barra de la izquierda, *Motores*, luego *Añadir…*, y elige el programa de un motor (un
   ejecutable UCI o WinBoard): se arranca una vez para leer su nombre y sus opciones. *Importar…* > *Una lista de
   motores…* toma los motores de otro programa (cutechess, Banksia, Arena, ChessGUI), *Importar…* > *Todos los
   motores de una carpeta…* todos los programas de una carpeta. *Comprobar* indica si un motor funciona.
2. **Nuevo torneo.** *Torneos* > *Nuevo torneo*. Elige un *Formato* en su lista (*Todos contra todos*,
   *Gauntlet*…) y un *Ritmo de juego* (*10+0.1* para una primera prueba), y deja las demás filas como están.
3. **Participantes.** Haz doble clic en los motores de la *Biblioteca*, junto a los participantes, o selecciona
   varios y pulsa Intro.
4. **Iniciar.** El torneo se abre en su espacio de trabajo: *Vista general* para la clasificación y el tiempo
   restante, *Directo* para las partidas en juego.

El resumen, a la derecha de la configuración, indica cuántas partidas se jugarán y cuánto durarán, y qué fallaría (un
programa no encontrado, demasiados hilos, un libro que falta), cada cosa con un botón que la arregla.

### Probar una nueva versión de un motor
<!-- en: testing-a-new-version-of-an-engine 300dc828 -->

1. Añade ambas versiones a la biblioteca.
2. *Nuevo torneo*, *Formato*: *SPRT mejora [0, 3]* (¿gana fuerza la nueva versión?) o *SPRT no regresión [-5, 0]*
   (¿no es más débil?).
3. Elige la nueva versión como **A** y la antigua como **B**, un ritmo de juego corto (*10+0.1*) y, en *Aperturas*,
   un libro de posiciones equilibradas (EPD); luego, **Iniciar**.

El match se detiene solo en cuanto el SPRT decide: H1 aceptada (A es más fuerte, según los límites) o H0 aceptada (no
lo es). La *Vista general* muestra el Elo, el LLR y sus límites a medida que llegan las partidas. La configuración se
conserva: la próxima vez, elige la siguiente versión como A y pulsa **Iniciar**, o haz clic en *Relanzar con…* en el
match terminado.

### Qué leer después
<!-- en: where-to-go-next 2cd9b9f7 -->

- [Nuevo torneo](#nuevo-torneo): todos los ajustes, y [consejos](#consejos-para-los-ajustes) para elegirlos;
- [Un torneo en curso](#un-torneo-en-curso): qué muestran las pestañas, pausar, modificar un torneo mientras se
  juega;
- [Clasificaciones](#clasificaciones): una lista de rating sobre todas las partidas jugadas;
- [Retransmisión](#retransmisión): las partidas en directo en una página web, en TLCV o en lichess;
- [Solución de problemas](#solución-de-problemas) cuando algo va mal, y el [Glosario](#glosario) para las palabras de
  los testers.

## La ventana
<!-- en: the-window 3a5230d4 -->

### Las páginas
<!-- en: the-pages b13b4de2 -->

Una barra de navegación a la izquierda lleva a ocho páginas:

| Página | Contenido |
|---|---|
| Torneos | La página de inicio: lo que está en marcha y lo que requiere atención, la cola, los últimos torneos terminados, todos los torneos del perfil, *Nuevo torneo* |
| Motores | La biblioteca de motores: opciones, comprobaciones, grupos, Elo de referencia, importación de las listas de otros programas |
| Libros | La biblioteca de libros de aperturas, y las aperturas de cada libro con lo que tienen de malo |
| Tablero | Una partida o una posición de cualquier procedencia, analizada por un motor de la biblioteca |
| Suites | Suites de tests (STS, WAC, ERET…): motores frente a las posiciones de un archivo EPD |
| Clasificaciones | Una lista de rating sobre todas las partidas de todos los torneos |
| Registro | La salida propia de fastchess y los mensajes del programa, e *Informar de un problema…* |
| Ajustes | Idioma, tema, tamaño de la interfaz, notificaciones, la apariencia de los tableros, las carpetas del perfil y de torneos, las máquinas remotas, el token de lichess, la versión, la forma en que se dibuja la ventana |

### La ayuda del programa
<!-- en: help-in-the-program 351be20d -->

- **Descripciones emergentes.** Deja el ratón sobre un control para ver qué hace. La descripción emergente de un
  control que esta guía explica más a fondo termina con *F1: más en la guía*.
- **F1** abre esta guía en un panel a la derecha de la página, en la sección de lo que está bajo el ratón (una fila de
  la configuración, una tarjeta de la página de inicio, una pestaña, una columna de la clasificación); si no hay nada
  bajo el ratón, en la sección del último panel en el que se hizo clic, y si no, en la de la página. El **?** redondo
  junto al título de una página abre la sección de esa página. F1 otra vez, o Esc en el panel, lo cierra.
- **El panel de la guía** muestra una sección con sus subsecciones, y la página de al lado sigue funcionando: puedes
  leer la explicación mientras cambias el ajuste. Su cabecera tiene *Atrás* y *Adelante* (también Alt+Izquierda y
  Alt+Derecha, o los botones laterales del ratón), *Índice* (todas las secciones), el campo de búsqueda (Ctrl+F en
  el panel) y *Abrir en el navegador* (esta guía en GitHub, para la versión en uso). La búsqueda encuentra todas las
  palabras escritas, en cualquier orden, sin distinguir mayúsculas ni acentos; los títulos van primero, y las
  coincidencias se resaltan. Los enlaces a otras secciones se abren en el panel, los enlaces a páginas web en el
  navegador, y un bloque de código tiene *Copiar*. Arrastra el borde izquierdo del panel para ensancharlo; su anchura
  se conserva.
- **Idiomas.** La guía está integrada en el programa, en todos los idiomas de la interfaz, y corresponde a la versión
  en uso; no se descarga nada de la red. Una sección cuya traducción falta o es más antigua que el texto en inglés se
  muestra en inglés, con una línea que lo indica. Los archivos comprimidos de cada versión también la incluyen
  (`guide.md`, y `guide.<language>.md` para cada traducción).
- **Ctrl+K**, la paleta de comandos, encuentra las secciones de la guía por sus títulos (*Guía: …*), y *Buscar «…»
  en la guía* busca en ella lo que se ha escrito.
- **Mensajes.** Un problema que el programa notifica nombra el motor, el archivo o el recurso, con su solución a
  mano; un **?** a su lado abre su entrada en [Solución de problemas](#solución-de-problemas).
- **Primeros pasos.** Hasta que se inicia un primer torneo, la página de inicio muestra los pasos del
  [Inicio rápido](#inicio-rápido), cada uno con su botón, marcado una vez hecho; *Ocultar* los quita de la vista.
- [Informa de un problema](#informar-de-un-problema) cuando el programa no haga lo que debería.

### La paleta de comandos
<!-- en: the-command-palette 725e7934 -->

**Ctrl+K** abre la paleta de comandos: las páginas y pestañas, los motores de la biblioteca (abre su ficha), los
libros, los torneos de la página de inicio, las secciones de la guía y las acciones posibles en ese momento (*Pausa*,
*Reanudar*, *Pausar todo*, *Detener*, *Iniciar*, *Añadir a la cola*, *Relanzar … con…*, las entradas de
*Importar…*, *Comprobar los no comprobados*, *Exportar para una lista de rating…*, *Copiar* la clasificación,
*Copiar la versión*, *Informar de un problema…*, los colores del tablero y los juegos de piezas, los sonidos de las
jugadas activados o desactivados…). Las mayúsculas no importan y las letras escritas no tienen por qué ser contiguas
(«lr» encuentra *Lista de rating*); los nombres que empiezan por ellas, o que tienen una palabra que empieza por
ellas, van primero. Arriba y Abajo eligen, Intro ejecuta, Esc cierra; Ctrl+K no abre la paleta mientras se escribe en
un campo de texto.

### Idioma, tema y tamaño
<!-- en: language-theme-and-size 954fe416 -->

La interfaz está en inglés, francés, alemán, español, italiano, portugués de Brasil, ruso o chino simplificado. Al
iniciar sigue el primer idioma preferido del sistema (inglés si no hay traducción para él); se puede forzar en
**Ajustes**. El chino usa una fuente del sistema: Microsoft YaHei en Windows, Noto Sans CJK o WenQuanYi en Linux. El
tema es claro salvo que se elija el oscuro, o el del sistema, en **Ajustes**.

Las traducciones al español, italiano, portugués, ruso y chino son nuevas: las correcciones de jugadores de esos
idiomas son bienvenidas. Las de la interfaz son simples archivos `.po` en `gui/i18n`, que abren Poedit y programas
similares; las de esta guía son `docs/guide.<language>.md`.

Las páginas usan toda la ventana, sea cual sea su tamaño: los tableros de la pestaña Directo crecen con ella, el
formulario de nuevo torneo se divide en dos columnas en una ventana ancha, y la lista de las páginas Motores, Libros y
Clasificaciones puede ensancharse arrastrando su borde derecho. Todo (texto, botones, tableros) puede además hacerse
más grande o más pequeño, del 80 al 250 %: **Ajustes** > Tamaño de la interfaz, o Ctrl + rueda del ratón, Ctrl + y
Ctrl -, y Ctrl 0 para volver al 100 %. F11 pone la ventana a pantalla completa, y la devuelve a su tamaño.

Una insignia en la pestaña *Directo* y en el botón *Torneos* da el número de partidas en curso. Un campo numérico
acepta aritmética: `40*60`, `15+10`, con `+ - * /` y paréntesis, calculada al pulsar Intro o al salir del campo; Esc
devuelve al campo su valor anterior.

### Los tableros y sus botones
<!-- en: boards-and-their-buttons 1ed997f4 -->

Bajo cada tablero que muestra una partida (la reproducción de la pestaña *Partidas*, la partida en primer plano en
*Directo*, un tablero separado, la página Tablero), una fila de botones pequeños va a la posición inicial, a la
jugada anterior, a la siguiente y a la última, y gira el tablero; sus descripciones emergentes indican las teclas, que
hacen lo mismo: ← → Inicio Fin, y F para girar. Quien prefiera las teclas puede ocultar los botones con **Ajustes** >
*Apariencia* > *Botones de navegación bajo los tableros*, con un clic derecho sobre ellos o con la paleta de comandos
(*Ocultar los botones de navegación bajo los tableros*, y *Mostrar…* para recuperarlos). La rueda del ratón sobre una
lista de jugadas desplaza la lista, nunca las jugadas. **A** en cualquier tablero abre su posición en la página
[Tablero](#tablero) y la analiza.

### Versión y nuevas versiones
<!-- en: version-and-new-versions 70e3e9a2 -->

La versión está en el título de la ventana y en **Ajustes**, cuyo *Copiar* la pone en el portapapeles junto con el
sistema, para un informe de error. *Avisarme cuando salga una versión nueva* (desactivado por defecto) pregunta a
GitHub por la última versión publicada al iniciar, como mucho una vez al día y en segundo plano; cuando es más
reciente, la página de inicio lo indica, con *Página de descarga* (la página de esa versión en GitHub, en el
navegador), *Novedades* (sus notas) y *Ocultar* (hasta la próxima versión). El programa no descarga ni instala nada,
y un fallo (sin red, GitHub inaccesible) solo se escribe en el registro.

### Informar de un problema
<!-- en: reporting-a-problem 3e4e5989 -->

*Informar de un problema…* (en **Ajustes**, en la página **Registro**, en la paleta de comandos y en el mensaje de
un fallo al iniciar) abre una nueva incidencia en GitHub en el navegador, a partir del formulario de informe de
errores, ya rellenada con la versión, el sistema (sistema operativo, procesador, memoria, tarjeta gráfica y
controlador) y las últimas líneas del registro. Léela antes de enviarla, quita lo que no quieras mostrar (rutas,
nombres de motores) y explica qué hiciste, qué esperabas y qué ocurrió. El programa no envía nada por sí mismo. Las
ideas van al mismo sitio: *New issue*, *Idea*.

### Tamaño y posición de la ventana
<!-- en: window-size-and-place 892d41c4 -->

La ventana se abre como se dejó: su tamaño, su posición en las pantallas, maximizada o no. Cuando la pantalla en la
que estaba ya no está (un monitor desconectado), se abre en el centro de la principal; cuando su tamaño ya no cabe en
la pantalla (una resolución menor), con el tamaño por defecto. Los paneles sacados de ella, como un tablero separado,
también vuelven a donde estaban (`layout.ini`), y a la ventana cuando su pantalla ya no está. Wayland no permite que
un programa elija la posición de su ventana: allí solo se recuperan el tamaño y el estado maximizado.

### Cómo se dibuja la ventana
<!-- en: drawing-the-window caff7bef -->

La ventana se dibuja con OpenGL 3.2. Donde no lo hay (una máquina virtual sin aceleración 3D, un escritorio remoto, un
servidor antiguo), el programa la dibuja en su lugar con el renderizador propio de SDL: Direct3D 11 en Windows,
OpenGL 2.1, o solo el procesador. El registro dice cuál, y por qué no se usó OpenGL; **Ajustes** > Renderizado
también lo muestra, con el nombre de la tarjeta gráfica cuando dibuja OpenGL. *Renderizado por software*, ahí (o
`--software` en la línea de comandos, para un solo inicio), hace que el procesador dibuje la ventana desde el próximo
inicio, para un controlador gráfico que falla o se cuelga. Usa más tiempo de procesador, solo vuelve a dibujar lo que
cambia (y lo que se mueve, como los relojes, 15 veces por segundo en lugar de 30 mientras el ratón y el teclado están
quietos), dibuja esquinas rectas, y los paneles del espacio de trabajo flotan dentro de la ventana principal en lugar
de ir a otra pantalla. En Wayland (y en macOS) la imagen del procesador se sigue mostrando a través del controlador
gráfico; si no lo hay, inicia el programa con `SDL_VIDEODRIVER=x11`. Cuando ninguna forma de dibujar funciona, un
mensaje lo indica antes de que el programa termine.

### Apariencia
<!-- en: appearance decda352 -->

**Ajustes** > *Apariencia* elige el aspecto de todos los tableros del programa (el mosaico en directo y la partida
seguida, los tableros separados, la pantalla grande, una partida reproducida en *Partidas*, la pestaña *Aperturas*,
las aperturas de un libro), con un pequeño tablero junto a las opciones que las muestra:

![Ajustes > Apariencia: el tablero marrón y las piezas Celtic](screenshots/appearance.png)

- **Tablero**: los colores de las casillas, para el tema claro y para el tema oscuro por separado (se dibuja el
  tablero del tema en uso). Un esquema predefinido (*Gris azulado*, los colores de las versiones anteriores;
  *Marrón*; *Azul*; *Verde*; *Gris*), o *Personalizado*: un clic en *Casillas claras*, *Casillas oscuras* o *Última
  jugada* abre un selector de color, y cambiar un color convierte el tablero en uno personalizado a partir de los
  colores que tenía. *Última jugada* se superpone a las dos casillas de la última jugada, con una transparencia que
  deja ver el color de la casilla.
- **Piezas**: un juego integrado en el programa (*Cburnett*, el predeterminado; *Chessnut*; *Celtic*; *Fantasy*;
  sus autores y licencias en la descripción emergente y en `ATTRIBUTION.md`), o *Tu propia carpeta…*: una carpeta
  de 12 imágenes SVG llamadas `wK.svg`, `wQ.svg`, `wR.svg`, `wB.svg`, `wN.svg`, `wP.svg` y `bK.svg` … `bP.svg` (rey,
  dama, torre, alfil, caballo y peón, blancos y negros). Las imágenes se dibujan con nanosvg, como las integradas:
  formas simples, rellenos, trazos y degradados; sin texto, filtros ni imágenes incrustadas. Cuando un archivo falta,
  ocupa más de 1 MB o no es una imagen que se pueda dibujar, la página dice cuál y por qué, mientras tanto se dibujan
  las piezas por defecto, y *Volver a leer* lee la carpeta una vez arreglados los archivos. La página web de la
  retransmisión conserva las piezas por defecto.
- **Sonidos de las jugadas** (desactivados por defecto): *Un sonido por cada jugada* reproduce un golpe corto en cada
  jugada de la partida seguida, en *Directo* y en la pantalla grande, y uno más pesado en una captura; no en todas
  las partidas del mosaico. *Volumen*, y *Probar una jugada* / *Probar una captura* para oírlos. Los sonidos los
  genera el programa (ningún archivo de sonido); el dispositivo de sonido solo se abre mientras están activados, y
  cuando no hay ninguno el registro dice por qué y el programa permanece en silencio.

- **Figuras en la notación** (activado por defecto): las jugadas de la página Tablero, en su lista de jugadas y en las
  líneas de su análisis, se escriben con las piezas del juego elegido arriba (Nf3 pasa a ser un caballo y f3);
  desactivado, con letras.

La paleta de comandos también tiene estas opciones (*Colores del tablero: Marrón*, *Piezas: Celtic*, *Activar los
sonidos de las jugadas*…). Se guardan en `settings.json`, en el perfil; en el modo portátil, la carpeta de piezas se
guarda relativa a la carpeta del programa.

**Logos de los motores**: la ficha de un motor en la página **Motores** tiene *Logo* > *Elegir…*, una imagen PNG,
JPEG o SVG de 2 MB como máximo (4096 píxeles de lado como máximo), mostrada junto al nombre del motor en *Directo*, en
la pantalla grande y en la página web de la retransmisión. La imagen se copia en la carpeta `logos` del perfil, así
que el original puede moverse o borrarse y el perfil trasladarse entero; *Quitar* la retira (y su copia, salvo que
otro motor la tenga). El logo puede cambiarse mientras el motor juega.

### El tablero 3D
<!-- en: the-3d-board cc0dd596 -->

**Ajustes** > *Apariencia* > *Tablero 3D* > *El tablero de la página Tablero en 3D* dibuja el tablero de la página
[Tablero](#tablero) en tres dimensiones, como la fotografía de un tablero real (desactivado por defecto; todos los
demás tableros siguen planos). La tarjeta gráfica lo calcula por trazado de caminos: la imagen se afina durante unos
segundos mientras nada se mueve, unos milisegundos de la tarjeta en cada fotograma, y luego la tarjeta descansa;
mientras el tablero gira, una imagen más tosca ocupa su lugar. El pequeño tablero de *Apariencia* se dibuja entonces
también en 3D, para que las opciones de al lado se vean a medida que cambian:

- la iluminación, como los fotógrafos iluminan un bodegón: *Estudio*, *Clave baja*, *Clave alta*, *Sala de
  exposición*, *Luz de ventana*, *Hora dorada* o *Sala de torneo*;
- *Exposición*: más claro o más oscuro de lo que lo deja la iluminación, en pasos, cada uno duplica o reduce a la
  mitad la luz;
- *Calidad*: las muestras de las que se compone la imagen antes de descansar, 512 por defecto; menos alivian los
  gráficos de un procesador durante los torneos;
- *Barniz*, de una madera encerada y mate a una capa en la que se ven las luces, y *Brillo*, de satinado a espejo;
- *Por defecto* devuelve la exposición, la calidad, el barniz y el brillo a como estaban; la iluminación se mantiene.

Lo que muestra el tablero plano está en él: la barra de evaluación, la última jugada, la pieza tomada y adónde puede ir,
un rey en jaque, las flechas del análisis, que se ven tenuemente a través de las piezas, la respuesta que esperaba el
motor que jugó la jugada, en trazos, el juicio de la jugada como insignia sobre su casilla, y el marco de una línea
mostrada en lugar de la partida. Un clic izquierdo elige una casilla como en el tablero plano, sobre una pieza o sobre
el tablero detrás de ella, y una pieza se puede arrastrar, con su fantasma siguiendo al puntero (ver
[Tablero](#tablero)). Si mantienes pulsado el botón derecho mientras mueves el ratón, la cámara gira alrededor del
tablero (un clic derecho sin moverse sigue soltando la pieza tomada), la rueda acerca, y un clic de la rueda devuelve la
vista; girar el tablero (F) hace que la cámara dé media vuelta.

El tablero 3D necesita OpenGL 3.3. Donde la ventana se dibuja sin él (el renderizador por software de
[Cómo se dibuja la ventana](#cómo-se-dibuja-la-ventana), algunas máquinas virtuales y escritorios remotos), los
tableros siguen planos y *Apariencia* dice por qué. Las piezas son las Staunton-Pieces de clarkerubber, bajo la
licencia MIT; *Apariencia* enlaza con ellas.

## Atajos de teclado
<!-- en: keyboard-shortcuts e0d74f26 -->

| Teclas | Dónde | Qué hacen |
|---|---|---|
| <kbd>Ctrl</kbd>+<kbd>K</kbd> | En todas partes | La paleta de comandos |
| <kbd>F1</kbd> | En todas partes | Esta guía, en la sección de lo que está bajo el ratón; otra vez: la cierra |
| <kbd>F11</kbd> | En todas partes | Pantalla completa, y vuelta atrás (la ventana propia de un tablero separado cuando tiene el teclado) |
| <kbd>Ctrl</kbd>+rueda, <kbd>Ctrl</kbd>+<kbd>+</kbd>, <kbd>Ctrl</kbd>+<kbd>-</kbd> | En todas partes | Tamaño de la interfaz; <kbd>Ctrl</kbd>+<kbd>0</kbd>: volver al 100 % |
| <kbd>Intro</kbd>, <kbd>Esc</kbd> | Un campo numérico | Calcular el valor; devolver al campo su valor anterior |
| <kbd>Alt</kbd>+<kbd>←</kbd>, <kbd>Alt</kbd>+<kbd>→</kbd> | El panel de la guía | Atrás, adelante |
| <kbd>Ctrl</kbd>+<kbd>F</kbd> | El panel de la guía | Buscar en la guía |
| <kbd>Esc</kbd> | El panel de la guía | Cerrarlo |
| <kbd>↑</kbd> <kbd>↓</kbd>, <kbd>Re Pág</kbd> <kbd>Av Pág</kbd> | La paleta de comandos | Elegir |
| <kbd>Intro</kbd>, <kbd>Esc</kbd> | La paleta de comandos | Ejecutar, cerrar |
| <kbd>←</kbd> <kbd>→</kbd> <kbd>Inicio</kbd> <kbd>Fin</kbd> | Un tablero que muestra una partida | Jugada anterior, jugada siguiente, inicio, última jugada |
| <kbd>F</kbd> | Un tablero | Girarlo |
| <kbd>A</kbd> | Un tablero | Abrir la posición en la página Tablero y analizarla; en la página Tablero: iniciar o detener el análisis |
| <kbd>Fin</kbd> | La partida en primer plano en *Directo* | Volver a la jugada actual |
| <kbd>Ctrl</kbd>+<kbd>V</kbd> | La página Tablero | Pegar una FEN, una línea EPD, un PGN o una lista de jugadas |
| <kbd>G</kbd> | La página Tablero | Analizar la partida, pasada tras pasada; otra vez: detenerlo |
| <kbd>X</kbd> | La página Tablero | Mostrar u ocultar la amenaza |
| <kbd>N</kbd>, <kbd>Mayús</kbd>+<kbd>N</kbd> | La página Tablero | La jugada siguiente, la anterior que marcó el análisis |
| <kbd>Esc</kbd> | La página Tablero | Devolver la partida tras mostrar una línea; soltar la pieza cogida |
| <kbd>←</kbd> <kbd>→</kbd> | La pantalla grande | La partida anterior o la siguiente |
| <kbd>+</kbd> <kbd>-</kbd> | La pantalla grande | Texto más grande o más pequeño |
| <kbd>Esc</kbd> | La pantalla grande | Salir de ella |
| <kbd>Tab</kbd>, flechas, <kbd>Intro</kbd>, <kbd>Espacio</kbd> | Nuevo torneo | Moverse entre las filas, listas y botones; abrir una fila o una lista |
| <kbd>Ctrl</kbd>+<kbd>↑</kbd>, <kbd>Ctrl</kbd>+<kbd>↓</kbd> | Los participantes | Llevar los participantes seleccionados al principio, al final |
| <kbd>Alt</kbd>+<kbd>↑</kbd>, <kbd>Alt</kbd>+<kbd>↓</kbd> | Los participantes | Moverlos un puesto |
| <kbd>Supr</kbd> | Los participantes | Quitarlos |
| <kbd>Menú</kbd>, <kbd>Mayús</kbd>+<kbd>F10</kbd> | Los participantes | Abrir el menú del participante (sus ajustes propios) |
| <kbd>Ctrl</kbd>+<kbd>F</kbd> | La biblioteca de la configuración | Ir a su filtro |
| <kbd>Intro</kbd> | La biblioteca de la configuración | Añadir los motores seleccionados, o el primero mostrado |
| <kbd>Ctrl</kbd>+<kbd>A</kbd> | La biblioteca de la configuración | Seleccionar todos los motores mostrados |
| <kbd>Ctrl</kbd>+clic, <kbd>Mayús</kbd>+clic | Listas | Seleccionar varios |
| <kbd>Supr</kbd> | La página Motores | Quitar el motor seleccionado (tras una confirmación) |
| <kbd>Intro</kbd>, <kbd>↑</kbd> <kbd>↓</kbd> | La consola de un motor | Enviar la línea; recuperar las líneas anteriores |
| <kbd>Ctrl</kbd>+<kbd>C</kbd> | El modo sin interfaz gráfica | Detenerse tras las partidas en juego |

## La página de inicio
<!-- en: the-home-page 40031a16 -->

La página **Torneos** es una sala de control: primero lo que está en marcha y lo que te necesita, después el resto.

- **Primeros pasos**, hasta que se inicia un primer torneo: los pasos del [Inicio rápido](#inicio-rápido) (añadir
  motores, configurar un torneo, iniciarlo), cada uno con su botón y marcado una vez hecho, y *Leer el inicio
  rápido*, que lo abre en el panel de la guía. *Ocultar* quita la tarjeta de la vista.
- **Pausar todo**, junto a *Abrir una carpeta…* y *Nuevo torneo*, pone en pausa los torneos en marcha (no empieza
  ningún par de partidas nuevo, las partidas en curso se juegan hasta el final) y retiene la cola: ningún torneo en
  cola empieza hasta **Reanudar todo**, que los reanuda y deja que la cola siga, de inmediato si mientras tanto
  terminó un torneo.
- **Núcleos**: los hilos de hardware de la máquina, y los que ocupan las partidas en juego, el resto libres. Una
  partida ocupa los `Threads` de aquel de sus dos motores que tiene más, ya que los dos motores piensan por turnos; la
  barra muestra el nombre de cada torneo y su número, y el cursor, los detalles.
- **Requiere atención**, cuando algo lo requiere, cada cosa con su solución a mano: un motor que no arranca (consulta
  [Un motor que no arranca](#un-motor-que-no-arranca): *Reintentar*, *Retirar…*, *Reemplazar…*, *Registro del
  motor*); el torneo que un error o un motor detuvo, con lo que lo detuvo, *Abrir el motor*, *Abrir el registro*,
  *Reanudar*, *Abrir* y *Ocultar*; una unidad demasiado llena para una noche de partidas (menos de 1 GB libre, o menos
  del 2 % en una unidad de menos de 500 GB), la de la carpeta de torneos o la de un torneo en marcha, comprobada cada
  minuto, con *Abrir la carpeta* y *Cambiar la carpeta de torneos…* (donde van los torneos nuevos, como en
  **Ajustes**).
- **En curso**: una tarjeta por cada torneo en marcha: su nombre, formato, ritmo de juego y estado, una barra de
  progreso con las partidas jugadas de las previstas y las partidas en juego, cuándo termina, y quién va en cabeza: el
  primero de la clasificación con su rating frente al conjunto de rivales y sus puntos (una eliminatoria: los
  jugadores que quedan; un encuentro por equipos: el marcador de los equipos y su Elo), el motor que va por delante en
  un match con su Elo y el marcador, o el LLR de un SPRT con sus límites, y luego el Elo y el marcador. *Abrir* va a
  su espacio de trabajo, *Pausa* lo pone en pausa. Un torneo que se puede reanudar aparece en cambio bajo
  *Interrumpido*, con *Reanudar*. Consulta [Varios torneos a la vez](#varios-torneos-a-la-vez) para los pesos y los
  núcleos de cada uno.
- **Cola**: los torneos en espera, cada uno con su duración y cuándo empieza, y cuándo termina todo (consulta
  [Cola y relanzamiento](#cola-y-relanzamiento)).
- **Últimos terminados**: los cinco últimos torneos terminados, con su resultado: el marcador de un match, la
  decisión del SPRT, el ganador de una eliminatoria (de su final), el marcador de un encuentro por equipos, o el
  primero de la clasificación con sus puntos sobre sus partidas. *Relanzar con…* para un match, *Clonar* (ver
  [Cola y relanzamiento](#cola-y-relanzamiento)) y *Abrir*.
- **Fiabilidad**: los cuelgues o bloqueos, las pérdidas por tiempo y las jugadas ilegales de las últimas 24 horas,
  la memoria libre e instalada, y los procesos de motor en ejecución; luego los motores que perdieron esas partidas,
  los tres con más, con *Abrir el motor* y *Abrir la carpeta* (la carpeta `logs` del torneo guarda el registro del
  motor de cada partida que terminó mal). Una pérdida por tiempo antes de la jugada 20 se distingue aparte: se debe
  más a menudo a un motor mal configurado (demasiados hilos, un hash demasiado grande para reservarlo a tiempo, una
  red cargada en cada partida, un arranque lento) que a su gestión del tiempo.
- **Todos los torneos**: todos los torneos del perfil, del más reciente al más antiguo, con *Abrir*, *Clonar* y
  *Carpeta*; su título lo pliega. El resultado de uno terminado aparece bajo el cursor, sobre su estado.

La paleta de comandos también tiene estas acciones: *Pausar todo*, *Reanudar todo*, *Relanzar … con…* para los
últimos matches terminados, *Clonar …* para el torneo mostrado y los últimos terminados, *Abrir la carpeta de
torneos* y *Cambiar la carpeta de torneos…*.

## Motores
<!-- en: engines 4f15155a -->

La página **Motores** es la biblioteca de motores: la lista a la izquierda, la ficha del motor elegido a la derecha.

### Añadir y comprobar motores
<!-- en: adding-and-checking-engines ebf41ee1 -->

**Añadir…** registra un ejecutable UCI. El motor se arranca una vez para leer su nombre, su autor y sus opciones.
Las opciones se editan en una tabla: los valores modificados se resaltan, y solo los valores que difieren del valor
por defecto del motor se envían con `setoption` al inicio de cada partida. `Threads` y `Hash` van primero.

- **Duplicar** crea una segunda entrada para el mismo binario, con otras opciones (otro `Hash`, un nivel de juego,
  otro archivo de red…). Las entradas reciben el nombre del `id name` UCI al añadirse por primera vez y pueden
  renombrarse.
- **Comprobar** arranca el motor, mide lo rápido que responde a `uci` y a `isready`, y le hace jugar una jugada;
  luego ejecuta los pasos de conformidad UCI de fastchess (`id name` e `id author`, posiciones desde una FEN y con
  jugadas, `go` con cada reloj, líneas info con una puntuación entera…), hasta el primero que falle. El resultado (✓
  o el error) queda junto al motor con su fecha, e *Informe de conformidad UCI* lista los pasos, con lo que escribió
  el motor tras uno fallido. Un motor al que se habla en xboard solo pasa por la primera parte.
- **Consola…** habla con el motor, fuera de cualquier torneo: se arranca con el programa, los argumentos y la
  carpeta de trabajo de la biblioteca, y un motor UCI recibe `uci` y luego los valores de opciones de la biblioteca.
  Las líneas escritas se envían tal cual (Intro; Arriba y Abajo recuperan las anteriores), y el registro muestra cada
  línea con su hora, las enviadas marcadas con `>` y la salida de error con `!`. Unos botones envían los comandos
  habituales (`isready`, `position startpos`, `go depth 10`, `go infinite`, `stop`…; los de WinBoard para un motor
  WinBoard, al que se habla en su propio protocolo salvo que se marque *Por el adaptador*) y la posición de la página
  Tablero. *Copiar* y *Guardar…* conservan el registro. La consola aparece en lugar de los ajustes del motor
  (*Ajustes* vuelve a ellos, con la consola aún abierta); *Cerrar* detiene el motor. Mientras se juega un torneo, lo
  que el motor busca quita núcleos a sus partidas: la consola lo indica. Su motor no es uno de los procesos del
  torneo: el final de un torneo no lo detiene.
- **Prueba rápida** juega un match corto contra el propio motor u otro: 10 partidas a 10+0.1 desde 5 posiciones
  (una con una captura al paso), con el mismo código que un torneo, mostrado en su espacio de trabajo mientras se
  juega. El resultado queda junto al motor: su puntuación, sus pérdidas por tiempo, cuelgues y jugadas ilegales, y la
  parte de su reloj que usó, que delata a un motor que juega demasiado rápido o demasiado cerca de la caída de la
  bandera. Sus PV de mate se comprueban (consulta *Comprobar las PV de mate* en [PGN y otros](#pgn-y-otros)). Solo se
  guarda la última prueba rápida, en `tests/` en el perfil (con los registros del motor de sus partidas que
  terminaron mal).

### Una biblioteca grande
<!-- en: a-large-library beb231ed -->

- **Grupos**: etiquetas libres separadas por comas (`pool A`, `list 40/15`…). La lista puede filtrarse por grupo o
  buscarse, y un grupo puede añadirse a un torneo en un clic. *Notas*: cualquier cosa que recordar sobre el motor.
  *Logo*: una imagen mostrada junto a su nombre en *Directo*, en la pantalla grande y en la página web de la
  retransmisión (consulta [Apariencia](#apariencia)).
- **Búsqueda y filtros**: toda búsqueda de motores (la biblioteca, y las listas de motores de las demás páginas)
  encuentra un nombre, el nombre que comunica el motor, su autor, la ruta de su programa o sus notas; el filtro de
  estado muestra los motores que funcionan, que fallaron o cambiaron, que nunca se comprobaron, que han perdido su
  programa, que hablan WinBoard o que están inactivos. Ctrl o Mayús clic selecciona varios motores: *Comprobar* (uno
  tras otro, nunca mientras se juega un torneo), *Añadir a un grupo…*, *Quitar…*. *Más…* comprueba los que nunca se
  comprobaron (*Comprobar los no comprobados*) y quita los programas ausentes. Una opción de texto que contiene un
  archivo o una carpeta (`EvalFile`, `SyzygyPath`…) tiene un botón *…*.
- **Inactivo** (en la ficha, o desde el clic derecho para todos los motores seleccionados): el motor sigue en la
  biblioteca, atenuado, pero queda fuera de los selectores de motores (configuración, suites, prueba rápida,
  relanzamiento, añadir un motor a un torneo en curso) y de *Comprobar los no comprobados*. Un torneo o una
  configuración que ya lo tiene lo conserva.
- **Usa una GPU** (en la ficha): un motor de red neuronal que juega en una tarjeta gráfica, como Lc0. No juegan a la
  vez más motores así que las *GPU* de **Ajustes**, en un torneo y entre torneos (consulta [Varios torneos a la
  vez](#varios-torneos-a-la-vez)); a partir del siguiente torneo en el que entre.
- **Elo de referencia**: el rating del motor en una lista en la que confías. Un torneo o la lista de rating pueden
  anclarse en él, y las partidas de un torneo lo llevan en sus etiquetas `WhiteElo` y `BlackElo` (copiado cuando el
  motor se incorpora). *Importar…* > *Una lista de rating, para el Elo de referencia…* lo fija para muchos motores a
  la vez (consulta [Importar motores y ratings](#importar-motores-y-ratings)).

### Arrancar los motores
<!-- en: starting-engines 09d107ba -->

- **La lectura de las opciones** nunca bloquea un inicio. *Reanudar* empieza de inmediato: un torneo juega con los
  motores copiados en su carpeta. *Iniciar*, la cola y la prueba rápida solo esperan a la lectura de un motor que
  juegan, que se lee primero; cualquier otra lectura o comprobación se interrumpe y se vuelve a hacer más tarde. Un
  inicio que espera lo indica en su botón (*Esperando a …*, con *Cancelar*), y mientras tanto se rechaza otro inicio.
  Un inicio también espera a una suite de tests en curso, y una suite de tests no empieza mientras se ejecuta la
  prueba rápida o espera un inicio. Las lecturas, las comprobaciones y las suites de tests se ejecutan junto a los
  torneos, que se juegan en procesos propios. Un motor en cola puede leerse a continuación desde su menú.
- *Reiniciar el motor en cada partida* arranca un proceso nuevo para cada partida en lugar de reutilizarlo. Si no,
  un torneo mantiene un proceso en espera por cada motor al que aún le quedan partidas por jugar, hasta 32 (más dos
  por partida en juego), y cierra un motor tras su última partida: en una eliminatoria, en cuanto pierde su match; en
  un suizo, cuando ya no le queda ninguna partida en la última ronda.
- *Tiempo de arranque (s)* (60 por defecto) es el tiempo que se da a un motor para responder `uciok` cuando arranca
  e `isready` al inicio de cada partida; un motor lento en arrancar (una red grande que cargar) necesita más. Un motor
  que no logra arrancar se vuelve a intentar tres veces, con 10 s de intervalo, con el mismo tiempo límite; una pausa
  retiene los intentos, sin gastarlos. Después, sus partidas se apartan y las demás continúan (consulta [Un motor que
  no arranca](#un-motor-que-no-arranca)). Un torneo creado después copia este tiempo límite en sus participantes.

### Programas recompilados
<!-- en: rebuilt-programs 549daa6e -->

- **Programas movidos o recompilados**: la biblioteca recuerda cada programa (tamaño, fecha, CRC32). Un programa que
  ya no está se marca con **!** y *Localizar…*, y un torneo no empieza con él. Un programa que cambió (una nueva
  compilación, u otro archivo en la misma ruta) vuelve a leer sus opciones automáticamente.
- **Qué cambió una nueva compilación**: cuando la nueva lectura difiere de la anterior, el motor se marca con **Δ** y
  su ficha lista las diferencias hasta *Cerrar*: su nuevo `id name` (con *Renombrarlo* cuando el motor llevaba el
  nombre del antiguo) y su autor, cada opción cuyo valor por defecto, límites o valores posibles cambiaron (antes y
  ahora), las opciones nuevas y desaparecidas, y tu valor de cada una (el valor de una opción desaparecida se
  descarta; uno que la nueva compilación ya no acepta aparece en naranja). Para una opción cuyo valor por defecto
  cambió, o cuyo valor ya no encaja, elige *Conservar mi valor* (o *Conservar el antiguo por defecto* si no tenías
  ninguno: juega como antes) o *Usar el nuevo por defecto*; *Conservar mis valores* y *Usar los nuevos por defecto*
  eligen para todas. El aviso desaparece una vez hechas todas las elecciones, salvo que tenga algo más que decir.
  Varias compilaciones antes de cerrarlo dan un solo aviso, comparado con la lectura anterior a la primera. *Con fallo
  o modificado* en el filtro de estado también muestra estos motores, y la paleta de comandos tiene *Qué cambió*,
  *Conservar mis valores*, *Usar los nuevos por defecto* y *Cerrar el aviso de cambios* para cada uno.
- **Los programas de los torneos**: cada torneo registra los programas con los que se inició. El resumen de la
  configuración avisa de un motor cuyo programa cambió desde su último torneo (el más reciente de la carpeta de
  torneos que jugó este programa), con *Qué cambió* cuando su ficha tiene un aviso. Reanudar un torneo con un
  programa recompilado o desaparecido desde entonces lo indica en su cabecera, con *Continuar* (el aviso se cierra) y
  *Pausa*; el torneo no se detiene por sí solo.

### Importar motores y ratings
<!-- en: importing-engines-and-ratings 63154f3f -->

**Importar…** > *Una lista de motores…* lee la lista de motores de otro programa: ChessGUI (`ChsGUI_Eng.TXT`, con
las opciones de `ChsGUI_Cfg.ini` a su lado), cutechess y Banksia (`engines.json`), Arena (`ArenaENG.cfg`, en la
carpeta de Arena: el programa, los argumentos y el protocolo de cada motor y, para los motores UCI, los valores de las
opciones fijados en Arena) y otras listas de estilo INI. Una lista que no da ningún motor lo indica, con los formatos
leídos y *Todos los motores de una carpeta…* para probar en su lugar.

*Todos los motores de una carpeta…* toma todos los programas encontrados en una carpeta, y recuerda la carpeta:
*Actualizar: los motores nuevos de la carpeta* ofrece entonces solo los motores añadidos a ella desde entonces. Los
motores encontrados se listan primero, para desmarcar los que hay que dejar fuera; los motores que ya están en la
biblioteca (mismo programa y argumentos) se omiten; se leen las opciones de los demás; los motores WinBoard también
se importan.

*Importar…* > *Una lista de rating, para el Elo de referencia…* lee una lista de rating, pegada o abierta: la
versión de texto de una lista de rating o su página web (guardada o copiada), la salida de Ordo (texto o CSV), o un
CSV de líneas `name,elo`. Un nombre de la lista es un motor de la biblioteca con el mismo nombre, o el único cuyo
nombre es el mismo sin contar mayúsculas ni espacios (como para los jugadores de los archivos PGN importados,
consulta [Clasificaciones](#clasificaciones)). La vista previa muestra el Elo de referencia de cada motor, antiguo y
nuevo, marcado para fijarlo, y los nombres de la lista que no son ningún motor de la biblioteca. Los nombres de una
lista suelen llevar su compilación (`64-bit 4CPU`): renombra los motores para que coincidan si hace falta.

### Motores WinBoard
<!-- en: winboard-engines 079e4849 -->

Los **motores WinBoard** (protocolo xboard, CECP) se juegan como los demás. El *Protocolo* de un motor se averigua
al añadirlo (*Automático*), o se elige: UCI o WinBoard. fastchess solo habla UCI: un motor WinBoard se arranca a
través de una traducción integrada en fastchess-gui (`fastchess-gui --wb2uci ENGINE [ARGS]`, que fastchess arranca en
lugar del motor), y sus opciones, `memory`, `cores` y la ruta de Syzygy se convierten en opciones UCI. Maneja el
motor como lo hace WinBoard: features del protocolo 2, o protocolo 1 para los motores que no envían ninguna;
posiciones con `setboard`, o modo de edición; jugadas en coordenadas o en SAN; `level`, `time`/`otim`, `st`, `sd`,
`nps`; salida de pensamiento, abandono. Los motores marcados con ↔ en la lista son motores WinBoard.

- Un motor de protocolo 1 sin `setboard` ni modo de edición (TSCP, por ejemplo) solo puede empezar desde la posición
  inicial: dale las aperturas como jugadas (un libro PGN), no como posiciones (EPD).
- Chess960 para un motor con la variante `fischerandom`: `variant fischerandom`, posiciones en X-FEN, enroque como
  `O-O`/`O-O-O`, tal como los envía WinBoard.
- *Scores from White's side*, entre las opciones del motor: para los motores antiguos que dan sus puntuaciones desde
  el punto de vista de las blancas en lugar de desde el del bando al que le toca mover, como pide el protocolo; sus
  puntuaciones con negras se invierten.
- Un motor que pide que se lo vuelva a arrancar en cada partida (`feature reuse=0`) recibe *Reiniciar el motor en
  cada partida* marcado al añadirlo.
- `FCG_WB2UCI_LOG=FILE` en el entorno escribe en `FILE` cada línea intercambiada con los motores WinBoard.

## Libros
<!-- en: books 4cf13005 -->

**Añadir…** registra un libro de aperturas (EPD, PGN o Polyglot `.bin`) con un nombre y una descripción; para un
libro PGN, el número de medias jugadas que se conservan; para un libro Polyglot, la profundidad hasta la que se siguen
sus líneas. La sección *Aperturas* de la configuración ofrece entonces los libros de la biblioteca por su nombre, con
su número de aperturas (aún se puede elegir cualquier otro archivo). *Quitar* saca un libro de la biblioteca y deja
el archivo intacto.

### Comprobar un libro
<!-- en: checking-a-book 96eee229 -->

El libro elegido se lee en segundo plano, con los lectores propios de fastchess (así que sus aperturas son
exactamente las que juega un torneo), y se comprueba:

- **posiciones ilegales**: no exactamente un rey de cada color, peones en la primera o la última fila, el bando que no
  mueve en jaque;
- **duplicados**: la misma posición que una apertura anterior, transposiciones incluidas;
- **colores invertidos**: una apertura anterior con los colores intercambiados (un libro pensado para jugarse dos
  veces, con los colores invertidos, no necesita ambas);
- **partidas ya terminadas**: ninguna jugada legal, o material insuficiente para dar mate.

fastchess deja fuera de un torneo las aperturas cuya posición no se puede jugar (una línea FEN o EPD sin rey o mal
formada de otro modo, un rey que podría ser capturado, una partida PGN cuya etiqueta FEN no es válida), con un aviso
por libro; aquí se cuentan como posiciones ilegales. Una partida PGN cuyas jugadas dejan de poder analizarse (una
jugada mal formada o ambigua) conserva las jugadas anteriores. Las líneas de un libro PGN que empiezan por `%` (líneas
de escape, que el estándar PGN convierte en comentarios; los libros AVT tienen algunas) se ignoran, también en los
torneos, y la página Libros las cuenta (*líneas que empiezan por %, ignoradas*).

Sus aperturas se listan debajo, todas o solo las que tienen algún problema, y se pueden buscar por parte de una FEN
o por jugadas (un libro PGN muestra sus jugadas en SAN); un clic muestra la posición en un tablero. *Usarlo en el
próximo torneo* lo pone en la configuración (con Chess960 para un libro Chess960). *Guardar una copia limpia…*
escribe un libro EPD sin las posiciones ilegales, los duplicados ni las partidas terminadas (ni, si está marcado, las
posiciones con los colores invertidos), y lo añade a la biblioteca. *Ejecutar como suite de tests…* abre un libro EPD
en **Suites**.

### Crear libros
<!-- en: creating-books b9c072ab -->

**Crear…** escribe libros para la biblioteca (también en la paleta de comandos):

- *Generar un libro Chess324*: el Chess324 de Larry Kaufman. Los reyes y las torres se quedan en sus casillas
  habituales, así que el enroque es el habitual; la dama, los alfiles y los caballos de cada bando se barajan en las
  demás casillas de su primera fila (las columnas b, c, d, f y g), con los alfiles en casillas de distinto color. Eso
  da 18 disposiciones por bando, elegidas de forma independiente para las blancas y las negras: *Las 324
  posiciones*, o *Solo las 18 simétricas*, en las que las piezas negras están en las mismas columnas que las
  blancas. Son posiciones normales, escritas en un archivo EPD (propuesto junto a los libros de la biblioteca, o en
  `books/` en el perfil) y que cualquier motor juega, sin Chess960.
- *Construir un libro Polyglot…*: un libro Polyglot (`.bin`) a partir de las partidas de un archivo PGN, como hace
  `make-book` de Polyglot, con la conversión que transforma los libros PGN de los participantes en libros Polyglot.
  Su formulario, en lugar de los detalles del libro, pide el archivo PGN (el libro PGN elegido en la lista, si lo
  hay), la **profundidad** (las medias jugadas de cada partida que entran en el libro), el **mínimo de partidas** que
  necesita una jugada en su posición para conservarse (`-min-game` de Polyglot; las demás se descartan por jugarse
  demasiado poco), los **pesos**, *por puntos* (los puntos del bando que jugó la jugada: una victoria 2, unas tablas
  1, una derrota 0, como cuenta Polyglot; las partidas sin resultado se descartan, y también una jugada cuyas
  partidas se perdieron todas) o *por número* (el número de partidas que la juegan), y, si está marcado, un **Elo**:
  solo cuentan las jugadas de un jugador con al menos ese Elo, según la etiqueta `WhiteElo` o `BlackElo` de la
  partida (un jugador sin ella se descarta). *Construir…* pregunta dónde escribir el libro (junto al archivo PGN por
  defecto) y luego lee las partidas en segundo plano, con su progreso y *Cancelar*, junto al formulario y al final de
  la lista; el libro, ordenado por clave como exige el formato, se añade entonces a la biblioteca, con sus opciones
  en su descripción. La consola dice cuántas partidas entraron en él, cuántas descartaron las opciones y cuántas no se
  pudieron leer.

## Tablero
<!-- en: board 0bbd00c9 -->

La página Tablero muestra una partida o una posición, de cualquier procedencia: **A** en cualquier tablero del programa
(una partida del mosaico de la pestaña *Directo* o la seguida, el tablero de la pestaña *Partidas*, el de la pestaña
*Aperturas*, el de una apertura de un libro) abre aquí su posición y la analiza; en la página Tablero, A inicia y
detiene el análisis. *Volver al torneo* (o *Volver*) regresa a la página de la que vino. Su tablero puede dibujarse en
3D: véase [El tablero 3D](#el-tablero-3d).

El tablero está a la izquierda, con los jugadores encima y debajo; a la derecha, de arriba abajo, el análisis, las
jugadas (con la tabla de las jugadas, la revisión de la partida y las partidas del archivo en pestañas) y el gráfico de
la partida. El subtítulo de la página dice qué partida es: su evento y su ronda, su resultado, su ritmo de juego, su
código ECO y su apertura, y cómo terminó.

Una jugada se hace en el tablero con el ratón: un clic en la pieza y luego otro en su casilla, o la pieza arrastrada
hasta allí. Mientras se arrastra, sigue al puntero y la casilla donde caería se tiñe; si se suelta en otro sitio,
vuelve, todavía tomada, para moverla con un clic. En el tablero 3D, un fantasma translúcido de la pieza sigue al
puntero, flotando sobre el tablero y pasando por detrás de las piezas que tiene delante, mientras la imagen se queda
como está; la pieza misma se mueve al soltarla.

### Abrir, pegar y preparar una posición
<!-- en: opening-pasting-and-setting-up-a-position 3778ddb5 -->

- **Abrir un PGN…** lee cualquier archivo PGN (también se puede soltar un archivo sobre la ventana), en segundo plano
  con una barra de progreso: un archivo de millones de partidas tarda unos segundos, y sus partidas se listan en una
  pestaña junto a las jugadas, filtradas por lo que se escribe (cada palabra debe encontrarse en los jugadores, el
  evento, el lugar, la fecha, la ronda, la apertura, el código ECO o el resultado). Un clic muestra una partida. Las
  evaluaciones de sus jugadas dibujan el gráfico: se leen los comentarios de cutechess y fastchess
  (`{+0.35/18 1.2s}`) y el `[%eval 0.35]` de lichess y ChessBase (desde el lado de las blancas, `#-3` para un mate),
  y también los relojes de `[%clk 0:01:23]`. Sus variantes se conservan (consulta [Variantes](#variantes)). Una
  partida cuya etiqueta FEN no se puede jugar se rechaza, y las jugadas de una partida se detienen en la primera que no
  se puede leer.
- **Pegar** (Ctrl+V en la página) lee el portapapeles: una FEN, o los cuatro campos de una línea EPD; un PGN de una
  o más partidas (listadas como las de un archivo); una lista de jugadas desde la posición inicial, en SAN con o sin
  números (`1. e4 e5 2. Nf3`) o en UCI (`e2e4 e7e5`). Una posición que no se puede jugar se rechaza indicando el
  motivo (falta un rey, un peón en la primera fila, el bando que no mueve en jaque). Unos derechos de enroque cuyo rey
  o torre no está en su casilla habitual, o escritos como columnas (`HAha`), la convierten en una posición Chess960.
  También se puede soltar un archivo `.fen` o `.epd`.
- **Editar la posición** prepara una posición en el tablero: un clic pone la pieza elegida (un segundo clic la
  quita), un clic derecho la quita; el bando al que le toca mover, el número de jugada, los derechos de enroque
  (ofrecidos solo cuando el rey y la torre están donde deben), la casilla al paso (solo detrás de un peón que acaba de
  avanzar dos casillas) y Chess960. La FEN de debajo sigue al tablero, y una FEN escrita ahí lo fija. *Listo* se
  ofrece en cuanto la posición se puede jugar, y la pone en el tablero.
- *Copiar PGN* (a la derecha de las pestañas de las jugadas) copia la partida mostrada, con sus etiquetas, sus jugadas
  (las puntuaciones de los motores como comentarios cuando las tiene) y sus variantes.
- **El tablero**: los botones de debajo y ← → Inicio Fin recorren las jugadas, y F (o el último botón) lo gira; un
  clic en una pieza y en una casilla juega una jugada (un peón que corona pregunta por su pieza). *Copiar FEN* copia la
  posición.

### Los jugadores
<!-- en: the-players 1f4f551f -->

Encima y debajo del tablero, cuando la partida tiene jugadores: el nombre de cada uno, la evaluación de su última
jugada según su motor (la puntuación desde el lado de las blancas y la profundidad, en una etiqueta), la jugada que hizo
y el tiempo que tardó, o *le toca jugar*, y a la derecha su reloj tras su última jugada, enmarcado mientras tiene el
turno. Los relojes son los de los `[%clk]` de las jugadas cuando la partida los tiene; si no, se reconstruyen a partir
de su etiqueta `TimeControl` (`300+2`, `10+0.1`: segundos e incremento) y del tiempo de cada jugada, sin que las
jugadas de libro consuman ninguno.

### El análisis
<!-- en: the-analysis 1e6120de -->

Un motor de la biblioteca busca en la posición mostrada y sigue al tablero. La tarjeta muestra su motor y luego un
botón con sus *Threads*, su *Hash* (0: el del motor) y sus *Líneas* (MultiPV), que se abre con un clic: los ajustes
propios del análisis, que se conservan para la próxima vez y no cambian ni el motor de la biblioteca ni ningún torneo
(los demás valores de opciones de la biblioteca se envían como los envía una partida). A la derecha, los hilos que usa
la página Tablero de los de la máquina, en el color de aviso mientras se juega un torneo (su descripción emergente
indica cuántos hilos ocupan las partidas del torneo: el análisis compite con ellas).

La búsqueda se muestra en recuadros (profundidad y seldepth, nodos, velocidad, tiempo, accesos a tablas de finales, lo
lleno que está el hash), las probabilidades de victoria, tablas y derrota cuando el motor las da (`UCI_ShowWDL`), y cada
línea: su puntuación (desde el lado de las blancas, la mejor destacada), su profundidad y sus jugadas en dos renglones,
con figuras (consulta [Apariencia](#apariencia)). Pasar el ratón por una jugada de una línea muestra en el tablero,
enmarcado, la posición tras ella; un clic juega la línea hasta esa jugada. La primera jugada de cada línea se dibuja
como una flecha, la mejor más oscura (**Ajustes** > *Apariencia* > *Las flechas del análisis en la página Tablero* las
quita; las líneas siguen mostrando las jugadas).

**Amenaza** (X) hace que otro proceso del motor busque en la posición pasando el turno: la mejor jugada del rival si
volviera a mover, dibujada como una flecha roja y escrita bajo las líneas (ninguna mientras el bando al que le toca
mover está en jaque).

El análisis espera mientras se muestra otra página, y continúa cuando se vuelve a la página Tablero; *Detener el
análisis* (o A) lo detiene, y un punto en el botón *Tablero* de la barra indica cuándo está en marcha. Su motor no es
uno de los procesos del torneo: el final de un torneo no lo detiene. Detenido el análisis, su motor sigue iniciado para
la siguiente búsqueda, con su tabla hash (y la memoria de GPU de un motor de red neuronal): el botón de encendido junto
al motor, *Descargar el motor* (también en la paleta de comandos), lo cierra, y también el de la amenaza.

### Analizar una partida entera
<!-- en: analysing-a-whole-game a7e6166c -->

**Analizar la partida** (G, en las herramientas de la página o en la cabecera del gráfico) hace que el motor de
análisis (sus hilos y su hash, una línea) busque en cada posición de la partida en segundo plano, primero 20 ms cada
una, luego de nuevo cuatro veces más en cada pasada (80 ms, 0,3 s, 1,3 s, 5 s…), hasta el tiempo elegido con la flecha
junto al botón (0,3 s, 1,3 s, 5 s, 20 s por posición, o *hasta que lo detengas*): el gráfico aparece en un segundo o
dos y se afina en cada pasada. Las posiciones van de la última a la primera, lo que permite que el hash del motor
guarde lo que viene después de una posición (medido: alrededor de un 16 % más cerca de las búsquedas largas que de la
primera a la última, con el mismo tiempo), empezando por la posición del tablero. La cabecera del gráfico indica la
pasada, el tiempo por posición y el progreso, y una banda fina bajo su franja de tiempos lo que recibió cada posición;
*Detener* (o G) lo detiene.

Cada jugada se juzga por las probabilidades de ganar que perdió quien la jugó, a partir de las puntuaciones del
análisis antes y después de ella (la medida de lichess: 50 % a 0.00, alrededor del 59 % a +1.00): 10 puntos perdidos
es una imprecisión (`?!`), 20 un error (`?`), 30 un error grave (`??`), marcados tras la jugada, en el gráfico y en el
tablero; la jugada que habría jugado el motor nunca se marca. Una jugada se juzga según la última pasada completa; una
marca que la última pasada cambió queda hueca hasta que la siguiente la confirma. N y Mayús+N (o los botones junto a la
navegación) van a la jugada marcada siguiente y a la anterior.

Cada pasada completa se guarda con la partida (en `analyses/` en el perfil): la misma partida, abierta de nuevo desde
cualquier sitio, la muestra de inmediato, y G continúa a partir de las pasadas hechas. El análisis se ejecuta en su
propio hilo con su propio proceso de motor, y sus hilos cuentan en el presupuesto de núcleos; el análisis del tablero
(A) lo pone en pausa, y cuando A busca en una posición de la partida durante más tiempo que la pasada, su valor ocupa
el lugar del de esa posición.

**Varios análisis**: *+ Motor* en la leyenda del gráfico hace que otro motor de la biblioteca analice también la
partida, el mismo tiempo en cada pasada. Cada análisis tiene su color en el gráfico y en el tablero (su mejor jugada
como una flecha mientras el análisis del tablero está detenido), su línea en *Esta jugada* y sus precisiones en la
revisión. Avanzan juntos pasada a pasada, todos a la vez cuando los núcleos lo permiten, uno tras otro si no. Un
análisis juzga las jugadas (el primero; un clic derecho en otro en la leyenda lo convierte en el juez, o lo quita); una
marca queda hueca cuando los análisis no juzgan todos igual la jugada. Las probabilidades de victoria comparan los
motores mejor que los centipeones, que difieren de un motor a otro: toman el WDL de cada motor cuando lo da.

### El gráfico de la partida
<!-- en: the-game-graph 14e02bad -->

Bajo las jugadas, el gráfico de la partida, el mismo que en *Directo* y en *Partidas*:

- **Evaluación**: el análisis que juzga como un área blanca y negra (la ventaja de las blancas por encima de la línea,
  la de las negras por debajo), cada motor que jugó como una curva de su color en sus propias jugadas, los demás
  análisis como curvas discontinuas; las jugadas marcadas sobre la curva; los valores más allá de ±6 dibujados a lo
  largo del borde en el color de aviso. Sin análisis, el área es la media de las evaluaciones de los dos motores.
- Las jugadas de libro rayadas, las fases bajo el eje (la apertura; el medio juego desde la primera posición con 10
  piezas como mucho sin contar los reyes y los peones, o menos de 4 en la primera fila de un bando; el final desde 6
  como mucho, como divide lichess una partida), la zona que conocen las tablas de finales coloreada, los números de
  jugada a lo largo de la parte inferior.
- Bajo el eje, el tiempo de cada jugada: el de las blancas hacia arriba, el de las negras hacia abajo.

Sus pestañas: *Posibilidades de victoria* (a partir del WDL cuando los motores lo dan, con la fórmula de lichess si
no), *V/T/D* (las proporciones que comunicaron los motores, con las victorias de las blancas abajo), *Tiempo* (con el
tiempo de decisión), *Profundidad* (con la seldepth), *Nodos*, *Velocidad*, *Accesos a tablas*, *Acuerdo* (cuánto se
separan las evaluaciones de los dos jugadores, cada jugada frente a la anterior, y las de los análisis en cada
posición) y *Coincidencia de PV* (cuántas medias jugadas de la PV de un motor repiten lo que predijo la del otro); las
que no tienen valores en la partida se omiten, y las que no caben van al menú `⋯`. Al pasar el ratón se ven la jugada
y lo que dijo cada motor; un clic o un arrastre lleva a la jugada. Un clic en una curva de la leyenda la oculta o la
muestra; la flecha de la derecha pliega el gráfico.

### Esta jugada
<!-- en: this-move a4a908f8 -->

Bajo las jugadas, sobre la jugada mostrada: lo que dijo el motor que la jugó (su puntuación, profundidad y seldepth,
tiempo, nodos, velocidad, accesos a tablas de finales, según los tenga el PGN), la línea que esperaba tras ella, el
veredicto de cada análisis (la parte de las probabilidades de ganar perdida, el juicio, la mejor jugada y su
puntuación), y con las tablas de finales el resultado tras la jugada y si la jugada conservó el de antes. *Mostrar*
pone una línea en el tablero sin tocar la partida: la posición tras ella, el tablero enmarcado, lo que es, escrito
encima; Esc, un clic en el tablero o un paso por la partida devuelven la partida. En el tablero, el juicio de la jugada
es una insignia en su casilla, y la respuesta que esperaba su motor, una flecha discontinua.

### Variantes
<!-- en: variations 7932794c -->

Una jugada hecha en el tablero donde la partida siguió otro camino empieza una variante: la partida se queda como
llegó, con sus análisis y su gráfico. Las jugadas muestran las variantes entre paréntesis tras la jugada que
reemplazan, anidadas y numeradas; un clic en una jugada lleva allí, y *Volver a la partida* regresa a la posición de la
que sale la variante. Un clic derecho en una jugada de una variante la promueve (pasa a ser la línea de la que parte,
cuyas jugadas a partir de ahí pasan a ser la variante), la borra junto con las que parten de ella, o la copia. *Copiar
PGN* escribe las variantes, y se leen las de un archivo PGN abierto (se omiten comentarios, números, anotaciones y
resultados; una jugada que no se puede jugar solo termina su variante). El gráfico, los análisis y los juicios son los
de la partida; en una variante, el gráfico marca dónde sale de la partida.

### Las jugadas, la tabla y la revisión
<!-- en: the-moves-the-table-and-the-review 7880f8d8 -->

**Jugadas** escribe la partida como texto corrido: figuras (consulta [Apariencia](#apariencia)), las jugadas de libro
en gris y el final del libro marcado, los juicios en color, la jugada actual enmarcada; al pasar el ratón por una
jugada se ve lo que dijo su motor y su juicio. **Tabla** lista cada jugada con la puntuación de su motor (desde el lado
de las blancas), su profundidad y su tiempo, una barra para el tiempo, y las jugadas marcadas en color.

**Revisión**, una vez analizada la partida: para cada bando su precisión (la de lichess: la de cada jugada según las
probabilidades de ganar que perdió, pesando más las jugadas de los momentos tensos), sus imprecisiones, errores y
errores graves, su pérdida media en centipeones, la parte de sus jugadas que el análisis también jugaría, la
profundidad y el tiempo medios de su motor; su precisión en la apertura, el medio juego y el final; con varios
análisis, las precisiones que da cada uno. Luego los momentos clave, cada uno a un clic: el punto de inflexión (la
jugada que más perdió, con la mejor jugada), los demás errores y errores graves, dónde los dos motores discreparon en
1,5 peones o más durante dos jugadas seguidas, el comienzo del final y el de las tablas de finales. *Quién lo vio
primero* indica, para el punto de inflexión, en qué jugada cada motor que jugó y cada análisis dieron por primera vez
un peón o más al bando que gana. *Copiar el PGN con el análisis* escribe la partida con la evaluación de cada posición
(`[%eval]`), los juicios como NAG y, tras una jugada marcada, la línea que prefería el análisis; *Copiar como BBCode*
da la revisión para un foro.

### Tablas de finales en la página Tablero
<!-- en: tablebases-on-the-board-page 721b71dd -->

Con las carpetas Syzygy de la configuración (*Adjudicación* > tablas de finales), la página Tablero consulta cada
posición de la partida: el gráfico colorea la zona que conocen las tablas, *Esta jugada* indica el resultado tras la
jugada y si la jugada conservó el de antes o lo estropeó, y la revisión marca dónde toman el relevo las tablas. Cada
posición se consulta como si se acabara de hacer una captura o una jugada de peón, lo que es exacto salvo cuando la
regla de las 50 jugadas está cerca. Las tablas son las del proceso del programa: no se consultan mientras se juega un
torneo en él.

### Jugar una partida desde una posición
<!-- en: playing-a-game-from-a-position 7e83c318 -->

**Jugar una partida…** juega una partida desde la posición del tablero entre dos motores de la biblioteca, o entre tú y
uno de ellos (ver *Humano* más abajo) (*Blancas* y *Negras*; `<->` los intercambia con sus relojes), con un reloj para
cada uno (minutos y un incremento en segundos; *El mismo para ambos*, o uno para cada uno para dar ventaja de tiempo) y,
si está marcado, una segunda partida con los colores invertidos. Los motores se arrancan con los valores de opciones de
la biblioteca, un motor WinBoard a través del adaptador. El tablero sigue la partida a medida que se juega (retroceder
en ella deja de seguirla hasta que se vuelve a alcanzar su final, o hasta que se hace clic en *Volver a la jugada
actual*), con los relojes, la búsqueda del bando al que le toca mover y *Detener*, que termina la partida sin resultado.
Una partida termina por mate, ahogado, material insuficiente, la regla de las 50 jugadas, una triple repetición, una
jugada ilegal, un motor que se detiene, o una pérdida por tiempo (con 0,1 s de margen en el reloj para que llegue la
jugada). Cada partida se añade, al terminar, a un archivo PGN en `games/` en el perfil (con el nombre de la fecha y de
los motores, y una etiqueta `Termination`), que *Abrir las partidas* lista en el tablero.

Las partidas las juega el cliente de motores propio de fastchess-gui, al margen de cualquier torneo: se juegan
mientras se juega uno, sin interferir con él. Sus hilos (los del motor que más tiene de los dos, ya que juegan por
turnos) cuentan en el presupuesto de núcleos; cuando las partidas de los torneos en marcha y el análisis ya llenan la
máquina, la tarjeta lo indica y ofrece *Jugar de todos modos* en lugar de *Jugar*. El análisis se detiene cuando
empieza una partida, ya que competiría con sus motores.

**Humano**, primero en las listas de *Blancas* y *Negras*, eres tú: juegas ese bando tú mismo; tus jugadas se hacen en
el tablero con el ratón, en el tablero plano como en el 3D (un peón que llega a la última fila pide su pieza). El
tablero se gira para dejar tu bando abajo en cada partida (*Girar* lo sigue girando), solo toma tus piezas en tu turno
(la tarjeta muestra *Te toca mover*), y un clic mientras miras una jugada anterior lo devuelve a la partida. Tu reloj
corre como el de un motor, pérdida por tiempo incluida: date más tiempo que al motor desmarcando *El mismo para ambos*.
Mientras juegas, lo que piensa el motor se guarda aparte (su evaluación, su profundidad y su línea, y por tanto la barra
de evaluación y la curva del gráfico); la partida terminada lo tiene, lista para *Analizar la partida*. *Abandonar la
partida* la termina en cualquier momento, ganada por el motor; con una segunda partida, esta sigue, con tu color
invertido. Tus partidas se guardan con tu nombre de usuario, y solo el motor cuenta en el presupuesto de núcleos. No se
puede deshacer una jugada, y el motor juega a plena fuerza salvo que su tarjeta fije una menor (`UCI_LimitStrength` y
`UCI_Elo`, o `Skill Level`, si los tiene).

## Suites de tests
<!-- en: test-suites 42c2b2ba -->

La página **Suites** ejecuta una suite de tests EPD: posiciones con su mejor jugada (`bm`), las jugadas que hay que
evitar (`am`) o, para la Strategic Test Suite, los puntos de varias jugadas (`c0 "f5=10, Be5+=2"`).

- **Suite**: un archivo EPD, o un libro EPD de la biblioteca. Las líneas sin una posición legal o sin una de estas
  operaciones se descartan (se muestra su número).
- **Motores**: cualquier número de motores de la biblioteca, con sus opciones; *Hash* y *Threads* imponen un valor
  a todos (0 = el de cada motor).
- **Tiempo**, **Nodos** o **Profundidad** por posición. *Encontrada desde (medias jugadas)*: una búsqueda se
  detiene en cuanto una solución ha seguido siendo la primera jugada del motor durante esa cantidad de medias jugadas
  más de profundidad (0 = cada búsqueda usa todo su tiempo).
- **Concurrencia automática**: con un tiempo, los núcleos físicos − 1 divididos entre los hilos de los motores; con
  nodos o una profundidad, todos los hilos de hardware.

Una posición está resuelta cuando la jugada final del motor es una solución: una de `bm`, ninguna de `am` o, si solo
hay puntos, la que más tiene. Su tiempo de solución es el momento en que el motor se decidió por esa jugada. La
cuadrícula muestra las posiciones frente a los motores, ✓ con el tiempo de solución o ✗ con la jugada jugada, y los
puntos STS; los totales van primero: posiciones resueltas, su tiempo medio y la puntuación STS. Una ejecución
terminada se guarda en `suites/` en el perfil; *Comparar con una ejecución guardada…* añade las columnas de una
ejecución anterior de la misma suite, para comparar versiones de un motor. *Exportar CSV…* escribe la cuadrícula.

## Nuevo torneo
<!-- en: new-tournament 6d169de4 -->

**Torneos → Nuevo torneo** abre la configuración: una tarjeta de filas plegadas que dicen cómo se juegan las
partidas, los participantes con la biblioteca de motores al lado, y un resumen en vivo a la derecha (número de
partidas, partidas por motor, duración estimada, aperturas, ajustes comunes, ventaja de tiempo). La configuración del
último torneo se conserva, así que volver a probar dos versiones de un motor consiste en: elegir A y B, **Iniciar**.

Todo en la página se puede hacer desde el teclado: Tab y las flechas se mueven entre las filas, las listas y los
botones, Intro o Espacio abre una fila o una lista, y las teclas de [los participantes y la
biblioteca](#los-participantes-y-la-biblioteca) funcionan ahí. **Iniciar** crea el torneo y lo juega; **Añadir a la
cola** lo guarda para más tarde (consulta [Cola y relanzamiento](#cola-y-relanzamiento)).

### El resumen y sus comprobaciones
<!-- en: the-summary-and-its-checks 5609344a -->

El resumen indica la memoria que pueden ocupar las tablas hash: dos procesos de motor por cada partida jugada a la
vez, y los motores que esperan su siguiente partida, cada uno con su `Hash`. Por encima de la memoria libre avisa (el
sistema recurrirá al archivo de intercambio), nombrando el motor con el `Hash` más grande; por encima de la memoria
instalada el torneo no empieza. `--run` dice lo mismo. Cuando el presupuesto de memoria de **Ajustes** permite jugar a
la vez menos partidas que la concurrencia, el resumen dice cuántas (cada partida ocupa el `Hash` de sus dos motores y
lo que ocupan además sus procesos), y también lo dice la fila *Concurrencia*; lo mismo para las partidas con un motor
que usa una GPU, frente a las *GPU*. También nombra los motores cuyo programa cambió desde su último torneo (una nueva
compilación), con *Qué cambió* cuando la ficha del motor lista nuevos valores por defecto u opciones nuevas (consulta
[Programas recompilados](#programas-recompilados)).

Lo que fallaría se indica con el motor, el recurso y la solución, un botón bajo la línea (la paleta de comandos lista
las mismas soluciones):

- un programa o una carpeta de trabajo no encontrados: *Abrir el motor* (su ficha en la página Motores),
  *Localizar…*; un libro de aperturas no encontrado: *Localizar…*;
- un motor cuyos propios `Threads` (su ajuste, o si no su valor por defecto: algunos motores toman todos los núcleos)
  superan la mitad de los hilos de hardware, o los hilos que le tocan a cada partida con las partidas jugadas a la vez
  (los núcleos físicos con un reloj): *Bajar Threads a N* fija la opción del motor en la biblioteca, *Abrir el
  motor*. Unos `Threads` dados por la configuración por encima de lo que tiene la máquina reciben *Bajar Threads*
  donde se dan (la celda del participante, los ajustes comunes). La lista de participantes marca un motor así con un
  *!*, y los motores del match se señalan debajo de ellos;
- un valor de opción que un motor rechaza (fuera de su rango, que no está entre sus valores posibles, una opción que
  ya no tiene): fastchess no lo envía, y el motor juega con su valor por defecto;
- las tablas Syzygy que usa el torneo: las carpetas de la adjudicación y aquello a lo que apunta el `SyzygyPath` de
  cada motor (varias carpetas separadas por `;` en Windows, `:` en los demás sistemas). Para las piezas usadas (las
  *Máx. piezas* de la adjudicación, o si no las tablas más grandes encontradas), todos los archivos WDL (`.rtbw`)
  deben estar, y ningún archivo cortado (un archivo Syzygy mide 16 bytes más que un múltiplo de 64; los motores
  ignoran un archivo de otro tamaño, y algunos se detienen cuando lo encuentran): *Syzygy 6 piezas: faltan 14
  archivos en D:\TB (KRPPvKR.rtbw…)*, *Adjudicación hasta 6 piezas, pero solo hay archivos de 5 piezas*,
  *SyzygyPath de Alpha apunta a una carpeta sin archivos de tablas*, con *Abrir la carpeta* y *Bajar a 5 piezas*;
  la descripción emergente lista los archivos. Son avisos: la adjudicación y los motores se las arreglan sin los
  archivos que faltan. Solo una adjudicación sin carpeta, o cuyas carpetas no contienen ninguna tabla, detiene el
  torneo al empezar (fastchess la rechaza): *Localizar…* elige otra carpeta. Las carpetas se leen en segundo plano,
  y de nuevo cada pocos segundos mientras se muestra el resumen.

Un inicio rechazado nombra los mismos problemas, con sus soluciones y *Abrir el registro*. Cada problema tiene un
**?** que abre su entrada en [Solución de problemas](#solución-de-problemas).

### Perfiles y plantillas
<!-- en: profiles-and-templates df08e65d -->

**Perfiles.** Las cinco primeras filas, *Formato*, *Ritmo de juego*, *Aperturas*, *Adjudicación* y *Ajustes de los
motores*, son cada una un perfil con nombre: su lista elige uno, la línea de al lado dice qué juega («40 jugadas en
15 min, repetido», «tablas 40/8/10 cp · TB 6 piezas»; su descripción emergente da lo mismo tal como lo escriben
cutechess y fastchess, `tc=40/900`, `-draw movenumber=40 movecount=8 score=10`), y ✎, o un clic en esa línea, abre
los detalles ahí mismo, debajo. Un cambio hecho solo para este torneo pone un punto junto al perfil, y su lista y sus
detalles ofrecen *Actualizar* (el perfil adopta el cambio, para todos los torneos y plantillas que lo usan),
*Revertir* y *Guardar como perfil…* (con un nombre nuevo). El programa incluye perfiles predefinidos:

- ritmos de juego: 10+0.1 y 60+0.6 (los tests cortos y largos de fishtest), 2+1, 40/4, 40/15 y 40/20 (40 jugadas en
  4, 15 o 20 minutos, repetido, como las juegan las listas de rating), y 200k nodos por jugada;
- formatos: SPRT mejora [0, 3] y no regresión [-5, 0], un match de 1000 partidas, un todos contra todos, un gauntlet,
  un suizo de 9 rondas, una eliminatoria y un encuentro por equipos;
- adjudicaciones: ninguna, una estándar (tablas desde la jugada 40 tras 8 jugadas dentro de 10 cp, derrota tras 3
  jugadas a 600 cp) y la misma con las tablas de finales hasta 6 piezas;
- ajustes de los motores: los de cada motor, o Hash de 16, 64 o 256 MB con 1 hilo y los libros propios de los motores
  desactivados;
- aperturas: sin libro, o las 960 posiciones de Chess960.

Un perfil tuyo guardado con el nombre de uno predefinido ocupa su lugar; eliminarlo (desde su lista, tras una
confirmación) recupera el predefinido, y las configuraciones y plantillas que lo usaban conservan sus valores. Los
perfiles se guardan en `profiles.json` en la carpeta del perfil, y la paleta de comandos (Ctrl+K) los lista: elegir
uno lo aplica a la configuración. La paleta también abre cada fila de la configuración por su título, y carga o guarda
plantillas. Las carpetas Syzygy no están en ningún perfil: pertenecen a este ordenador, y la adjudicación y los
motores las comparten. Las filas *Concurrencia* y *PGN y otros* contienen lo que es solo de este torneo.

**Plantilla** y **Nombre**, junto a *Iniciar*: una plantilla es la configuración entera, un perfil para cada sección
más lo que ningún perfil contiene (el nombre, los participantes, la concurrencia, los archivos PGN, la
retransmisión). El menú carga una o guarda la configuración como plantilla (*Guardar la configuración como
plantilla…*, que pregunta antes de reemplazar una plantilla con el mismo nombre), y la paleta de comandos también las
carga. Una sección que seguía su perfil cuando se guardó la plantilla sigue ese perfil, así que actualizar un perfil
actualiza todas las plantillas que lo usan; una sección cambiada para ese torneo conserva sus propios valores. Las
plantillas guardadas por versiones anteriores se cargan tal como eran, y sus secciones reciben los perfiles a los que
equivalen. Un nombre vacío se forma a partir de los motores.

### Formato
<!-- en: format 29a36e7c -->

La descripción emergente de cada formato dice qué es.

- *Match*: dos motores, A (el motor a prueba; todas las estadísticas son desde su punto de vista) y B (la
  referencia). Un número fijo de partidas, hasta que lo detengas, o un SPRT: elo0, elo1, alpha, beta, modelo
  (`normalized` usa límites en nElo como fishtest y OpenBench, `logistic` el Elo clásico, `bayesian` BayesElo) y un
  número máximo de partidas opcional. Los perfiles SPRT mejora y no regresión dan los límites habituales, y la línea
  bajo los límites indica cuántas partidas es probable que dure el test. *Calculadora…* dibuja, como la calculadora
  SPRT de fishtest, la probabilidad de que se acepte H1 y el número esperado de partidas frente a la diferencia de Elo
  real, a partir de una proporción de tablas esperada y del sesgo RMS de las aperturas; *Usar estos límites* devuelve
  a la configuración los límites que se le dieron. *Sesgo Elo* (0 por defecto) se suma a ambos límites, para probar
  frente a una versión que se sabe más fuerte o más débil en esa medida: [0, 3] con un sesgo de 5 da el SPRT [5, 8],
  que el resumen, la vista general y el modo sin interfaz gráfica escriben `SPRT [0, 3] + 5`; la calculadora y la
  línea bajo los límites lo tienen en cuenta. *Partidas mín.* (0, ninguna, por defecto) retiene la decisión hasta que
  se juegan esas partidas, con el LLR aún calculado y mostrado, para proteger un test corto de una parada temprana por
  suerte: un LLR más allá de un límite antes de eso se indica bajo la barra del LLR y el match continúa; una vez
  alcanzadas las partidas, el siguiente par que termine con el LLR más allá de un límite termina el match. Un máximo de
  partidas inferior al mínimo termina el match sin decisión. Es la forma más rápida de comparar dos versiones de un
  motor, y no hace falta nada más.
  *Colores* (*Alternados* por defecto) indica quién lleva las blancas: A y B por turnos, o *A siempre con blancas* o *B
  siempre con blancas*, como `-noswap` (y `-reverse`) de fastchess, para partidas con ventaja en las que el motor más
  fuerte debe llevar siempre el bando en desventaja, o una apertura probada desde un solo bando. Las partidas siguen
  yendo por pares, ambas con los mismos colores, pero cada partida tiene su propia apertura, la siguiente del libro
  (como `-games 1` de fastchess): los pares y sus estadísticas se mantienen, la puntuación incluye la ventaja de las
  blancas, y el sesgo RMS, que invertir los colores anula, no se muestra. El resumen y la cabecera del match lo indican.
- *Todos contra todos*: cada motor juega contra todos los demás, *Ciclos* veces, cada vez dos partidas con los
  colores invertidos (lo que cutechess llama rondas, de 2 partidas por encuentro). Las rondas siguen las tablas de
  Berger, así que cada motor juega una vez por ronda.
- *Gauntlet*: los primeros *Motores probados* (los cabezas de serie) juegan contra todos los demás; los demás no
  juegan entre sí.
- *Suizo*: un número de *Rondas*. Cada ronda son dos partidas, con los colores invertidos, contra un motor con una
  puntuación similar; nadie se enfrenta dos veces al mismo rival cuando puede evitarse. Con un número impar de
  motores, el motor peor clasificado que aún no ha descansado recibe un descanso, que vale un punto.
- *Eliminatoria*: los motores se ordenan como cabezas de serie según el orden de su lista, los primeros cabezas de
  serie reciben los descansos cuando el número de participantes no es una potencia de dos, y los dos primeros cabezas
  de serie solo pueden enfrentarse en la final. Un match son *Pares de partidas por match* pares de partidas; un match
  empatado recibe un par más cada vez, hasta diez, tras lo cual pasa el cabeza de serie mejor situado.
- *Encuentro por equipos*: dos equipos de motores, para comparar una familia de motores, o un conjunto de versiones,
  con otra en su conjunto. *Emparejamientos*: *Cada motor contra todos*, cada motor de un equipo contra cada motor
  del otro equipo, un rival por ronda (un sistema Scheveningen; los equipos pueden ser de distinto tamaño), o
  *Tablero a tablero*, el primer motor de cada equipo contra el primero del otro, el segundo contra el segundo, y así
  sucesivamente (equipos del mismo tamaño). Cada emparejamiento se juega *Ciclos* veces, cada vez dos partidas con la
  misma apertura, con los colores invertidos; los colores de la primera partida alternan de tablero en tablero y de
  un ciclo al siguiente. *Equipo A* y *Equipo B* dan nombre a los equipos («Equipo A» y «Equipo B» si no tienen
  nombre); a su lado, cuántos motores tiene cada uno. Los equipos se forman en la lista de participantes: la mitad
  superior de la lista es el equipo A, la mitad inferior el equipo B (el equipo A recibe el impar), hasta que se ponen
  motores en un equipo.

### Los participantes y la biblioteca
<!-- en: participants-and-the-library a7ef2e55 -->

**Participantes**, con la **Biblioteca** al lado (debajo en una ventana estrecha). Cada participante es una línea:
su puesto (una estrella para los motores probados de un gauntlet), su nombre, su Elo de referencia, lo que juega de
forma distinta a los ajustes comunes («Hash 512 MB · 80 % del tiempo») y ⋯, resaltado cuando tiene ajustes propios.
⋯, un clic derecho o la tecla Menú los abre: su propio `Hash`, `Threads`, nodos o profundidad fijos, ventaja de
tiempo (un porcentaje del ritmo de juego, con reloj), su propio *Libro*, *Este motor otra vez, con otro libro*, las
opciones para moverlo y *Quitar*. El orden de la lista determina los cabezas de serie: arrastra los participantes
para reordenarlos (los cabezas de serie primero), o pulsa Ctrl+Arriba o Ctrl+Abajo para llevarlos al principio o al
final, Alt+Arriba o Alt+Abajo para moverlos de uno en uno. Ctrl o Mayús clic selecciona varios; Supr (o *Quitar*)
los quita.

En un encuentro por equipos, el puesto es el equipo y el tablero, A1, A2… para el equipo A, B1, B2… para el equipo B,
en el orden de la lista (tablero a tablero, A1 juega contra B1). El menú ⋯ pone al participante en un equipo;
*Equipo A* y *Equipo B* bajo la lista ponen en uno de ellos a los participantes seleccionados, y *Mitades* vuelve a
formar los equipos a partir de las mitades de la lista (la paleta de comandos ofrece lo mismo). Un motor añadido una
vez elegidos los equipos se une al más pequeño. El resumen dice cuántos motores tiene cada equipo, y un inicio se
rechaza, con el motivo, cuando un equipo está vacío o, tablero a tablero, cuando los equipos son de distinto tamaño.

La biblioteca lista los motores activos con su Elo de referencia, ordenados por nombre o por Elo (haz clic en la
columna), con un `!` en los que tienen algún problema (programa ausente o recompilado, última comprobación fallida: la
descripción emergente dice cuál) y un ✓ en los que ya juegan. El filtro busca en los nombres, los nombres que
comunican los motores, sus autores, programas, notas y grupos (Ctrl+F le da el teclado; Intro en él añade los motores
seleccionados, o el primero mostrado); una etiqueta por grupo muestra solo ese grupo, y *Añadir todos los mostrados*
añade el grupo entero. Haz doble clic en un motor, o selecciona varios con Ctrl o Mayús (Ctrl+A: todos los
mostrados) y pulsa Intro, para añadirlos; o arrástralos a un lugar entre los participantes. *Sugerir…* selecciona
los N motores activos que funcionan y cuyo Elo de referencia está más cerca del del primer participante (el motor a
prueba de un gauntlet), como mucho a 150 de distancia por defecto; Intro los añade. Para un match, la biblioteca
llena A y luego B (B se reemplaza cuando ambos están elegidos), y un motor arrastrado sobre A o B ocupa su lugar.

### Libros propios de los participantes
<!-- en: participants-own-books 580e52cc -->

*Libro*: un libro de aperturas propio del participante, como `book=`/`bookdepth=` de Cute Chess: un libro Polyglot
(`.bin`) o PGN de la página Libros y hasta qué profundidad se sigue, en medias jugadas desde el inicio de la partida,
incluida la apertura del torneo (*todo*: mientras el libro tenga una jugada; para un libro PGN, las primeras 40 medias
jugadas de sus partidas). Un libro PGN se convierte primero en un libro Polyglot: las jugadas de cada partida hasta
esa profundidad, cada jugada ponderada por el número de partidas que la juegan; las partidas con una jugada ilegal se
descartan y se cuentan. Esto se hace en segundo plano en cuanto se elige el libro (con el progreso en el botón
*Libro*, en su ventana emergente y en el resumen) y se guarda en la carpeta `book-cache` del perfil hasta que cambia
el archivo o la profundidad; un inicio que llega antes de que termine lo espera. Un libro que no se puede preparar
muestra por qué en su ventana emergente, con *Reintentar*; cada inicio también lo vuelve a intentar. Un torneo juega
con el libro preparado cuando se inició o se reanudó, aunque el archivo PGN cambie mientras tanto.

Mientras la posición está en ese libro, el participante juega de inmediato la jugada del libro, elegida según los
pesos del libro tal como los modifica la *Aleatoriedad* de las aperturas (también en la ventana emergente *Libro*, la
misma para todos los libros del torneo: consulta [Aperturas](#aperturas)); después toma el relevo su motor. Las
aperturas del torneo, si las hay, se juegan primero, con todos los motores; los libros EPD solo dan esas y aparecen
atenuados en la lista. Para comparar libros con un motor, elige *Este motor otra vez, con otro libro* en su menú ⋯:
cada copia juega como `<engine> (<book>)`, de modo que sus resultados y su rating quedan separados. En un match los
botones *Libro* están junto a A y B, y un motor puede jugar contra sí mismo con dos libros. Las jugadas de libro se
marcan con `{book}` en el PGN; al pasar el ratón sobre un nombre en la clasificación se muestra su libro. Los motores
WinBoard también pueden tener uno.

### Ajustes de los motores
<!-- en: engine-settings 7a93d075 -->

*Hash* y *Threads* impuestos a todos los motores, también en un match (0 = los de cada motor; los propios de un
participante, desde su menú ⋯, tienen prioridad); *Tablas Syzygy para los motores* da las carpetas de tablas de
finales (las mismas que las de la adjudicación) a todos los motores con una opción `SyzygyPath`, y *Libros propios de
los motores desactivados* pone `OwnBook` a false donde existe, para que todas las partidas se jueguen desde las
aperturas del torneo. Ambas opciones están desactivadas por defecto.

### Ritmo de juego
<!-- en: time-control 672cb041 -->

Tiempo base e incremento en segundos, jugadas por periodo opcionales y un margen antes de que una partida se pierda
por tiempo; un tiempo fijo por jugada (`go movetime`, con el mismo margen); un número fijo de nodos por jugada; o una
profundidad fija en medias jugadas (`go depth`), con la que ningún motor puede perder por tiempo.

### Concurrencia
<!-- en: concurrency c02ee2cf -->

El número de partidas jugadas a la vez. En modo automático se mide al iniciar:

- nodos o profundidad fijos: una partida por hilo de hardware, ya que el Hyper-Threading no cambia las jugadas que se
  juegan;
- con reloj: núcleos físicos − 1, para que los motores no se ralenticen unos a otros.

La fila y la línea bajo el campo dicen qué regla eligió el número. *Afinidad de CPU* (desactivada por defecto; no en
macOS, que no la tiene) fija cada partida a un procesador, compartido por sus dos motores ya que nunca piensan a la
vez. fastchess solo la admite para motores de un solo hilo, así que se desactiva automáticamente cuando un motor tiene
`Threads` > 1. Aparece un aviso cuando concurrencia × hilos supera los hilos de hardware (o, con reloj, los núcleos
físicos). *Jugar en* elige la máquina en la que se juega el torneo (consulta [Un torneo en una
máquina](#un-torneo-en-una-máquina)).

### Aperturas
<!-- en: openings 44656411 -->

Un libro EPD, PGN o Polyglot (`.bin`) (una suite de aperturas), orden aleatorio o secuencial, el número de medias
jugadas que se conservan de un libro PGN, el índice de inicio y la semilla aleatoria (0 = un orden nuevo para cada
torneo). Cada apertura se juega dos veces, con los colores invertidos (en un match cuyos *Colores* están fijados, cada
partida desde su propia apertura).

Un libro Polyglot se sigue desde la posición inicial hasta una profundidad (8 medias jugadas si no se fija otra),
dejando fuera las jugadas que pesan menos de una décima parte de la mejor de su posición; las posiciones alcanzadas se
escriben una vez en `<book>-<N>plies.epd` en la carpeta del torneo y se juegan como un libro EPD. En orden aleatorio
se escriben en el orden en que se sortean, cada una una vez: una línea sale tanto antes cuanto más probable es, según
los pesos de sus jugadas, de modo que un torneo que solo juega parte del libro juega primero sus líneas principales;
la semilla vuelve a sortear el mismo orden. En orden secuencial conservan el orden del libro. *Aleatoriedad* (de −100
a 100, 0 por defecto) dice cómo cuentan los pesos, como cuando Polyglot y Cute Chess eligen una jugada de libro: 0 los
pesos tal cual, −100 solo la mejor jugada de cada posición (una línea, salvo que varias compartan el mejor peso), 100
todas las jugadas por igual (vuelven las jugadas raras); entre medias, −50 eleva los pesos al cuadrado y 50 toma sus
raíces cuadradas. También elige las jugadas de los libros propios de los participantes, y se muestra para ellos
incluso con un libro EPD o PGN; el resumen lo indica, y la línea bajo las copias de la clasificación da el libro con
su profundidad y, para un libro Polyglot, esta aleatoriedad.

En un todos contra todos, un gauntlet, un suizo o un encuentro por equipos, *La misma apertura para todos los pares de
una ronda* da a todos los pares de partidas de una ronda la misma apertura (tablero a tablero, un ciclo es una ronda:
todos los tableros juegan la misma apertura), la siguiente del libro en cada ronda, para que ningún motor reciba
aperturas más fáciles que los demás de su ronda (un motor añadido más tarde se pone al día en rondas propias, cada una
con su apertura).

*Marcar las aperturas desequilibradas* (desactivado por defecto) muestra, a medida que llegan las partidas, las
aperturas que deciden la partida por sí solas: una partida se marca al terminar si ambos motores vieron al mismo bando
por delante en al menos la puntuación fijada (200 cp por defecto) en cada una de sus primeras jugadas tras el libro (4
por defecto; las jugadas del libro propio de un participante cuentan como jugadas de libro). Lee las puntuaciones de
los motores, así que un motor que no da ninguna no marca nada; los resultados no cambian. La pestaña *Partidas*
filtra esas partidas (*Apertura desequilibrada*), la pestaña *Aperturas* marca sus aperturas con un `!` y las cuenta,
y la vista general cuenta las partidas. *Volver a jugar el par desde otra apertura* anula un par así una vez jugadas
sus dos partidas (se quedan en los archivos PGN y en el diario, como cualquier partida anulada) y lo vuelve a jugar
desde una apertura del final del libro (en el orden de juego del libro), que el torneo jugaría en último lugar, para
que las aperturas de los pares siguientes no se muevan; como mucho tres veces para un mismo par, tras lo cual el par
cuenta, marcado. La consola y la vista general dicen cuántas aperturas se sustituyeron.

### Chess960
<!-- en: chess960 515f3f2d -->

Fischer random, en las aperturas: sin libro, las 960 posiciones iniciales en orden aleatorio (escritas una vez en
`chess960.epd` en la carpeta de torneos); con un libro, sus posiciones. Todos los motores deben jugar Chess960: los
motores UCI con la opción `UCI_Chess960`, los motores WinBoard con la variante `fischerandom`. El resumen nombra los
que no lo juegan, y un torneo así no empieza.

### Adjudicación
<!-- en: adjudication a18dca1e -->

Tablas (desde un número de jugada, durante N jugadas consecutivas, por debajo de una puntuación), abandono (N jugadas
consecutivas por encima de una puntuación, opcionalmente con ambos motores de acuerdo), un número máximo de jugadas, y
tablas Syzygy.

### PGN y otros
<!-- en: pgn-and-misc 6762bab8 -->

- Nombre del evento: su descripción emergente nombra los dos archivos PGN en los que se escribe cada partida.
- *Añadir también cada partida a*: un archivo PGN tuyo al que cada torneo añade sus partidas, como `-pgnout` de
  cutechess (nunca se sobrescribe, con el estilo de comentarios de ChessBase / Lichess o de cutechess), en el orden
  del calendario: las dos partidas de una apertura una junto a otra, las rondas en orden, sea cual sea el orden en que
  terminen. Una partida espera ahí hasta que se escriben las anteriores; una reanudación continúa; cuando no se puede
  escribir el archivo (bloqueado, movido), las partidas esperan y el torneo continúa, con un aviso en la vista
  general. Un fallo del programa o un corte de luz mientras se añaden partidas nunca las escribe dos veces: la
  reanudación comprueba el archivo y completa una escritura interrumpida, y nunca quita nada de él.
- Campos adicionales en los comentarios de las jugadas (nodes, seldepth, nps, pv, tbhits, y el tiempo de decisión
  `dt=`: cuándo se decidió el motor por la jugada que hizo; `wdl=W/D/L` siempre se escribe para los motores que lo
  comunican), activados por defecto ya que los gráficos de la pestaña *Partidas* los leen (pv hace el PGN
  notablemente más grande).
- *Reiniciar un motor que se cuelga*, y *Detener el torneo cuando un motor no arranca* (desactivado por defecto:
  consulta [Un motor que no arranca](#un-motor-que-no-arranca)).
- Guardar el registro del motor de cada partida (siempre se guarda para las partidas que terminan mal).
- *Comprobar las PV de mate*: la PV de una puntuación de mate debe tener la longitud correcta y terminar en mate.
  Todas las PV se comprueban en busca de jugadas ilegales, jugadas tras el final de la partida y una mejor jugada que
  no es su primera jugada; las jugadas con una PV errónea se marcan en la pestaña Partidas y se escriben en el
  registro.

### Consejos para los ajustes
<!-- en: tips-for-the-settings 62afdb4d -->

- **Comparar dos versiones de un motor**: un SPRT, mejora [0, 3] para un cambio que pretende ganar fuerza, no
  regresión [-5, 0] para una simplificación o una limpieza de código. Primero un ritmo de juego corto (10+0.1), luego
  uno largo (60+0.6) para lo que lo supere. Usa un libro de posiciones equilibradas, no la posición inicial: si no,
  todas las partidas serían una de unas pocas aperturas.
- **Concurrencia**: déjala en automático. Con reloj, más partidas que núcleos físicos menos uno hacen que los motores
  se ralenticen unos a otros, y los resultados se desvían; con nodos o una profundidad por jugada, pueden jugar todos
  los hilos de hardware.
- **Threads**: 1 por motor para probar; muchas partidas de un hilo cada una dan más resultados por hora que pocas
  partidas de muchos hilos. Da a todos los motores los mismos `Threads` y `Hash` en *Ajustes de los motores*.
- **Hash**: lo suficiente para el ritmo de juego (de 16 a 64 MB a 10+0.1, 256 MB a 40/15 y ritmos más largos); el
  resumen avisa cuando el hash de las partidas ya no cabe en la memoria.
- **Adjudicación**: el perfil estándar ahorra tiempo en torneos largos sin cambiar mucho los resultados; desactívala
  para probar los finales de un motor, o usa las tablas de finales.
- **Aperturas**: el orden aleatorio con una semilla fija permite volver a jugar un test exactamente igual; *La misma
  apertura para todos los pares de una ronda* es más justa en un todos contra todos con pocos ciclos.
- **Varios torneos**: da al que más importa un peso mayor en lugar de pausar los demás (consulta [Varios torneos a la
  vez](#varios-torneos-a-la-vez)).

## Un torneo en curso
<!-- en: running-a-tournament 9bcba803 -->

Un torneo en marcha o abierto tiene un espacio de trabajo con una cabecera (nombre, progreso, *Pausa*, *Detener*,
*Clonar*, *Carpeta*, *Disposición*) y paneles, mostrados por defecto como pestañas:

| Pestaña | Qué muestra |
|---|---|
| Vista general | Los resultados hasta ahora y las estadísticas, los gráficos, la lista de comprobación del envío |
| Clasificación | La clasificación como tabla o como tabla cruzada, los rivales de un motor, las copias para un foro |
| Equipos | Un encuentro por equipos: el marcador de los equipos y la tabla cruzada de los dos equipos |
| Cuadro | Una eliminatoria: el cuadro, ronda a ronda |
| Calendario | Todos los pares de partidas, y los cambios en el torneo |
| Retransmisión | Archivos en directo, el servidor TLCS, la página web y lichess |
| Directo | Las partidas en curso |
| Partidas | Las partidas terminadas, sus filtros, su reproducción y su exportación |
| Aperturas | Los resultados por posición inicial |

### Vista general
<!-- en: overview 6985cebb -->

**Un match**: Elo y nElo con su intervalo de confianza del 95 %, LOS, puntuación, recuentos pentanomiales, límites del
SPRT y LLR. *Estadísticas* (plegado) da las cifras de fishtest a partir de las partidas y de los pares: puntuación, Elo,
nElo y BayesElo con DrawElo, desviación típica por partida, el LLR de ambos, la razón de varianzas de los pares frente a
las partidas sueltas, el sesgo RMS de las aperturas, los tipos de pares equilibrados y los pares ganados frente a
perdidos, que *Copiar* incluye. Luego los gráficos: Elo y LLR a lo largo del tiempo, duración de las partidas, cómo
terminaron las partidas y *Búsqueda por motor*.

**Un torneo**: la clasificación (con *TF*, las pérdidas por tiempo de cada motor, en naranja en cuanto hay una; un *!*
junto a un motor que se colgó o hizo una jugada ilegal), *Búsqueda por motor* debajo, el progreso por ciclo o ronda, las
partidas por hora y el tiempo restante, los últimos resultados.

*Búsqueda por motor* da la media de cada motor en las jugadas que buscó, sin las jugadas de libro: profundidad, NPS,
tiempo por jugada, tiempo de decisión y uso del procesador; un clic en una columna ordena los motores por ella. Junto a
sus NPS, ×4 indica que el motor busca con 4 hilos: cuenta más nodos que con uno sin ser por ello más rápido, así que
compara sus NPS con los de motores que tengan los mismos. Los NPS necesitan motores que los den, y *nps* en *PGN y
otros* para volver a leerse al reanudar.

Ambos cuentan las partidas de aperturas desequilibradas, y las aperturas sustituidas, cuando la configuración las
busca, con *Mostrar* para esas partidas. Un torneo terminado añade una *Lista de comprobación del envío*: todas las
partidas jugadas, cada par con ambos colores, sin partidas duplicadas, sin cuelgues, pérdidas por tiempo ni jugadas
ilegales (con botones que abren esas partidas), el hash de cada motor (mostrado, no juzgado), ponder desactivado, el
libro de aperturas, las tablas de finales.

### Clasificación
<!-- en: standings e4198fcd -->

Puntos, partidas, pérdidas por tiempo (*TF*), victorias/tablas/derrotas, ratings con su intervalo de confianza,
Sonneborn-Berger (Buchholz en un suizo), descansos. *Tabla* los lista con un motor por fila; *Tabla cruzada* (todos
contra todos, hasta 200 motores) muestra cada emparejamiento. *Orden:* por puntos, puntuación % u orden inicial (que
mantiene en su sitio las columnas de la tabla cruzada de un gauntlet). Un clic en un motor lista debajo sus rivales:
partidas, con blancas, con negras, + = −, puntos y %, con los pares de los que solo se jugó una partida marcados; un
gauntlet se abre con su motor probado seleccionado.

*Copiar* como texto, o en BBCode para un mensaje de foro (con una línea que da el formato, el ritmo de juego, las
partidas y el libro), y en un gauntlet *Tabla del gauntlet en texto* o *en BBCode, para un foro* (el motor probado,
luego sus rivales según sus propios puntos, cada uno con puntos, %, Elo ±, W-L-D); *Exportar HTML…*. Todos tienen
*TF* como la clasificación del modo sin interfaz gráfica, siguen el orden elegido y terminan con una línea que da las
partidas jugadas de las previstas, la proporción de tablas, el ritmo de juego, el libro (con las medias jugadas
conservadas de un libro PGN; para un libro Polyglot, su profundidad y cómo se eligieron sus jugadas, p. ej. `book:Perfect2023.bin (8
plies, moves by weight)`) y el evento. Las copias firman entonces con *fastchess-gui* y su versión, y la tabla del
gauntlet usa = en lugar de - para las líneas, para que una tabla publicada en un foro se distinga de las de otros
programas.

### Equipos
<!-- en: teams 8c845645 -->

Solo en un encuentro por equipos, en la pestaña *Clasificación* (donde se abre) y en lo alto de la *Vista general*:
el marcador de los equipos, «Lynx 17½ – 18½ Otter», con las partidas jugadas, la puntuación % del equipo A, la
diferencia de Elo que representa esa puntuación con su intervalo del 95 % y el LOS, contados sobre los pares de
partidas como las estadísticas de un match (pentanomial); luego los motores de cada equipo con sus partidas, puntos,
%, rating y los puntos que cada uno sumó contra cada motor del otro equipo (B1, B2… como columnas, el nombre bajo el
cursor): la tabla cruzada de los dos equipos. *Tabla* y *Tabla cruzada* muestran los motores como en cualquier
torneo. *Copiar* y *Exportar HTML…* dan los marcadores de los equipos y los motores de ambos equipos enfrentados entre
sí, con la línea de debajo.

### Cuadro y calendario
<!-- en: bracket-and-schedule fe207ac0 -->

*Cuadro*, solo en una eliminatoria: el cuadro, ronda a ronda, con el marcador de cada match. *Calendario*: cada par
de partidas por ciclo o ronda, con su estado y la apertura que juega (una vez empezado, o una vez empezada su ronda
cuando la ronda comparte una), y los cambios en el torneo (consulta [Modificar un torneo en
curso](#modificar-un-torneo-en-curso)).

### Directo
<!-- en: live 5561ed9b -->

Las partidas en curso, como un mosaico de tableros o una partida en primer plano con los relojes, la búsqueda en curso
de cada motor (profundidad, puntuación, nodos, nps, PV en SAN), la lista de jugadas y el gráfico de la partida (consulta
[El gráfico de la partida](#el-gráfico-de-la-partida)): las evaluaciones de los dos motores, y en sus pestañas sus
posibilidades de victoria, el tiempo por jugada con el tiempo de decisión, la profundidad con la seldepth, los nodos,
la velocidad, los accesos a tablas de finales, cuánto se separan, o cuántas medias jugadas de lo que predijo la PV del
otro motor repite una PV. Las proporciones de victoria/tablas/derrota (`UCI_ShowWDL`, activado para los motores que lo
tienen cuando se añaden) siguen cada línea de búsqueda; la pestaña *V/T/D* las dibuja como bandas, con las victorias de
las blancas abajo.

Cuando hay sitio (una partida en primer plano, un panel grande), cada motor tiene su tarjeta: su logo tan alto como su
nombre y su reloj, lo que hace (piensa, o la jugada que hizo), su búsqueda cifra por cifra (profundidad y seldepth,
evaluación, nodos, velocidad, los accesos a tablas de finales y el llenado del hash cuando el motor los envía, el tiempo
dedicado a la jugada hasta ahora) y toda su PV; el motor que espera muestra lo que decía al jugar, con el momento en que
se decidió. Junto al mosaico, el panel conserva una línea de cifras y las 12 primeras medias jugadas de la PV.
*Evaluación del bando que juega* | *Evaluación de las blancas*, a la derecha de la pestaña, elige el lado de todas sus
evaluaciones: el del bando que juega, como fastchess las escribe en el PGN, o el de las blancas; la barra de evaluación
y la pantalla grande siempre muestran el de las blancas.

Encima de las jugadas: la apertura, el código ECO y el nombre de la posición más avanzada de la partida que conoce la
tabla de aperturas con nombre (jugadas del libro y de los motores por igual, de modo que también se nombra una
transposición después del libro), con la última jugada del libro; y en cuanto las tablas Syzygy del torneo (las de su
adjudicación, si no las que se dan a los motores) conocen la posición mostrada, su veredicto: el bando que gana con la
DTZ (las medias jugadas hasta la próxima captura o movimiento de peón: las tablas Syzygy no tienen distancia al mate), o
el empate, con la regla de los 50 movimientos contada desde el contador de la partida salvo que la adjudicación la
ignore. Cada posición se consulta una vez, fuera de los hilos que juegan las partidas. En la lista, cada jugada de un
motor tiene su evaluación, su profundidad y su tiempo; al pasar el ratón se añaden el momento en que el motor se decidió
y la respuesta que esperaba (la segunda jugada de su PV: fastchess juega sin ponder). Las jugadas del libro dicen
*libro*, una línea marca el fin del libro, y la jugada en la que se piensa dice *piensa*.

El mosaico tiene un tablero por partida en curso, no por partida que permita la concurrencia: hacia el final de un
torneo, o con menos partidas que eso, los tableros que quedan crecen. Un tablero cuya partida terminó se queda 10
segundos (la siguiente partida de su hueco empieza ahí), el de la partida seguida se queda, y cuando no se juega nada,
se quedan las últimas partidas. Cuando los tableros necesitarían una barra de desplazamiento, el mosaico le quita
anchura a la partida en primer plano, hasta dejarle el 40 % de la página.

Bajo el tablero: el contador de las 50 jugadas (medias jugadas desde la última captura o jugada de peón, sobre 100),
la diferencia de material con las piezas que un bando tiene de más, y las piezas que quedan, marcadas *TB* cuando la
adjudicación por tablas puede terminar la partida; *Detalles bajo los tableros* los añade al mosaico. Debajo, los
botones para recorrer las jugadas (consulta [Los tableros y sus botones](#los-tableros-y-sus-botones)): la partida en
primer plano, o un tablero separado, puede retroceder mientras la partida continúa (también con ← → Inicio Fin, un
clic en una jugada de la lista o en la curva), con la posición anterior mostrada y su jugada marcada en la lista y en
la curva; *Volver a la jugada actual* (o Fin, o un clic en la última jugada) hace que vuelva a seguir la partida, como
hace en cada partida nueva.

*Flechas* (activado por defecto) dibuja la jugada en la que piensa el bando al que le toca mover en el color de su
motor y la respuesta que espera, más clara, en el color del rival, en el tablero en primer plano y en los separados
(no en el mosaico), cuando muestra la jugada actual. En un gauntlet, *Motor probado abajo* (activado por defecto)
muestra cada partida nueva desde el lado del motor probado; girar el tablero (F, o su botón) sigue dándole la vuelta
hasta la siguiente partida. *Separar* abre un tablero en su propia ventana. Un clic derecho en un tablero termina su
partida con un resultado (consulta [Modificar un torneo en curso](#modificar-un-torneo-en-curso)). *Pantalla grande*
muestra la partida seguida llenando la ventana (consulta [Pantalla grande](#pantalla-grande)).

### Pantalla grande
<!-- en: big-screen 14c8f0d6 -->

Para una televisión, un club o un stream, *Pantalla grande* (en la pestaña *Directo*, en un tablero separado o en la
paleta de comandos) muestra una partida llenando la ventana, en claro sobre negro: los nombres de los jugadores y los
relojes en letra grande (el bando al que le toca mover y su reloj en marcha en amarillo, un reloj por debajo de diez
segundos en rojo), la barra de evaluación, la evaluación, la profundidad y la velocidad de la búsqueda, la última
jugada enmarcada en el tablero y escrita en grande, la apertura, las líneas de los motores (*Líneas*, activado por
defecto: la del bando que juega mientras busca, en amarillo, la del otro desde su última jugada, dos renglones cada una
como mucho), el veredicto de las tablas de finales cuando conocen la posición, y las jugadas en letra grande, con la
última marcada; el
resultado sobre el tablero una vez terminada la partida. Su texto sigue el tamaño de la ventana y tiene un tamaño
propio (*A-* / *A+*, o las teclas + y -). Muestra la partida seguida, o va pasando de una partida en curso a otra cada
N segundos (*Cada partida por turnos*), o sigue *La más interesante*: la más cercana a la igualdad, la más larga de
las que están igual de cerca, conservada mientras siga siéndolo. Los controles aparecen durante tres segundos cuando
se mueve el ratón (el puntero se oculta con ellos); ← y → cambian de partida, F11 pone la ventana a pantalla
completa, Esc sale. Un tablero separado llevado a otra pantalla (la televisión) la muestra en su propia ventana, y F11
pone esa ventana a pantalla completa mientras la ventana principal sigue funcionando; acoplado en la ventana
principal, la muestra la ventana principal.

### Partidas
<!-- en: games ba4a9604 -->

Las partidas terminadas con su apertura (código ECO y nombre), y un tablero de reproducción: los botones de debajo, ←
→ Inicio Fin y F, o un clic en una jugada o en el gráfico; los mismos detalles debajo y el mismo gráfico de la partida
que en *Directo* (consulta [El gráfico de la partida](#el-gráfico-de-la-partida)), a partir de los campos con los que
se escribió el PGN: una pestaña cuyo campo no escribió el torneo está en el menú `⋯` del gráfico, atenuada, y lo
indica con un botón hacia las opciones PGN de la configuración.

**Filtros**: por resultado (victorias, tablas, derrotas, pérdidas por tiempo, cuelgues y jugadas ilegales,
adjudicaciones, PV erróneas, y las partidas de una apertura marcada como desequilibrada cuando la configuración las
busca); por lo que dicen las evaluaciones (los motores discrepan en N peones durante 3 jugadas seguidas de cada uno,
la evaluación de un motor cambia en N peones de una de sus jugadas a la siguiente, el perdedor creía estar N peones
mejor; la partida se abre en ese momento); y por apertura (un código ECO o su comienzo, un rango como `B20-B99`, o
palabras del nombre).

**Exportación** de una, de todas o de las partidas filtradas: *Exportar todo* propone `<name>.pgn` junto a la carpeta
del torneo; los propios archivos PGN del torneo se rechazan como destino. *Exportar para una lista de rating…*
escribe las partidas tal como las envían los testers de las listas de rating (p. ej. CCRL):
`[Tester YYYY-MM-DD] Event (hash NMB) (book X) (egtb N-man).pgn`, también en zip si se pide; los jugadores nombrados
como los escriben las listas, `<engine> 64-bit 4CPU` (editable para cada participante), las rondas renumeradas, las
etiquetas `Event` y `Site` rellenadas, sin las partidas anuladas ni los duplicados, y tampoco las perdidas por un
cuelgue o un bloqueo salvo que se desmarque; el `PlyCount` de una partida perdida por una jugada ilegal solo cuenta
las jugadas escritas. El tester y el lugar se recuerdan.

*Registro del motor* abre el registro de una partida que lo tiene (consulta [Archivos](#archivos)). Un clic derecho
en una partida la vuelve a jugar, la anula o fija su resultado (consulta [Modificar un torneo en
curso](#modificar-un-torneo-en-curso)).

### La pestaña Aperturas
<!-- en: the-openings-tab c51dd29a -->

Resultados por posición inicial: sus pares de partidas, la puntuación de las blancas, las tablas, y cuántos pares
ganaron dos veces las blancas, dos veces las negras (la apertura favorece a un color) o dos veces un mismo motor
(distingue a los motores), con la duración media. Se ordenan por cualquier columna y se filtran por las
desequilibradas, las de tablas o las decisivas, o por las *Marcadas como desequilibradas durante las partidas*
(consulta [Aperturas](#aperturas) en la configuración: marcadas con `!`, su número junto al recuento, con las
aperturas sustituidas). *Exportar como EPD…* escribe las posiciones mostradas, para conservarlas o para limpiar un
libro, y *Sin las desequilibradas* (activado por defecto, visible en cuanto se marca una apertura) deja fuera las
marcadas. Un clic muestra la posición y sus partidas. En un match cuyos *Colores* están fijados, cada partida tiene su
propia apertura: se cuentan las partidas, y las columnas y filtros sobre los pares no se muestran.

### Paneles y disposiciones
<!-- en: panels-and-layouts 9ed9b915 -->

Cada pestaña es un panel. Arrastra uno por su pestaña para acoplarlo a un lado de otro panel o como pestaña junto a
él, o suéltalo en otro sitio para que flote (también en una segunda pantalla en Windows y Linux/X11, no en Wayland ni
con el renderizado por software). La partida seguida en *Directo* se separa en los paneles *Tablero*, *Jugadores*,
*Búsquedas*, *Jugadas* y *Gráfico*; las partes que no tienen un panel abierto se quedan junto al mosaico en
*Directo*, y los tableros separados se acoplan como los demás paneles. Cierra un panel con la x de su pestaña;
recupéralo desde *Disposición* > *Paneles*, o con *Mostrar: …* en la paleta de comandos, y vuelve acoplado.

*Disposición* también ofrece disposiciones predefinidas (*Pestañas*, como por defecto; *Seguir una partida*: el
tablero a la izquierda, los jugadores, las búsquedas y el gráfico a la derecha, la clasificación debajo; *Torneo
grande*: el mosaico junto a la clasificación; *Retransmisión*: un tablero grande con los jugadores, las búsquedas y la
clasificación), *Guardar la disposición como…* (las tuyas, con nombre, guardadas en `layouts.json` en el perfil),
*Eliminar* y *Disposición por defecto*. Un panel sacado del espacio de trabajo sigue en pantalla en las demás
páginas; la disposición se conserva entre sesiones.

### Uso del procesador
<!-- en: processor-use e5234cbe -->

Mientras se juegan partidas, el tiempo de procesador de los procesos de cada motor (incluido el motor que hay detrás
de un adaptador WinBoard) se mide en cada `go` y en cada jugada. La Vista general muestra, para cada motor, los
núcleos que mantiene ocupados mientras piensa y la parte de un núcleo que usa mientras piensa su rival, y avisa
cuando un motor usa claramente más núcleos que sus `Threads`, o piensa durante el tiempo de su rival (un ponder que
no se le pidió, un hilo en segundo plano): lo que hace una partida injusta para el rival y para las demás partidas en
curso. Necesita 20 s de cada cosa para saberlo, y cuenta todas las partidas jugadas, en la ventana o sin interfaz
gráfica (`--run`): lo que usaron los motores de cada partida se escribe en `processor.tsv` en la carpeta del torneo,
así que una reanudación lo conserva. Solo avisa; nunca decide una partida.

### Pausar, detener y reanudar
<!-- en: pause-stop-and-resume 100c55cf -->

**Pausa** no inicia ningún par de partidas nuevo. Las partidas en curso se juegan hasta el final, y también la
segunda partida de un par cuya primera partida ha empezado, así que una pausa nunca desperdicia un resultado.
**Reanudar** continúa. **Detener** termina primero las partidas en curso, o detiene ahora (las partidas
interrumpidas no se cuentan y se volverán a jugar).

Un torneo detenido por un motor dice cuál en su cabecera y en la página de inicio, bajo *Requiere atención*:
*Interrumpido: Alpha no pudo arrancar: …* (tras su último intento), o *Alpha se colgó o dejó de responder (partida
12)* (sin *Reiniciar un motor que se cuelga*), con *Abrir el motor* y *Abrir el registro*.

**Cada torneo se reanuda exactamente donde se detuvo**, tras una detención, una ventana cerrada, un fallo o un corte
de luz: no se pierde ninguna partida ni se juega ninguna dos veces, y un par de partidas a medio jugar se completa con
la misma apertura y los mismos colores. *Reanudar* en la página de inicio lo continúa, con las estadísticas de las
partidas ya jugadas: resultados, y la profundidad, el tiempo por jugada y los NPS de los motores leídos de los
comentarios de las jugadas (los NPS solo cuando el PGN los registra, *nps* en *PGN y otros*). Cuando el programa
terminó mientras se jugaba un torneo (un fallo, un corte de luz, un reinicio, o salir mientras se jugaba), el
siguiente inicio lo reanuda por sí solo, y la cola continúa después: *Reanudar al iniciar el torneo que estaba en
curso*, en **Ajustes**, activado por defecto. Un torneo en pausa o detenido te espera, y `--autostart` inicia en su
lugar la configuración guardada. Un participante cuyo programa se recompiló o desapareció desde que se incorporó se
nombra en la cabecera del torneo reanudado, con *Continuar* y *Pausa*: sus siguientes partidas las juega el programa
tal como es ahora, con las opciones que le dio el torneo.

### Varios torneos a la vez
<!-- en: several-tournaments-at-once df7ce566 -->

Varios torneos pueden jugarse en paralelo: un gauntlet junto a un SPRT, dos gauntlets a la vez. *Iniciar* en la
página *Nuevo torneo* inicia otro mientras se juegan otros; *Reanudar* reanuda uno junto a ellos. Cada torneo se
juega en un proceso propio (`fastchess-gui --run <folder>` dirigido por la ventana), así que un fallo solo tumba ese
torneo, y la ventana, sus comprobaciones de motores, sus pruebas rápidas y sus suites de tests ya no esperan a que
termine un torneo.

El espacio de trabajo tiene **una pestaña por cada torneo abierto**, encima de su cabecera: un punto mientras se juega
(un signo de pausa mientras está en pausa), su estado bajo el cursor, una cruz para cerrar uno que no se está
jugando. Los paneles muestran la pestaña elegida; la paleta de comandos lista las demás (*Mostrar: …*, *Pausa*,
*Reanudar*). Un torneo terminado abierto desde la lista ocupa la pestaña de uno que solo se estaba consultando. La
página de inicio tiene una tarjeta por cada torneo en juego, con su progreso, su líder, *Abrir* y *Pausa*, y debajo
su **peso**, los núcleos que usan sus partidas de los que puede usar, y lo que espera su siguiente partida; la barra
*Núcleos* de arriba muestra la parte de cada torneo (y la de la página Tablero), y cuántos núcleos comparten los
torneos. El *Registro* muestra las líneas de un torneo, las de la ventana, o todas (cada línea de un torneo lleva su
nombre); las líneas de un torneo también se guardan en `fastchess-gui.log` en su carpeta.

**Compartir la máquina**: los torneos en juego comparten un presupuesto de núcleos, fijado en **Ajustes** (*Núcleos
para los torneos*: los núcleos físicos menos uno por defecto). Cada uno recibe primero lo de una partida mientras el
presupuesto lo permita, empezando por el de mayor peso; el resto se reparte según el peso (un torneo de peso 2 recibe
el doble de núcleos que uno de peso 1), ninguno por encima de su propia concurrencia, y lo que uno no puede tomar va a
los demás. Una partida cuenta como los `Threads` de aquel de sus dos motores que tenga más: uno piensa mientras el
otro espera. Un torneo en pausa conserva solo lo que necesitan sus partidas en curso y las segundas partidas de sus
pares. Una parte menor nunca interrumpe una partida: el torneo juega menos partidas a partir de la siguiente. Un
torneo solo juega tantas partidas como indica su concurrencia.

**Memoria**: una partida solo empieza cuando el `Hash` de sus dos motores, y lo que ocupa además cada proceso de motor
(64 MB, y 8 MB por hilo), caben en lo que las partidas en juego dejan del presupuesto de memoria (*Memoria para los
motores* en **Ajustes**, el 75 % de la memoria instalada por defecto), compartido entre los torneos. El resumen de la
configuración dice cuántas partidas permite jugar a la vez el presupuesto, y la tarjeta *Fiabilidad* de la página de
inicio, cuándo hay partidas esperando memoria.

**GPU**: un motor marcado con *Usa una GPU* en su ficha (un motor de red neuronal como Lc0) cuenta frente a las *GPU*
de **Ajustes**: no juegan a la vez más motores así que GPU hay, en un torneo y entre torneos, mientras las demás
partidas se juegan a su lado. El resumen de la configuración dice cuántas partidas con un motor de GPU permite eso, y
avisa cuando no se cuenta ninguna GPU, o cuando una partida entre dos motores de GPU necesita más GPU de las que tiene
la máquina (entonces se juega sola).

**Mantenerlos en marcha**: cerrar la ventana mientras se juegan torneos pregunta qué hacer. *Dejarlos en marcha y
salir* (la opción por defecto) deja cada uno jugándose en su proceso; la ventana los vuelve a mostrar, tal como estén,
en su siguiente inicio. *Detener y salir* los detiene, cada uno para reanudarse exactamente donde se detuvo. La prueba
rápida se detiene en ambos casos. Un torneo cuyo proceso ya no existe en el siguiente inicio (un reinicio, un fallo)
se reanuda como cualquier torneo interrumpido. La retransmisión de la pestaña *Retransmisión* funciona en la ventana,
para el torneo mostrado: para una retransmisión que continúe sin la ventana, inicia el torneo desde un terminal con
las opciones de retransmisión (consulta [Modo sin interfaz gráfica](#modo-sin-interfaz-gráfica)).

### Un motor que no arranca
<!-- en: an-engine-that-cannot-start 765a7c61 -->

Una noche desatendida no se detiene por un motor. Un motor que no arranca (un programa movido o recompilado, un
archivo de red que falta, una unidad desaparecida) se vuelve a intentar tres veces, con 10 s de intervalo; luego sus
partidas se apartan y las demás partidas continúan. Se vuelve a intentar cada 5 minutos, una partida cada vez, y sus
partidas vuelven en cuanto arranca. La cabecera del torneo y la página de inicio (bajo *Requiere atención*) dicen qué
motor, el error, y ofrecen:

- **Reintentar**: de inmediato, en lugar de en el siguiente intento (después de arreglar el programa, por ejemplo).
- **Retirar…** y **Reemplazar…** (todos contra todos, gauntlets y encuentros por equipos): el panel de cambios de la
  pestaña Calendario, preparado para ese motor, con el impacto mostrado antes de que nada cambie.
- **Registro del motor**: `logs/start-<engine>.log` en la carpeta del torneo, lo que se envió a los motores en su
  último intento y lo que respondieron, incluida su salida de error (a menudo el motivo: una biblioteca o un archivo
  de red no encontrados).

El registro y la notificación del escritorio indican cuándo se aparta un motor. El torneo solo se detiene cuando no
queda nada por jugar sin los motores apartados, tras un último intento de cada uno; su estado y la notificación del
final lo indican (*Detenido: no queda ninguna partida por jugar sin …*), y la cola continúa con el siguiente torneo.
*Reanudar* vuelve a intentarlo con todos los motores. Una pausa retiene los intentos. *Detener el torneo cuando un
motor no arranca*, en *PGN y otros* de la configuración, lo detiene en cambio de inmediato, una vez agotados los
intentos.

### Modificar un torneo en curso
<!-- en: changing-a-running-tournament aebe63e2 -->

El panel **Modificar el torneo** de la pestaña *Calendario* funciona sin detener nada: las partidas en curso
continúan y no se pierde ninguna partida jugada. Cada cambio muestra su impacto antes de aplicarse.

- **Añadir un motor**: juega los ciclos ya empezados (*Recuperar los ciclos ya empezados*) o se incorpora a partir
  del siguiente ciclo.
- **Retirar un motor**: sus partidas restantes se descartan; sus partidas jugadas hasta ahora cuentan o no.
- **Sustituir un motor**: sus partidas dejan de contar y las vuelve a jugar el nuevo motor.
- **Número de ciclos**: más o menos.
- **Volver a jugar partidas**: todas las partidas de un motor, o las pérdidas por tiempo y los cuelgues.
- Clic derecho en una partida (aquí, o en la pestaña *Partidas*): volver a jugarla, anularla (sin volver a jugarla)
  o fijar su resultado.

Un motor añadido o que entra como sustituto juega con los ajustes del torneo (su ritmo de juego, Hash y Threads,
mostrados bajo el motor), o con *Ajustes propios*: Hash, Threads, parte del tiempo (o nodos, profundidad), como los
ajustes propios de un participante en la página *Nuevo torneo*. Un torneo creado antes de la 0.6.6 no guardaba sus
ajustes: los de su primer motor ocupan su lugar, y podían ser los propios de ese motor.

En un encuentro por equipos, un motor añadido se une al equipo elegido y juega contra todos los motores del otro
equipo; tablero a tablero no se puede añadir un motor (su tablero no tendría rival): sustituye uno en su lugar. Un
sustituto ocupa el equipo y el tablero del motor al que sustituye; más ciclos emparejan a los motores que siguen
jugando (tablero a tablero, en los tableros del primer ciclo). Las partidas que jugó un motor retirado o sustituido
siguen contando para su equipo, salvo que se anulen.

**Fijar el resultado** decide una partida que el programa no supo decidir bien: un bloqueo que no detectó, un final
ganado en el que los motores dan vueltas, una partida que terminó por un motivo equivocado. Elige 1-0, ½-½ o 0-1 e
indica un motivo; los puntos que mueve se muestran antes de que *Fijar el resultado* lo aplique. La clasificación, la
tabla cruzada, los ratings y la lista de comprobación del envío cuentan el nuevo resultado (una adjudicación: una
pérdida por tiempo cambiada a tablas ya no cuenta como tal); la pestaña *Partidas* lo marca con un `*`, con su
descripción emergente indicando el motivo y cómo terminó la partida tal como se jugó, y *Volver al resultado jugado*
lo revierte. Una partida en juego se termina del mismo modo: clic derecho en su tablero en la pestaña *Directo*,
*Terminar con un resultado* (también en la paleta de comandos para la partida seguida). El motor al que le toca mover
se detiene, fastchess termina la partida de inmediato y esta cuenta con el resultado elegido; el motor vuelve a
arrancar para su siguiente partida. Necesita *Reiniciar un motor que se cuelga* (configuración, *PGN y otros*,
activado por defecto).

Las partidas anuladas y los resultados fijados a mano se quedan en los archivos PGN y en el diario tal como se
jugaron: a los archivos PGN solo se les añade contenido. Lo que lee las partidas los aplica: las partidas anuladas se
descartan, y un resultado fijado a mano aparece en todas las exportaciones (*Exportar la partida…*, *Exportar
todo…*, *Exportar para una lista de rating…*, la exportación de cara a cara de la página *Clasificaciones*, el
archivo de *Añadir también cada partida a*) con `[Termination "adjudicated by the operator"]` y el motivo en un
comentario antes del resultado. **Deshacer el último cambio** se ofrece hasta que empieza la siguiente partida:
todavía no se ha jugado nada con el cambio, así que puede revertirse exactamente (las partidas que ya estaban en curso
cuando se hizo se conservan). Un resultado fijado a mano puede deshacerse mientras sea el último cambio, se hayan
jugado partidas desde entonces o no (en una eliminatoria, cuyas rondas siguientes dependen de los resultados, hasta
que empieza la siguiente partida); la pestaña *Partidas* también ofrece *Deshacer* junto a la partida.

### Cola y relanzamiento
<!-- en: queue-and-relaunch 382fd33e -->

La **Cola** de la página de inicio contiene torneos que se juegan uno tras otro: cuando uno termina por sí solo,
empieza el siguiente (no tras una detención o un error, ni mientras *Pausar todo* retiene la cola). Con *Empezar
cuando haya núcleos libres*, el siguiente empieza junto a los que se están jugando en cuanto el presupuesto de núcleos
supera lo que estos pueden tomar, cada uno con su concurrencia. Se pueden reordenar o quitar, e iniciar ahora el
primero cuando no hay nada en marcha. La página de inicio indica cuándo termina el torneo en curso (*Termina hacia
HH:MM (en …)*) y cuándo termina toda la cola, cada torneo en cola con su duración y, mientras se conozcan las
duraciones de los anteriores, cuándo empieza; los torneos cuya duración no se puede conocer (un SPRT sin máximo, nodos
o profundidad por jugada) se cuentan aparte.

**Relanzar con…** en un match terminado (en su cabecera, o bajo *Últimos terminados* en la página de inicio) lo
vuelve a jugar con otro motor A, normalmente la siguiente versión, contra el mismo motor B, con los demás ajustes del
match (los de la página *Nuevo torneo* para un match iniciado por una versión anterior, que no los guardaba).

**Clonar** (en la cabecera de un torneo, bajo *Últimos terminados* y en *Todos los torneos* de la página de inicio)
abre la página *Nuevo torneo* rellenada con la configuración de ese torneo, sea cual sea su formato: los participantes
con sus propios ajustes, el ritmo de juego, las aperturas, la adjudicación y lo demás, cada valor tal como se jugó. Sus
perfiles aparecen con su nombre, y un perfil modificado desde entonces se marca como cambiado para este torneo. Cambia
lo que deba ser distinto (otro ritmo de juego, la siguiente versión de un motor, otro libro) y luego **Iniciar** o
*Añadir a la cola*. Un torneo que aún se juega también se puede clonar; uno iniciado por una versión anterior, que no
guardaba su configuración, no.

## Clasificaciones
<!-- en: ratings 3e4be48f -->

La página **Clasificaciones** calcula ratings de máxima verosimilitud (el modelo logístico, las tablas como medio
punto, como hace Ordo por defecto) sobre todas las partidas de todos los torneos del perfil, y de los archivos PGN
importados. Los motores se identifican por su nombre. Las partidas contadas pueden acotarse en la parte superior de la
página, para todas las pestañas:

- un ritmo de juego, un grupo de la biblioteca de motores (las partidas entre sus motores), los últimos 30 o 365 días;
- *Partidas importadas*: las partidas de los archivos PGN listados en *Archivos PGN…* (la descarga de una lista de
  rating, las partidas de otro programa), leídas de sus cabeceras; sus motores se reconocen por los nombres que les
  da el PGN, y un nombre que es el de un motor de la biblioteca, sin contar mayúsculas ni espacios («stockfish  17»
  por «Stockfish 17»), es ese motor: sus partidas cuentan como las del motor y su Elo de referencia puede anclarlas.
  No se aplica por sí sola ninguna coincidencia más laxa: «Stockfish 17 64-bit» sigue siendo un motor aparte hasta que
  digas qué motor es en *Jugadores PGN…* (junto al recuento de motores de la biblioteca, o en *Archivos PGN…*). Esa
  ventana lista por su nombre los jugadores que no son ningún motor de la biblioteca, con una búsqueda: para cada uno,
  *Un motor aparte* (la opción por defecto) o un motor de la biblioteca. Un nombre que comparten dos motores de la
  biblioteca, sin contar mayúsculas, se muestra en el color de aviso para que elijas; un motor probable (su nombre es
  el comienzo del del jugador) se sugiere como *¿Quizá …?*, y nunca se aplica. La ventana se abre por sí sola tras
  añadir un archivo PGN cuando hay algo que decidir. Las elecciones, incluida la de un motor aparte, se guardan con el
  motor (siguen a un cambio de nombre) y se aplican a todos los archivos PGN y a las listas de rating importadas para
  el Elo de referencia; las partidas se vuelven a leer con ellas al cerrar la ventana. La lista de rating dice cuántos
  motores de la biblioteca contienen los archivos PGN, y su descripción emergente, cuáles;
- *Ancla*: la media en 0, o un motor con un rating fijo (por defecto el primer motor con un Elo de referencia).

### Lista de rating
<!-- en: rating-list c726b55e -->

La lista, con un número mínimo de partidas. Además del Elo, el ± y las partidas, cada motor muestra su puntuación, sus
victorias (+), tablas (=) y derrotas (−), su proporción de tablas, *Rivales*: el rating medio de los rivales a los
que se enfrentó, partida a partida, *LOS*: la probabilidad de que sea más fuerte que el siguiente motor de la lista,
a partir de los dos ratings y de la covarianza de sus errores (aproximada por encima de 1000 motores, como dice su
descripción emergente), y *Diferencia*: sus puntos de Elo por encima del siguiente motor. Un clic en la cabecera de
una columna ordena por ella (# mantiene el puesto por rating); un clic derecho muestra u oculta columnas
(*Diferencia* está oculta al principio). Un clic en un motor abre su ficha.

*Copiar* como texto o BBCode, con *Con cabecera* para poner encima de la lista la cabecera descrita a continuación.
*Exportar…*: CSV con todas las columnas; una página HTML (clara u oscura) y Markdown (una tabla que GitHub y los
foros muestran como tal), ambas con una cabecera al principio que dice cómo se hizo la lista: los torneos y archivos
PGN contados, las partidas con las victorias de las blancas y de las negras, las tablas y la puntuación de las
blancas, el ancla, los filtros, el número mínimo de partidas, la fecha y la versión; y *Partidas para Ordo…*: las
partidas contadas como un PGN de cabeceras, que Ordo y BayesElo leen (`ordo -p ratings.pgn -o ratings.txt`).

### Ficha del motor
<!-- en: engine-card 80442e81 -->

La ficha de un motor: su rating, sus resultados en conjunto y por ritmo de juego, sus pérdidas por tiempo, cuelgues y
jugadas ilegales (cada partida se abre en su torneo), sus torneos (cada uno se abre), su profundidad media, su tiempo
por jugada y sus NPS leídos de los comentarios de las jugadas, y su rating tras cada torneo. La página *Motores* lleva
a ella con *Sus resultados…*.

### Cara a cara
<!-- en: head-to-head 1021ba2f -->

Dos motores enfrentados: la puntuación, el Elo y el LOS, los recuentos pentanomiales de los pares de los que jugaron
ambas partidas, por ritmo de juego y por torneo, sus partidas (cada una se abre en su torneo) y *Exportar las
partidas…*.

### Progreso
<!-- en: progress e2ea28fc -->

El rating de los motores marcados tras cada torneo que jugaron, en un eje de tiempo: las versiones de un motor una
junto a otra.

## Retransmisión
<!-- en: broadcast c2f489e7 -->

La pestaña *Retransmisión* de un torneo en curso lo publica mientras se juega; la instantánea se actualiza dos veces
por segundo. *Partida mostrada* elige la partida enviada a TLCS y a la página web: automática, o la elegida en la
pestaña *Directo*.

### Archivos en directo
<!-- en: live-files 09341896 -->

Escritos de forma atómica en una carpeta de tu elección, para publicarlos en cualquier sitio (FTP, rsync, un sitio
web): `live.pgn` (las partidas en curso), `standings.html` (clasificación y tabla cruzada), `tournament.json` (todo,
para scripts).

### Servidor TLCS
<!-- en: tlcs-server 712f28cb -->

Puerto UDP, 16001 por defecto: el protocolo de Tom's Live Chess Server, para que TLCV y otros visores TLCS puedan
seguir el torneo (tablero, relojes, PV, tabla cruzada y las partidas jugadas). Está comprobado con node-tlcv. *Un
puerto por partida* pone cada partida simultánea en su propio puerto: la partida #1 de la pestaña *Directo* en el
puerto UDP, la #2 en el siguiente, y así sucesivamente. Cada puerto se abre cuando empieza su partida, con sus propios
límites de clientes y de inicios de sesión, y recibe la tabla cruzada tras cada partida; *Partida mostrada* entonces
no se aplica.

### Página web
<!-- en: web-page 1d4c78b3 -->

Puerto TCP: una página servida por el propio fastchess-gui, para cualquier navegador de la red, de solo lectura y
actualizada cada segundo. La partida seguida (la primera en curso, o aquella en la que se hizo clic) está en el
centro: el tablero con la última jugada, un rey en jaque, las coordenadas y las flechas; el reloj de cada jugador en
letra grande; la evaluación de cada motor en letra grande con quién está pensando, su búsqueda en forma de recuadros
(profundidad/profundidad selectiva, nodos, velocidad, tiempo, accesos a tablas de finales, hash) y su línea de juego;
y las jugadas, con las jugadas de libro en cursiva. Recorre las jugadas con las flechas bajo el tablero o con las
teclas ← → (▶ tras la última jugada vuelve a la partida tal como se juega), gira el tablero con ⇅ o F, y copia el PGN
de la partida o la FEN de la posición.

Junto a la partida, unas pestañas muestran la clasificación y la tabla cruzada, los gráficos de la partida seguida, la
búsqueda de cada motor de media por jugada con sus anomalías, las siguientes partidas del calendario (un suizo o una
eliminatoria empareja sus rondas posteriores sobre la marcha), las estadísticas y anomalías de las partidas jugadas, y
el evento. Todas las partidas en curso se muestran debajo, con una pequeña barra de evaluación junto a cada jugador.
*Moves* alterna las jugadas entre letras (Nf3) y figuras (♘f3), y el tema sigue al sistema o se fija en claro u
oscuro; el navegador recuerda estas elecciones. La página no carga nada de otros sitios y funciona en teléfonos.
*Download the games (PGN)* da el archivo PGN del torneo, hasta la última partida registrada. `state.json`, que lee la
página, lo contiene todo para scripts. Los logos de los motores (consulta [Apariencia](#apariencia)) aparecen junto a
sus nombres, como pequeñas imágenes PNG que el programa genera a partir de ellos y sirve él mismo; la página solo
obtiene los de los motores del torneo.

### lichess
<!-- en: lichess 7597e86f -->

Una retransmisión de lichess, con el tablero, la barra de evaluación y el chat de lichess, para cualquiera en
internet. Necesita un token personal de API con el permiso `study:write` (*Crear un token en lichess…*), guardado
solo en `settings.json` en el perfil, y que se guarda al salir de su campo. Si no se indica ninguna ronda, se crea una
retransmisión con el nombre del torneo, no listada salvo que se marque *Listada entre las retransmisiones de lichess*;
su enlace se muestra una vez que está en directo. Si no, las partidas van a la ronda cuya URL o id se indica (una
retransmisión alimentada por envíos, no por una URL de origen). Las partidas en curso se envían cada 8 segundos con
sus relojes, y cada partida terminada una vez; una ronda contiene 64 partidas, tras lo cual se crea la siguiente ronda
de la misma retransmisión. Solo se envían las partidas terminadas después de activarla; `lichess.json` en la carpeta
del torneo recuerda adónde fueron, así que un torneo reanudado continúa la misma retransmisión. lichess solo conserva
los relojes de lo que se le envía (descarta los `[%eval]` y el texto de los comentarios de las jugadas): su barra de
evaluación es la suya propia, y las evaluaciones de los motores no aparecen allí.

La conexión usa Mbed TLS, integrado en el programa, y solo confía en los certificados raíz que firman lichess.org
(Google Trust Services) y en los de Let's Encrypt. Pasa por el proxy HTTP que indica `HTTPS_PROXY` (o `ALL_PROXY`),
como `http://proxy.example:3128` o `http://user:password@proxy.example:3128`, salvo para los hosts de `NO_PROXY`;
también en Windows el proxy viene de estas variables, no de la configuración del sistema. Los proxies SOCKS no son
compatibles.

### Un encuentro por equipos en directo
<!-- en: a-team-match-on-the-air fd32027f -->

Un encuentro por equipos muestra el marcador de los equipos en todas partes: en la cabecera de la página web y encima
de su clasificación (con la puntuación % del equipo A, Elo ± y LOS), en `standings.html` (los equipos, y luego los
motores de cada equipo frente a los del otro), en `tournament.json` y `state.json` (`teams`, `team_elo`,
`team_elo_error`, `team_los`), y en la tabla cruzada de TLCS, como una línea bajo la tabla («Lynx 17½ - 18½ Otter ·
48.6 % · Elo -9.7 ± 31.2»). Sus partidas llevan las etiquetas `WhiteTeam` y `BlackTeam` en los archivos PGN, que las
exportaciones conservan.

### Publicar la página web en internet
<!-- en: putting-the-web-page-on-the-internet b19f02f8 -->

La página web puede seguirse desde cualquier sitio sin abrir un puerto en el router: marca *Publicar en internet* bajo
*Página web* en la pestaña *Retransmisión*. Mientras se juega un torneo, fastchess-gui abre un túnel SSH (libssh2,
integrado en el programa) hacia un servicio de túneles, que da a la página una dirección https pública; no hace falta
instalar nada.

- *localhost.run* (la opción por defecto): gratuito, sin cuenta. Sus direcciones cambian de vez en cuando, y su
  velocidad está limitada; la página y su estado se envían comprimidos, así que esto no importa mucho. fastchess-gui
  conoce su clave de host: se rechaza una conexión a cualquier otra cosa.
- *Otro servidor SSH*: su host, puerto y usuario, y el puerto que abre para la página (como lo pediría
  `ssh -R REMOTE_PORT:localhost:PORT user@host`; 0 le deja elegir). Solo se pueden usar servidores que dejen entrar al
  usuario sin clave ni contraseña (autenticación «none»), y deben ofrecer curve25519-sha256, una clave de host RSA de
  2048 a 4096 bits, y AES (GCM o CTR). La clave del servidor se acepta la primera vez y se guarda en el perfil
  (`known_hosts.json`); si cambia, el túnel se detiene y lo indica, y *Confiar en la nueva clave* es para cuando el
  propietario del servidor confirma que cambió.

La pestaña muestra el estado (conectando, en directo con el número de conexiones, reconectando, o un error) y, una vez
conocida, la dirección pública con *Copiar*, *Abrir* (para ver la página como la ven los espectadores) y un código QR
para un teléfono (haz clic en él para ampliarlo). El túnel se reconecta solo tras un fallo, esperando un poco más cada
vez, y muestra la nueva dirección si cambió. Se detiene con la página web o con el torneo.

Otros túneles también funcionan, iniciados por separado: con el de Cloudflare, por ejemplo, activa la página web (aquí
en el puerto 8080) y ejecuta `cloudflared tunnel --url http://localhost:8080`; muestra una dirección como
`https://some-words.trycloudflare.com`. ngrok, tailscale funnel o un proxy inverso como nginx, también bajo una ruta
propia (`location /live/ { proxy_pass http://localhost:8080/; }`), funcionan del mismo modo.

La página es de solo lectura: solo responde a peticiones GET de la página, de su estado y del archivo PGN, nunca de
un archivo que nombre una petición, y nada en ella cambia el torneo. Cada petición se responde por separado, 32 a la
vez como máximo, y debe llegar en un segundo y responderse en diez, así que un visitante lento o silencioso no retiene
a nadie más; detrás del túnel, la conexión de cada espectador cuenta como una de ellas: se retransmiten 40 a la vez,
otras tantas esperan su turno, y las siguientes se rechazan; a un espectador que envía más de lo que acepta la página
se le desconecta solo a él. Se ejecutan dos descargas del archivo PGN a la vez, y a la siguiente se le pide que vuelva
un momento después. El estado nunca se almacena en caché, tampoco en el túnel, así que los espectadores ven las
partidas a medida que avanzan; la página la guarda el navegador y se vuelve a comprobar en cada carga. Los
espectadores mostrados en la pestaña *Retransmisión* se cuentan a partir de las peticiones que hacen las páginas, así
que la cifra también es correcta detrás de un túnel.

Los espectadores ven el nombre del torneo, los nombres de los motores, sus ratings, búsquedas y anomalías, las
aperturas y el archivo PGN de las partidas jugadas. Las carpetas y los archivos de los motores siguen siendo
privados. Sin interfaz gráfica:

```
fastchess-gui --run FOLDER --tunnel                      # localhost.run, the address printed on the console
fastchess-gui --run FOLDER --web 8080 --tunnel ssh://user@host[:port][/remote_port]
```

### Dónde hablan los espectadores
<!-- en: where-the-viewers-talk eb3ef90c -->

La página no tiene chat propio; señala dónde tiene lugar la conversación. Un *Enlace del chat* bajo *Página web* (una
invitación de Discord, un canal de Twitch, un hilo de foro; solo https, cualquier otra cosa se rechaza con un mensaje
bajo el campo) pone en la cabecera de la página un botón *Chat* que lo abre en una pestaña nueva. Mientras las
partidas se envían a lichess, la página también enlaza a la ronda de lichess, que tiene su propio chat.

Un *ID del servidor de Discord* añade una pestaña *Discord* junto a *Event*, con el propio widget de servidor de
Discord en el tema claro u oscuro de la página: el nombre del servidor, los miembros conectados, sus canales de voz y
un botón *Join*, no los mensajes. El propietario del servidor lo activa en Server Settings, Widget («Enable Server
Widget»), que también muestra el ID del servidor para copiarlo (de 17 a 20 dígitos); con el Developer Mode activado,
un clic derecho en el icono del servidor también da *Copy Server ID*. El widget se carga desde discord.com solo cuando
un espectador abre la pestaña, y Discord ve entonces la visita de ese espectador (su dirección, su navegador, su
sesión de Discord si la tiene); sin ID, la página no contacta con nadie más que con fastchess-gui. Los enlaces del
chat y de lichess no envían nada hasta que un espectador hace clic en ellos, y entonces sin referrer. Un archivo de
configuración ejecutado sin interfaz gráfica (`--run SETUP.json`) toma ambos de `web_chat_link` y `web_discord_id`.

## Modo sin interfaz gráfica
<!-- en: headless-mode 3471fbb3 -->

```
fastchess-gui --run TOURNAMENT_FOLDER     # resume a tournament
fastchess-gui --run SETUP.json            # create one from a setup or template file, with the profile's engines
```

No se abre ninguna ventana; la versión y luego el progreso se muestran en la consola, con los intentos de un motor que
no arranca, el motor apartado (con su registro del motor) o recuperado, como en la ventana, y la clasificación cada 20
partidas y al final (un encuentro por equipos: los marcadores de los equipos, y luego los motores de cada equipo frente
a los del otro), y el final de un torneo con la línea de las copias de la clasificación (partidas, tablas, ritmo de
juego, libro y evento). **Ctrl+C** se detiene tras las partidas en curso y el torneo puede reanudarse más tarde,
desde la línea de comandos o desde la interfaz gráfica. Códigos de salida: 0 terminado, 1 error durante el juego, 2
nada que ejecutar, 3 detenido antes del final (con Ctrl+C, o porque no queda nada por jugar sin un motor que no
arranca). El presupuesto de memoria y las GPU de los **Ajustes** del perfil también se le aplican.

`--detach` lo juega en cambio en segundo plano: el comando vuelve de inmediato, y el torneo continúa sin terminal; la
ventana lo muestra (se conecta a él) cuando se abre con el mismo perfil, y `--attach` se comunica con él:

```
fastchess-gui --run FOLDER --detach         # in the background, on a local socket
fastchess-gui --attach FOLDER               # its control channel on the standard input and output
```

La ventana maneja cada torneo de la misma manera: `--run FOLDER --control socket` (o `--control stdio`) lo juega sin
mostrar nada, intercambiando líneas JSON con la ventana: los eventos del torneo (su estado, cada línea del diario, las
partidas en curso jugada a jugada, cada partida registrada, su registro, su final) y los comandos de la ventana
(pausa, reanudar, detener, cambios, el final de una partida, su parte de la máquina con
`--allowance THREADS:MEMORY_MB:GPUS:ONE`). La primera línea en cada sentido es un saludo,
`{"hello": {"protocol": 1, ...}}`; un script a través de `--attach` recibe el saludo y los eventos del torneo, y puede
enviar comandos como `{"c": "pause"}`. Una sola ventana (o un solo script) a la vez se comunica con un torneo;
`--attach` lo suelta cuando su entrada estándar termina o su salida deja de leerse, así que un script debe mantener su
entrada abierta mientras lee las respuestas.

El torneo puede retransmitirse al mismo tiempo, como desde la pestaña *Retransmisión*:

```
fastchess-gui --run FOLDER --tlcs 16001             # a TLCS server on UDP port 16001
fastchess-gui --run FOLDER --tlcs 16001 --tlcs-lanes  # one port per game: 16001, 16002…
fastchess-gui --run FOLDER --web 8080               # the web page on http://<this machine>:8080/
fastchess-gui --run FOLDER --tunnel                # the web page on the internet (serves it too, --web's port or 8080)
fastchess-gui --run FOLDER --live-files [DIR]       # live.pgn, standings.html, tournament.json ("live" by default)
fastchess-gui --run FOLDER --lichess new            # a new lichess broadcast; or --lichess ROUND_URL|ROUND_ID
```

`--lichess` toma el token de la variable de entorno `FCG_LICHESS_TOKEN`, o el del perfil.

Las opciones se pueden combinar. Un archivo de configuración guardado con la retransmisión activada retransmite del
mismo modo sin ellas. Para espectadores fuera de la red local, abre los puertos en el router: TLCV y node-tlcv se
conectan al puerto TLCS. Para la página web, un túnel evita abrir un puerto (consulta [Publicar la página web en
internet](#publicar-la-página-web-en-internet)).

## Máquinas
<!-- en: machines 4d8fbfef -->

Otros ordenadores en los que jugar torneos: una máquina de pruebas, un servidor, un segundo PC. **Ajustes** >
*Máquinas* los lista. A una máquina se accede por SSH: no hay que abrir nada en ella salvo su servidor SSH, y el
programa no guarda nada secreto.

### Añadir una máquina
<!-- en: adding-a-machine f1f15823 -->

*Añadir una máquina*, y luego, en su ficha:

- *Nombre*, *Host* (su nombre en la red, o su dirección), *Puerto* (22) y *Usuario*, la cuenta con la que se ejecutan
  sus torneos;
- *Iniciar sesión con*: el *Agente SSH* (ssh-agent en Linux y macOS, Pageant o el servicio *OpenSSH Authentication
  Agent* en Windows; añádele tu clave con `ssh-add`), o un *Archivo de clave*, RSA o Ed25519 (`~/.ssh/id_ed25519`).
  La frase de contraseña de un archivo de clave se pide una vez por sesión: se queda en memoria y nunca se escribe en
  ningún sitio. La máquina debe conocer la clave: su mitad pública (`id_ed25519.pub`) va en `~/.ssh/authorized_keys`
  en la máquina (`ssh-copy-id user@host` la pone ahí), o, para un administrador de una máquina Windows, en
  `C:\ProgramData\ssh\administrators_authorized_keys`;
- *Carpeta allí*: la carpeta de fastchess-gui en la máquina, para el programa, sus torneos, libros y tablas de
  finales, relativa a la carpeta personal del usuario (`fastchess-gui` por defecto) o absoluta (`/srv/fcg`,
  `D:\fcg`);
- *Presupuesto*: los núcleos y la memoria que pueden usar a la vez los torneos de la máquina; 0 toma lo que encontró
  la prueba, todos los hilos menos uno y el 80 % de la memoria.

*Probar la conexión* inicia sesión y luego indica el sistema de la máquina, sus hilos, su memoria, el espacio libre
donde está su carpeta, y si fastchess-gui está instalado allí, y en qué versión. La primera conexión muestra la clave
de host de la máquina (su huella SHA256) y confía en ella a partir de entonces (`known_hosts.json` en el perfil, el
mismo que el del túnel de la retransmisión). Cuando la clave cambia más tarde, no se envía nada y la ficha lo indica,
con *Confiar en la nueva clave* para una máquina que se reinstaló. Cada fallo nombra la máquina y qué hacer: el host
al que no se puede llegar, una clave que la máquina rechaza (y adónde va su mitad pública), una frase de contraseña
incorrecta, ningún agente en ejecución.

*Instalar fastchess-gui* (*Actualizar fastchess-gui* una vez que está allí) copia el programa a la carpeta de la
máquina por SFTP, y luego comprueba que arranca allí (`fastchess-gui --version`). Copia este programa cuando la
máquina usa el mismo sistema (de Linux x86-64 a Linux x86-64); para otro, una máquina Windows desde Linux por
ejemplo, descarga la versión publicada para ese sistema y elige su programa en la ficha (*fastchess-gui para Windows
x86_64*). Una copia cortada por la red se reanuda donde se detuvo.

### Motores en una máquina
<!-- en: engines-on-a-machine 4b00e462 -->

*Motores en esta máquina*: los motores de la biblioteca que tiene la máquina, cada uno con su ruta allí
(`/opt/engines/stockfish`, `C:\Engines\sf.exe`). *Comprobar los motores* comprueba cada ruta: el programa está ahí,
se puede ejecutar y responde a `uci` (`xboard` para un motor WinBoard) en 10 segundos. La marca a su lado, y su
descripción emergente, dicen qué se encontró: el nombre del motor, o por qué no funciona (ausente, no ejecutable,
hecho para otro sistema, mudo).

Un motor que falta en la máquina se copia allí con *Copiar…* en su fila (o *Copiar \<motor\> a \<máquina\>* en la
paleta de comandos, o con el botón de un inicio rechazado porque un motor no tiene ruta allí). El panel que se abre
dice primero qué se envía: el programa, su tamaño y el sistema para el que está compilado, leído de sus primeros bytes
(un programa de Windows, uno de Linux o de macOS, universal o no, o un script). Un programa que la máquina no puede
ejecutar se rechaza antes de enviar nada, indicando qué compilación elegir: «Alpha es un programa de Windows; tc-box
usa Linux: elige la compilación Linux de Alpha». La arquitectura también cuenta (x86-64 o arm64); una compilación
x86-64 en un Mac arm64 solo funciona con Rosetta, y un formato que el programa no conoce se copia de todos modos: ambas
cosas se indican, no se rechazan. Un script solo va a una máquina Linux o macOS, y solo cuando el intérprete de su
primera línea (`python3`, `/bin/sh`) está allí. *Elegir la compilación \<sistema\> de \<motor\>…* toma otro archivo
de este ordenador, la compilación de Windows del motor por ejemplo, lo comprueba del mismo modo, y lo conserva para esa
máquina: las comparaciones y sustituciones posteriores lo usan, no el programa de la biblioteca.

*Con su carpeta* copia la carpeta entera del motor junto al programa (su carpeta de trabajo, o la carpeta del
programa), para un motor que necesita archivos a su lado: sus redes, un libro. El panel cuenta primero sus archivos y
su tamaño, y nombra las opciones del motor que apuntan dentro de ella (`EvalFile` de una red): en un torneo en la
máquina apuntan a la copia de allí, y la carpeta es la carpeta de trabajo del motor. Una carpeta de más de 2000
archivos no se ofrece (un programa en `/usr/bin` no debe copiarse con su carpeta), una carpeta `.git` se deja fuera,
y para una máquina Windows un nombre de archivo que Windows no admite (`:`, `*`, `?`, un punto final, `CON`,
`NUL`...) se nombra y detiene la copia antes de que empiece. Los archivos se copian tal cual, incluidos los finales de
línea.

*Copiar* envía los archivos por SFTP a `engines/<engine>/` en la carpeta de la máquina, en segundo plano con el
tiempo restante y *Cancelar*, y se reanuda donde se detuvo tras un corte; el SHA-256 de cada archivo se compara con el
de este ordenador tras la copia, y los archivos que ya están allí y son idénticos no se vuelven a enviar. En Windows
el programa conserva o recibe su `.exe`; en los demás sistemas se hace ejecutable (`chmod 755`). Luego la ruta del
motor en la máquina se fija en la copia y se comprueba como lo hace *Comprobar los motores*. En un Mac, un programa
copiado desde otro ordenador que no arranca puede necesitar una firma o salir de la cuarentena: el mensaje da los dos
comandos que hay que ejecutar allí (`codesign -s -` y `xattr -d com.apple.quarantine`); el programa no los ejecuta
por sí mismo.

La columna *Compilación* compara el motor de allí con el de este ordenador, por su SHA-256 (*Comprobar los motores* y
*Comparar* lo hacen, y el hash del programa de este ordenador se calcula una sola vez, hasta que cambie): *misma
compilación que este ordenador*, u *otra compilación allí*, con *Sustituir por el de este ordenador*. Sustituir copia
el programa de este ordenador a la carpeta del motor en la carpeta de la máquina y hace apuntar el motor allí: un
programa situado en otro lugar de la máquina (`/opt/engines/stockfish`) nunca se sobrescribe, se queda como estaba.

### Libros y tablas de finales en una máquina
<!-- en: books-and-tablebases-on-a-machine 8542f2e7 -->

*Comprobar lo que usa la configuración* compara el libro de aperturas y los libros propios de los participantes de la
configuración con la carpeta de libros de la máquina, por tamaño y SHA-256 (un libro con el mismo nombre pero otro
contenido no es el mismo libro), y los archivos Syzygy de la configuración de 6 piezas como máximo con la carpeta de
tablas de finales de la máquina, por nombre y tamaño. *Enviar los archivos que faltan* copia lo que falta o es
distinto, en segundo plano, con el tiempo restante; cada libro se comprueba por su hash una vez copiado, cada tabla
por su tamaño, y una copia interrumpida se reanuda. Las tablas de 7 piezas no se ofrecen: ocupan terabytes. Un torneo
no empieza en una máquina sin su libro; las tablas de finales que faltan son un aviso (la adjudicación y los motores
se las arreglan sin ellas).

### Un torneo en una máquina
<!-- en: a-tournament-on-a-machine 8f918d9a -->

En la configuración, *Jugar en* (en *Concurrencia*) elige dónde se juega el torneo: *Este ordenador*, o una máquina.
*Iniciar* lo prepara entonces allí: fastchess-gui debe estar instalado allí en esta versión (si no, *Actualizar
fastchess-gui*), cada motor debe tener su ruta en la máquina (si no, el inicio ofrece *Copiar \<motor\> a
\<máquina\>…*), y los libros de la configuración deben estar en su carpeta de libros (si no, el inicio dice cuál
falta, y *Enviar los archivos que faltan* lo pone allí); las tablas de finales que faltan solo son un aviso. El torneo
recibe su carpeta en `tournaments/` de la carpeta de la máquina, con los motores, libros y tablas de finales en sus
rutas de allí, y su proceso se inicia allí fuera de la sesión SSH (`fastchess-gui --run <folder> --detach`): se juega
pase lo que pase con la ventana o la red. La ventana lo sigue como un torneo de este ordenador: Directo, Partidas, la
clasificación, la pausa, la detención y los cambios, a través del canal de control de su proceso
(`fastchess-gui --attach <folder>` por SSH).

Su tarjeta en la página de inicio dice *En \<máquina\>*, y *Conectando con \<máquina\>…* mientras la conexión está
cortada: la ventana se vuelve a conectar sola, un poco más tarde tras cada fallo, y retoma los eventos donde se detuvo
su copia. El diario y los archivos PGN se copian de vuelta a la carpeta del torneo de aquí a medida que crecen, cada
pocos segundos, para que Clasificaciones, las exportaciones y la lista de comprobación del envío los lean; la carpeta
de la máquina sigue siendo la referencia (`machine.json`, aquí, dice qué máquina y qué carpeta). Cerrar la ventana lo
deja jugándose allí; el siguiente inicio lo vuelve a mostrar. *Reanudar* un torneo interrumpido de una máquina lo
reanuda allí. El presupuesto de la máquina (sus núcleos y su memoria) lo comparten sus propios torneos, aparte de los
de este ordenador. Un torneo se juega en una sola máquina: sus partidas no se reparten entre varias.

Una máquina necesita un servidor OpenSSH (Linux, macOS, BSD; Windows 10 1809 y posteriores), con una clave de host RSA
y SFTP, ambos presentes por defecto. Los comandos pasan por `sh` sea cual sea el shell del usuario (fish o csh no son
un problema), y por cmd en Windows, el shell por defecto de su servidor OpenSSH.

### Motores jugados desde este ordenador
<!-- en: engines-played-from-this-computer 8c14dc16 -->

Un motor de la biblioteca puede ejecutarse en una máquina mientras el torneo se juega aquí: un motor de GPU en la
máquina con GPU, un motor compilado para otro sistema, una máquina reservada para un motor. En su ficha, *Se ejecuta en*
elige *Este ordenador* o una máquina; *Programa allí* y *Carpeta de trabajo allí* son entonces rutas en la máquina
(vacía: la carpeta del programa allí), y los botones *…*, incluidos los de las opciones que nombran un archivo, exploran
los archivos de la máquina. En cuanto hay máquinas, *Añadir…* ofrece *Un programa en \<máquina\>…*, *Importar…* ofrece
*Todos los motores de una carpeta en \<máquina\>…*, y el *Añadir un motor…* de la máquina ofrece *Un programa de allí…*.
El motor se lee, se comprueba y se juega en la consola, la prueba rápida y los torneos como un motor de este ordenador.
Su ruta es también la de la lista de motores de la máquina, y el SHA-256 de su programa allí indica cuándo se recompiló
(Δ, se vuelve a leer).

La máquina debe haberse probado una vez (*Probar la conexión*), para que se conozca su clave de host: un motor nunca se
conecta a una máquina cuya clave no se haya mostrado y aceptado. La frase de contraseña de la clave, si la tiene, se
pide una vez por sesión, antes de la primera lectura o del inicio, y se queda en memoria: el proceso del torneo la
recibe de la ventana y la conserva, para que el torneo siga jugándose después de cerrar la ventana. `--run` en un
terminal no la pide: usa allí el agente SSH.

Cada motor se inicia a través de un intermediario, `fastchess-gui --remote-engine <key>`, que atiende el proceso que lo
hace jugar: una conexión SSH a la máquina para diez motores como máximo (el `MaxSessions` de un servidor OpenSSH), y
cada motor se inicia desde su carpeta allí. Un motor cuyo intermediario desaparece también se detiene allí, por su
identificador de proceso, cuando no se detiene por sí solo (en una máquina Linux o macOS; en una de Windows, se cierra
su canal).

*Comprobar* mide el tiempo de ida y vuelta hasta la máquina, la mediana de unos cuantos `isready`. El reloj lo
descuenta: el resumen de la configuración avisa cuando supera el 5 % del incremento, o del tiempo que tiene una jugada
si no hay incremento, con *Fijar un margen de … ms* (el doble del tiempo de ida y vuelta). Una máquina nunca probada
impide el inicio, igual que un torneo que se juega en una máquina distinta de la del motor (uno que se juega en la
máquina del motor lo juega allí como un motor propio).

Mientras se juega el torneo, la página de inicio tiene una línea *Máquinas* bajo los núcleos: cada máquina, conectada y
lo que juega allí, o desde cuándo no responde. Una máquina que no responde es una sola entrada bajo *Requiere atención*:
las partidas de sus motores se apartan y se vuelven a intentar, las demás siguen, con *Reintentar* y *Probar
\<máquina\>*. Una partida que un motor perdió porque se cortó su conexión (su final registrado como desconexión) se
anula y se vuelve a jugar, anotada en el diario con su motivo (`connection to its machine lost: <machine>`);
*Fiabilidad* la cuenta aparte, no como un cuelgue del motor. Una máquina cortada más de tres veces en una hora queda con
sus motores apartados. Los núcleos, la memoria y las GPU de este ordenador no cuentan los motores en máquinas, solo sus
intermediarios; el presupuesto de la máquina, que comparten sus propios torneos, aún no los cuenta.

### Una máquina Windows
<!-- en: a-windows-machine 58ae029a -->

Windows 10 (1809 y posteriores) y Windows 11 incluyen un servidor OpenSSH, que no se instala por defecto. En
PowerShell, como administrador:

```powershell
Add-WindowsCapability -Online -Name OpenSSH.Server~~~~0.0.1.0
Start-Service sshd
Set-Service sshd -StartupType Automatic
```

La instalación abre el puerto 22 en el cortafuegos. Luego pon tu clave pública en
`C:\Users\<user>\.ssh\authorized_keys`, o, para una cuenta del grupo Administrators, en
`C:\ProgramData\ssh\administrators_authorized_keys`, que solo pueden leer Administrators y SYSTEM (los grupos se
indican por sus SID, que son los mismos en todos los idiomas de Windows):

```powershell
icacls C:\ProgramData\ssh\administrators_authorized_keys /inheritance:r /grant "*S-1-5-32-544:F" /grant "*S-1-5-18:F"
```

Mantén cmd como shell del servidor: fastchess-gui habla cmd, no PowerShell (si se fijó un valor `DefaultShell` bajo
`HKEY_LOCAL_MACHINE\SOFTWARE\OpenSSH`, elimínalo). En la ficha de la máquina, elige el `fastchess-gui.exe` de la
versión para Windows, y luego *Instalar fastchess-gui*.

## Archivos
<!-- en: files 837f3661 -->

Todo se guarda en la carpeta del perfil: `~/.local/share/fastchess/fastchess-gui/` en Linux,
`%APPDATA%\fastchess\fastchess-gui\` en Windows, `~/Library/Application Support/fastchess/fastchess-gui/` en macOS.
Los archivos que crea el programa no pueden modificarlos otros usuarios, y `settings.json` solo lo puede leer su
propietario en cuanto contiene un token de lichess. **Ajustes** muestra la carpeta del perfil, con *Abrir la
carpeta*.

### Modo portátil
<!-- en: portable-mode 29ddbe3b -->

Con un archivo llamado `portable` (vacío, sin extensión) junto al programa (junto a `fastchess-gui.exe` o
`fastchess-gui`, junto a `fastchess-gui.app` en macOS), el perfil pasa a ser la carpeta `data` junto al programa, y la
carpeta de torneos por defecto es `data/runs`: no se escribe nada en ningún otro sitio, salvo en las carpetas
elegidas para torneos, exportaciones y similares. Las rutas de lo que está en la carpeta del programa (motores,
libros, tablas de finales, suites de tests, los archivos PGN de las clasificaciones y de la configuración, las
carpetas elegidas) se guardan relativas a ella en los archivos del perfil, así que la carpeta entera puede moverse,
copiarse a otra máquina o ejecutarse desde una memoria USB, sea cual sea su letra de unidad, y sus motores se vuelven
a encontrar. **Ajustes** indica *Modo portátil* junto a la carpeta del perfil. Para llevarte un perfil existente,
copia sus archivos en `data`. `--profile` sigue eligiendo otra carpeta, fuera del modo portátil. Un torneo conserva
las rutas de los motores de cuando se creó: reanuda uno sin terminar antes de mover la carpeta. En macOS, quita
primero la cuarentena (consulta el README): si no, macOS ejecuta la aplicación desde un lugar temporal, donde no está
el archivo `portable`.

### La carpeta del perfil
<!-- en: the-profile-folder 91838f38 -->

- `engines.json`: la biblioteca de motores, y `engines.json.bak` su versión anterior, que se lee si se pierde el
  primero;
- `setup.json`: la última configuración; `profiles.json`: tus perfiles de las secciones de la configuración;
  `templates/*.json`: las plantillas guardadas; `queue.json`: la cola;
- `book-cache/`: los libros PGN convertidos en libros Polyglot para los libros propios de los participantes (se
  eliminan tras 30 días sin uso);
- `layout.ini`: la disposición de la ventana y los paneles del espacio de trabajo; `layouts.json`: las disposiciones
  guardadas con nombre; `fastchess-gui.log`: el registro de la última sesión;
- `suite.json`: la última configuración de la página Suites; `suites/<suite>-<date>.json`: las ejecuciones de suites
  de tests terminadas;
- `board.json`: los ajustes del análisis y de las partidas de la página Tablero; `analyses/*.json`: las partidas
  analizadas jugada a jugada, un archivo por partida; `games/*.pgn`: las partidas jugadas desde la página Tablero;
- `settings.json`: la carpeta de torneos, cuando se eligió otra en **Ajustes**, *Renderizado por software*, la
  carpeta de *Todos los motores de una carpeta…*, el tester y el lugar de *Exportar para una lista de rating…*, el
  token de lichess, el tamaño y la posición de la ventana, la anchura del panel de la guía, la tarjeta de primeros
  pasos oculta, la comprobación de nuevas versiones (deseada o no, cuándo se hizo por última vez, la versión que
  encontró, el aviso oculto), la apariencia (colores del tablero, piezas, sonidos de las jugadas), y el presupuesto de
  la máquina (núcleos, memoria y GPU para los torneos, *Empezar cuando haya núcleos libres*);
- `logos/`: los logos de los motores, copias de las imágenes elegidas en sus fichas;
- `machines.json`: las máquinas remotas de **Ajustes** (su dirección, carpeta, presupuestos, rutas de los motores y lo
  que encontró la última prueba; ninguna frase de contraseña), y `known_hosts.json` las claves de host de confianza,
  de las máquinas y del túnel;
- `running.json`: los torneos que se están jugando ahora, con sus pesos, cada uno eliminado cuando se detiene; si se
  encuentra al iniciar, indica qué volver a mostrar (un torneo que siguió jugándose sin la ventana) o qué reanudar;
- `runs/`: los torneos, salvo que se eligiera otra carpeta (consulta [La carpeta de un
  torneo](#la-carpeta-de-un-torneo)).

### La carpeta de un torneo
<!-- en: a-tournaments-folder 76959723 -->

`runs/<date>_<A>_vs_<B>/` para un match, `runs/<date>_<name>/` para un torneo (en `runs/` o en la carpeta elegida),
una carpeta para cada uno, que se puede mover, copiar, comprimir en zip y enviar:

- `tournament.json`: el formato, los participantes (motores copiados con sus opciones) y las condiciones;
- `setup.json`: la configuración de la página *Nuevo torneo* con la que empezó, que leen *Clonar* y *Relanzar con…*
  (ver [Cola y relanzamiento](#cola-y-relanzamiento)); un torneo iniciado por una versión anterior no lo tiene;
- `journal.jsonl`: todo lo que ha ocurrido desde entonces, una línea por evento (pares de partidas empezados, partidas
  terminadas, cambios, emparejamientos de suizo y de eliminatoria); junto con los archivos PGN, es lo que lee una
  reanudación. Una partida está en el disco antes que su línea, así que un corte de luz pierde como mucho las
  partidas en juego. El texto de una partida al que no apunta ninguna línea (una partida descartada por un cambio
  mientras se guardaba, o dejada por un fallo) se sustituye por líneas en blanco, para que otros visores de PGN solo
  muestren las partidas del torneo;
- `tournament.lock`: lo mantiene el programa que juega el torneo; otro programa que abra la carpeta solo lo muestra;
- `<name>.pgn`: comentarios de jugada al estilo cutechess / fastchess, `{+0.35/18 1.234s}`, con la puntuación desde
  el punto de vista del bando que movió; las partidas en el orden en que terminaron (el diario encuentra cada una por
  su posición en el archivo; *Exportar todo* en la pestaña *Partidas*, y el archivo de *Añadir también cada partida
  a*, siguen el calendario);
- `<name>-chessbase.pgn`: `{[%eval 0.35,18] [%clk 0:00:59.200] [%emt 0:00:01.234]}`, con la evaluación desde el punto
  de vista de las blancas, tal como la leen ChessBase / Fritz y Lichess;
- `<name>` es el nombre del torneo cuando se creó (renombrarlo después conserva los archivos), con los caracteres que
  Windows no admite sustituidos por `_` y recortado a 64 bytes; `tournament.json` registra ambos nombres. Un torneo
  sin nombre, uno antiguo, o un match importado de la 0.2.x tiene `games.pgn` y `games-chessbase.pgn`;
- `lichess.json`: la retransmisión y la ronda de lichess a las que fueron sus partidas (sin token);
- `processor.tsv`: el tiempo de procesador que usaron los motores de cada partida, para el *Uso del procesador* de un
  torneo reanudado;
- `fastchess-gui.log`: el registro de los procesos que lo jugaron (cada torneo se juega en un proceso propio);
  `control.json`, mientras uno lo juega: dónde está su socket local (`control.sock` en la carpeta, o en la carpeta de
  ejecución del usuario cuando esa ruta es demasiado larga; una tubería con nombre en Windows), y qué proceso escucha;
- `logs/game-N.log`: cada línea intercambiada con ambos motores durante la partida N (el número mostrado en la
  pestaña *Partidas*), con la hora en que se envió o se leyó (`W>` enviado al motor de las blancas, `W<` leído de su
  salida, `W!` leído de su salida de error: aserciones, mensajes de cuelgue), para las partidas perdidas por tiempo,
  por un cuelgue, un bloqueo o una jugada ilegal (o todas las partidas, si se pide). Se conservan las primeras 300
  líneas de cada motor y las últimas 5000 líneas de la partida, en el orden en que llegaron. Es lo que necesita el
  autor de un motor para reproducir un error. Para un motor WinBoard muestra el lado UCI; `FCG_WB2UCI_LOG` da el lado
  WinBoard;
- `logs/start-<engine>.log`: el último intento fallido de arrancar un motor que no arranca: su programa, el error, y
  las líneas intercambiadas con los motores de esa partida hasta entonces (consulta [Un motor que no
  arranca](#un-motor-que-no-arranca)).

Una carpeta recibida de otra persona se puede abrir con seguridad: el archivo de *Añadir también cada partida a* debe
ser un archivo `.pgn` (otro nombre se ignora, con un aviso), y una carpeta cuyo diario, archivos PGN o archivo de
bloqueo son enlaces simbólicos no se abre para escritura, así que no puede hacer que el programa escriba fuera de
ella. Reanudarla ejecuta los motores que nombra su `tournament.json`, como hace cualquier torneo.

Los matches jugados con la versión 0.2 aparecen en la página de inicio: *Importarlas* los convierte en torneos
terminados.

## Línea de comandos
<!-- en: command-line bba4b98e -->

```
fastchess-gui [--profile DIR] [--autostart] [--size WxH] [--software]
              [--shot FILE.png[@WINDOW[@SECONDS]]]... [--select-game N] [--focus WINDOW]
              [--quit-after SECONDS]
fastchess-gui [--profile DIR] --run TOURNAMENT_FOLDER|SETUP.json [--tlcs UDP_PORT [--tlcs-lanes]]
              [--web TCP_PORT] [--tunnel [localhost.run|ssh://USER@HOST[:PORT][/REMOTE_PORT]]] [--live-files [DIR]] [--lichess ROUND_URL|ROUND_ID|new]
              [--control stdio|socket] [--detach] [--allowance THREADS:MEMORY_MB:GPUS:ONE]
fastchess-gui --attach TOURNAMENT_FOLDER
fastchess-gui --version
```

- `--profile DIR` usa otra carpeta de perfil;
- `--autostart` inicia la configuración guardada al arrancar (en lugar de reanudar el torneo que estaba en curso,
  consulta [Pausar, detener y reanudar](#pausar-detener-y-reanudar));
- `--size WxH` abre la ventana con ese tamaño, centrada, en lugar de como se dejó, y no toca el tamaño y la posición
  guardados para el siguiente inicio;
- `--software` hace que el procesador dibuje la ventana (el renderizador por software de SDL) en lugar de OpenGL, como
  *Renderizado por software* en **Ajustes**, para este inicio (consulta [Cómo se dibuja la
  ventana](#cómo-se-dibuja-la-ventana));
- `--shot` guarda una captura de la ventana tras el número de segundos indicado (5 por defecto), con WINDOW en primer
  plano, y sale una vez tomadas todas las capturas. Se puede repetir. WINDOW es una página (`home`, `setup`,
  `engines`, `books`, `board`, `ratings`, `ratings-engine`, `ratings-h2h`, `ratings-progress`, `console`,
  `settings`) o una pestaña (`live`, `games`, `openings`, `standings`, `schedule`, `bracket`, `broadcast`). El ratón
  se ignora en este modo y, en Linux, la ventana se renderiza fuera de la pantalla (no aparece ninguna ventana; fija
  `SDL_VIDEODRIVER` para cambiarlo); la ventana se abre con el tamaño por defecto, o con `--size`, y no se tocan el
  tamaño y la posición guardados;
- `--focus WINDOW` abre esa página o pestaña al iniciar, o bien: `engines-card` (la ficha del primer motor),
  `engines-refresh` (*Actualizar: los motores nuevos de la carpeta*), `engines-ratings=FILE` (la vista previa de esa
  lista de rating), `setup-picker` y `setup-suggest` (el filtro de la biblioteca con el foco, o los rivales sugeridos
  seleccionados), `setup-open=ROW` y `setup-profiles=ROW` (una fila de la configuración abierta, o su lista de
  perfiles: `format`, `time`, `openings`, `adjudication`, `engines`, `concurrency`, `pgn`), `setup-book` (el libro
  propio del primer participante), `submission-export` (*Exportar para una lista de rating…*), `ratings-players` (la
  ventana *Jugadores PGN*), `clone=DIR` (la página *Nuevo torneo* rellenada por *Clonar* desde la carpeta de ese
  torneo), `palette[=TEXT]` (la paleta de comandos, con TEXT escrito en ella), `game-menu=N` (el
  menú de la partida N en la pestaña *Partidas*, con un resultado elegido), `end-menu=N` (*Terminar con un
  resultado* de la partida del hueco N, desde 1), `big-screen[=N]` (la pantalla grande, de la partida del hueco N),
  `live-back=N` (la partida seguida en *Directo*, retrocedida N medias jugadas), `engines-console` (la consola del
  primer motor), `board-analyse` (la página Tablero, analizada), `board=FILE` (un archivo PGN, FEN o EPD en la página
  Tablero; `board-analyse=FILE` analizado, `board-review=FILE` su primera partida analizada jugada a jugada),
  `board-edit` (el editor de posiciones), `board-play` y `board-play-start` (una partida entre los dos primeros
  motores, iniciada), `machines` (la ficha de la primera máquina remota en **Ajustes**; `machines-test` probada,
  `machines-files` los libros y tablas de finales de la configuración comparados con ella), `guide[=ID]` (el panel de
  la guía, en la sección ID), `guide-search=TEXT` (el panel de la guía, buscando TEXT);
- `--select-game N` abre la partida N (en orden de finalización) en la pestaña *Partidas* en cuanto existe;
- `--quit-after SECONDS` detiene el torneo y sale;
- `--run`, `--detach`, `--attach`, `--control`, `--allowance`: consulta [Modo sin interfaz
  gráfica](#modo-sin-interfaz-gráfica);
- `--version` muestra la versión y el protocolo del canal a través del cual la ventana sigue un torneo remoto
  (consulta [Máquinas](#máquinas)), y sale.

## Solución de problemas
<!-- en: troubleshooting e8c4882d -->

### El programa no arranca
<!-- en: the-program-does-not-start dc0a0128 -->

- **Windows dice que el programa no es seguro, o un antivirus lo elimina.** El programa no está firmado, así que
  Windows SmartScreen y algunos antivirus desconfían de él. *Más información*, *Ejecutar de todas formas* lo deja
  arrancar; a un antivirus se le puede indicar que permita su carpeta. Las versiones publicadas las compila GitHub
  Actions a partir de las fuentes públicas.
- **macOS se niega a abrirlo.** No está firmado por una cuenta de desarrollador de Apple: haz Control-clic (clic
  derecho) en él, Abrir, y luego Abrir otra vez; en macOS 15 y posteriores, intenta abrirlo una vez, y luego Ajustes
  del Sistema, Privacidad y seguridad, Abrir igualmente. O ejecuta `xattr -dr com.apple.quarantine` en la carpeta
  descomprimida.
- **Un mensaje dice que ninguna forma de dibujar la ventana funciona, o la ventana se queda en negro.** Al
  controlador gráfico le falta OpenGL 3.2 o falla: inicia con `--software` (el procesador dibuja la ventana) y, si
  funciona, marca *Renderizado por software* en **Ajustes**. En Wayland sin controlador gráfico, inicia con
  `SDL_VIDEODRIVER=x11`. Consulta [Cómo se dibuja la ventana](#cómo-se-dibuja-la-ventana).
- **El texto chino aparece como cuadrados.** El chino necesita una fuente del sistema: Microsoft YaHei en Windows,
  Noto Sans CJK o WenQuanYi en Linux (instala `fonts-noto-cjk` o similar).

### Un motor no arranca
<!-- en: an-engine-does-not-start 202a67d4 -->

- **Su comprobación falla, o su torneo dice que no puede arrancar.** Lee el error bajo el motor, y luego su registro
  del motor (*Registro del motor*, o `logs/start-<engine>.log` en la carpeta del torneo): la salida de error del
  motor suele nombrar el archivo que falta. Las causas habituales: el programa se movió (*Localizar…*), una red o una
  biblioteca no encontradas (fija la carpeta de trabajo, o `EvalFile`, en su ficha), un programa compilado para otro
  procesador (una compilación con AVX-512 en una máquina sin él), un arranque lento (aumenta *Tiempo de arranque* en
  su ficha). Consulta [Un motor que no arranca](#un-motor-que-no-arranca).
- **Un motor WinBoard no arranca desde las posiciones del libro.** Un motor de protocolo 1 sin `setboard` solo puede
  empezar desde la posición inicial: dale un libro PGN (jugadas), no un libro EPD. Consulta [Motores
  WinBoard](#motores-winboard).
- **El torneo se niega a empezar: un motor no juega Chess960.** Todos los motores deben tener la opción
  `UCI_Chess960` (UCI) o la variante `fischerandom` (WinBoard); el resumen nombra los que no la tienen.

### Partidas perdidas o que parecen erróneas
<!-- en: games-are-lost-or-look-wrong f8a7cc97 -->

- **Un motor pierde por tiempo al principio de sus partidas.** Lo más frecuente es que esté mal configurado, no que
  juegue mal: demasiados hilos para los núcleos que tienen las partidas, un hash demasiado grande para reservarlo a
  tiempo, una red cargada en cada partida, un arranque lento. Baja sus `Threads` y su `Hash`, desmarca *Reiniciar el
  motor en cada partida*, aumenta *Tiempo de arranque*; la tarjeta *Fiabilidad* y la columna *TF* de la vista general
  muestran las pérdidas por tiempo.
- **Los motores parecen más lentos de lo que deberían.** Demasiadas partidas a la vez: deja *Concurrencia* en
  automático, y mantén concurrencia × hilos dentro de los núcleos físicos. *Uso del procesador* en la vista general
  nombra a un motor que usa más núcleos que sus `Threads`, o que piensa durante el tiempo de su rival.
- **El resumen avisa de que la memoria no será suficiente.** Las tablas hash de las partidas no cabrían en la memoria
  libre: baja el `Hash` (en *Ajustes de los motores*, o el del motor que nombra) o la concurrencia.
- **Se indica que faltan archivos de tablas de finales o que están cortados.** Una descarga se detuvo a medias, o la
  carpeta solo contiene tablas más pequeñas: vuelve a descargar los archivos nombrados en la descripción emergente, o
  baja la adjudicación a las piezas encontradas (*Bajar a 5 piezas*).
- **Se indica que un libro tiene posiciones ilegales o duplicados.** fastchess las deja fuera; *Guardar una copia
  limpia…* en la página Libros escribe un libro sin ellas. Consulta [Comprobar un libro](#comprobar-un-libro).
- **Una partida se decidió mal.** Haz clic derecho en ella en *Partidas* o en *Calendario*: vuelve a jugarla,
  anúlala o fija su resultado, con el motivo. Consulta [Modificar un torneo en curso](#modificar-un-torneo-en-curso).

### Retransmitir
<!-- en: broadcasting 4a2e6916 -->

- **TLCV u otro visor no se conecta.** Inicia el torneo (y su servidor TLCS) antes que el visor: este no vuelve a
  conectarse por sí solo. Abre el puerto UDP en el router para los espectadores de fuera de la red local.
- **No se puede acceder a la página web desde fuera.** Marca *Publicar en internet* para usar un túnel (consulta
  [Publicar la página web en internet](#publicar-la-página-web-en-internet)), o abre el puerto TCP en el router.
- **lichess rechaza el token.** El token necesita el permiso `study:write`: crea uno nuevo con *Crear un token en
  lichess…*. Detrás de un proxy, fija `HTTPS_PROXY`.

### Cualquier otro problema
<!-- en: anything-else ce59c083 -->

*Informar de un problema…* en **Ajustes** o en la página **Registro** abre una incidencia en GitHub ya rellenada con
la versión, el sistema y las últimas líneas del registro (consulta [Informar de un problema](#informar-de-un-problema)).

## Glosario
<!-- en: glossary 0386d0d9 -->

- **Adjudicación**: terminar una partida antes del mate cuando su resultado está claro: tablas cuando ambas
  puntuaciones se mantienen cerca de 0, derrota cuando se mantienen muy por debajo, o lo que digan las tablas de
  finales. Consulta [Adjudicación](#adjudicación).
- **BayesElo, DrawElo**: un modelo de rating con un parámetro aparte para las tablas (DrawElo), del programa de Rémi
  Coulom.
- **Buchholz, Sonneborn-Berger**: sistemas de desempate: la suma de las puntuaciones de los rivales (un suizo), y la
  suma de las puntuaciones de los rivales vencidos más la mitad de las de aquellos con los que se hizo tablas (un
  todos contra todos).
- **Cabeza de serie**: un puesto en el orden de los participantes: los motores a prueba de un gauntlet, los favoritos
  de una eliminatoria.
- **Chess960, Chess324**: posiciones iniciales con la primera fila barajada: 960 con el rey entre las torres (con el
  enroque a sus casillas habituales), 324 con el rey y las torres en su sitio.
- **Ciclo**: en un todos contra todos, cada emparejamiento jugado una vez con cada color (cutechess lo llama ronda, de
  2 partidas por encuentro).
- **Concurrencia**: el número de partidas jugadas a la vez.
- **ECO**: los códigos de la Encyclopaedia of Chess Openings, de A00 a E99.
- **Elo, nElo**: la diferencia de rating que representa una puntuación. El nElo (Elo normalizado) divide por la
  dispersión de los resultados, para que su valor no dependa de la proporción de tablas; fishtest y OpenBench
  expresan en él los límites de sus SPRT.
- **EPD, FEN**: una línea que describe una posición; una línea EPD puede llevar operaciones, como la mejor jugada `bm`
  de una suite de tests.
- **Gauntlet**: los motores a prueba (los cabezas de serie) juegan contra todos los demás motores, que no juegan entre
  sí.
- **H0, H1**: las dos hipótesis de un SPRT: la diferencia de Elo es elo0 o menos (H0), o elo1 o más (H1).
- **Hash**: la memoria que ocupa un motor para su tabla de transposición, en MB.
- **LLR**: el logaritmo de la razón de verosimilitudes (log-likelihood ratio) de un SPRT; el test termina cuando sale
  de sus límites, que se obtienen de alpha y beta.
- **LOS**: la probabilidad de superioridad (likelihood of superiority), la probabilidad de que un motor sea más fuerte
  que otro, según sus partidas.
- **MultiPV**: un motor que busca varias líneas a la vez.
- **Pentanomial**: los resultados de los pares de partidas contados por su total: 0, ½, 1, 1½ o 2 puntos. Da barras de
  error más estrechas que contar partidas sueltas, ya que las dos partidas de un par comparten apertura.
- **PGN**: el formato de texto de las partidas de ajedrez: etiquetas como `[White "…"]`, y luego las jugadas.
- **Polyglot**: el formato binario de libros de aperturas (`.bin`) que leen la mayoría de los programas, con un peso
  para cada jugada.
- **Ponder**: pensar durante el tiempo del rival; desactivado en los torneos de fastchess-gui.
- **SPRT**: el test secuencial de razón de probabilidades (sequential probability ratio test), que juega partidas
  hasta poder decir, con las tasas de error elegidas (alpha, beta), si un motor gana elo1 o más (H1) o no más de elo0
  (H0).
- **Suite de aperturas, libro**: las posiciones o jugadas desde las que empiezan las partidas, cada una jugada dos
  veces con los colores invertidos.
- **Syzygy**: las tablas de finales que dan el resultado de cada posición con pocas piezas (archivos WDL, `.rtbw`).
- **Tablas de Berger**: el orden de las rondas de un todos contra todos en el que cada motor juega una vez por ronda.
- **TF**: time forfeits, las partidas perdidas por tiempo.
- **Threads**: los hilos del procesador con los que busca un motor.
- **TLCS, TLCV**: Tom's Live Chess Server y Viewer, un protocolo para seguir en directo partidas de motores.
- **UCI, WinBoard (CECP)**: los dos protocolos con los que los programas hablan con los motores de ajedrez.
- **WDL**: las probabilidades de victoria, tablas y derrota que da un motor junto con su puntuación.
