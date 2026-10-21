<!-- PREPARACIÓN DE SUSTENTACIÓN - Proyecto Pong
     [JUAN] Documento de preparación creado por JUAN.
     Enfoque: dibujar la arquitectura + defensa módulo por módulo,
     pensado para un profesor riguroso que evalúa comprensión real. -->

# 🎯 Preparación de Sustentación — Proyecto Pong

> **Contexto:** el profe es riguroso. Pone a **dibujar la arquitectura** y hace
> **preguntas de cada módulo**. Este documento está hecho para que puedas
> *explicar con tus palabras*, no para memorizar.

---

## 1. Regla de oro antes de empezar

El profe no quiere que recites código: quiere saber **por qué** cada cosa está
así. Si no sabes una respuesta, di: *"no lo implementamos, pero la forma correcta
sería…"*. **Nunca inventes.** Un "no sé" honesto + una idea vale más que una
mentira técnica.

---

## 2. LO PRIMERO: aprende a dibujar la arquitectura

Si te piden "dibuje la arquitectura", dibuja **esto** de memoria. Es el diagrama
más importante de todos.

### 2.1 Diagrama Cliente/Servidor (dibújalo así en el tablero)

```
   ┌─────────────┐        ┌─────────────┐
   │  Cliente 1  │        │  Cliente 2  │      (N clientes)
   │  (Jugador)  │        │  (Jugador)  │
   └──────┬──────┘        └──────┬──────┘
          │                      │
          │        TCP           │
          └──────────┬───────────┘
                     ▼
        ┌────────────────────────────┐
        │        PongServer          │
        │   ┌────────────────────┐   │
        │   │  Lobby (main loop) │   │  ← accept() + registro
        │   └─────────┬──────────┘   │
        │             │ empareja 2   │
        │   ┌─────────▼──────────┐   │
        │   │ Hilo de Partida     │  │  ← un pthread por partida
        │   │ (game loop 60fps)   │  │
        │   └─────────┬──────────┘   │
        │             │              │
        │   ┌─────────▼──────────┐   │
        │   │ Logger (archivo)   │   │
        │   └────────────────────┘   │
        └────────────────────────────┘
```

### 2.2 Si te lo piden "por capas" (más completo)

```
   CAPA DE APLICACIÓN   │  PongClient  ◄──── MyAppGameProtocol ────►  PongServer
                        │
   CAPA DE TRANSPORTE   │                    TCP (SOCK_STREAM)
                        │
   CAPA DE RED          │                          IP
                        │
   CAPA DE ENLACE       │                   Ethernet / Wi-Fi
```

### 2.3 Frase de apertura (memorízala)

> *"Es una arquitectura **cliente/servidor autoritativa**. El cliente captura el
> input y envía su **intención** de movimiento; el servidor en C, usando **Sockets
> Berkeley** sobre **TCP**, mantiene el estado del juego, lo calcula y lo difunde
> a los dos jugadores. El servidor es **concurrente**: usamos **un hilo por
> partida**, lo que permite varias parejas jugando a la vez."*

---

## 3. Defensa MÓDULO POR MÓDULO

El profe pregunta de cada módulo. Aquí tienes, por cada archivo: **qué hace**,
**cómo funciona** y **las preguntas que te hará**.

---

### 3.1 `include/protocol.h` — El protocolo

**Qué es:** la definición de *MyAppGameProtocol*. Aquí viven los *opcodes*, el
header y las estructuras de cada mensaje.

**Clave:** el header es de **3 bytes**: `type` (1 byte) + `length` (2 bytes).

```c
typedef struct __attribute__((packed)) {
    uint8_t  type;     /* opcode */
    uint16_t length;   /* tamaño del payload */
} msg_header_t;
```

**Preguntas del profe:**

| Pregunta | Tu respuesta |
|---|---|
| ¿Por qué `__attribute__((packed))`? | Para que el compilador **no inserte bytes de relleno** (*padding*). Ej.: sin `packed`, `msg_header_t` ocuparía 4 bytes en vez de 3. |
| ¿Por qué el header lleva la longitud? | Porque **TCP es un flujo de bytes sin fronteras de mensaje**. Sin la longitud no sabrías dónde termina un mensaje y empieza el otro. |
| ¿Por qué el protocolo es binario y no texto? | Es más **compacto y eficiente**; además el enunciado lo exige. |
| ¿Cómo se distingue un mensaje de otro? | Con el campo **`type`** (opcode): `0x01` registro, `0x05` estado, etc. |

> ⚠️ **Dato fino que impresiona:** el campo `length` viaja en **Network Byte Order**
> (`htons`/`ntohs`), pero los payloads van en el orden nativo de la máquina. Es un
> punto que un profe riguroso puede preguntar.

---

### 3.2 `src/protocol.c` — La implementación del protocolo

**Qué hace:** 4 funciones que garantizan enviar/recibir **exactamente** los bytes.

```c
int send_all(sockfd, buf, len);   /* envía TODO o falla   */
int recv_all(sockfd, buf, len);   /* recibe TODO o falla  */
int send_msg(sockfd, type, payload, len);      /* header + payload */
int recv_msg(sockfd, header, payload, max_len);/* header → luego payload */
```

**El concepto clave: `send()` puede enviar MENOS bytes de los pedidos.**
Por eso `send_all` **itera en un bucle** hasta que envía todo. Lo mismo `recv_all`.

**Preguntas del profe:**

| Pregunta | Tu respuesta |
|---|---|
| ¿Por qué un bucle en `send_all`? | Porque `send()`/`recv()` en TCP pueden transferir **menos bytes de los solicitados** (segmentación). Hay que repetir hasta completar `len`. |
| ¿Cómo sabe el servidor cuántos bytes leer? | Lee **primero 3 bytes** (header), obtiene `length`, y luego lee **exactamente** `length` bytes. |
| ¿Qué pasa si no usaras `recv_all`? | Podrías leer un mensaje **partido** o mezclar dos mensajes distintos. |
| ¿Qué protege el parámetro `max_payload_len`? | Evita un **desbordamiento de buffer** si un cliente malicioso manda una longitud gigante. |

---

### 3.3 `src/server/main.c` — Punto de entrada

**Qué hace:** valida argumentos, inicializa el logger y arranca el servidor.

```bash
./server <PORT> <LogFile>
```

**Preguntas del profe:**

| Pregunta | Tu respuesta |
|---|---|
| ¿Por qué valida el puerto entre 1024 y 65535? | Los puertos **<1024 son privilegiados** (requieren root). Validar evita fallos y cumple el enunciado. |
| ¿Por qué `return EXIT_FAILURE` si algo falla? | Para que el sistema operativo sepa que el programa falló (buena práctica, y `$?` lo refleja). |

---

### 3.4 `src/server/server.c` — El corazón (¡el más importante!)

Tiene **cuatro piezas clave**. Prepáralas bien.

#### (a) `init_server_socket()` — la secuencia clásica de TCP

```
socket() → setsockopt(SO_REUSEADDR) → bind() → listen()
```

| Pregunta | Tu respuesta |
|---|---|
| Enumere los pasos para un servidor TCP | `socket()`, `bind()`, `listen()`, y luego `accept()` en bucle. |
| ¿Qué es `SO_REUSEADDR`? | Permite **reusar el puerto** inmediatamente tras reiniciar el servidor (evita el error *"Address already in use"*). |
| ¿Qué es `INADDR_ANY`? | Escuchar en **todas las interfaces** de red (no solo localhost). |
| ¿Qué es el *backlog*? | Cola de conexiones pendientes de `accept()`. Aquí es 10 (`SERVER_BACKLOG`). |
| ¿`listen()` bloquea? | No: deja el socket **en modo pasivo** a la espera de conexiones. |

#### (b) `run_server_loop()` — el **Lobby**

```c
while (g_server_running) {
    client_fd = accept(...);          /* 1. acepta */
    recv_msg(...);                     /* 2. recibe registro */
    send_msg(MSG_REGISTER_RESP, ...);  /* 3. confirma */
    if (!has_waiting_player)           /* 4a. primero → espera */
        guardar en waiting_player;
    else {                             /* 4b. segundo → empareja */
        crear match_context_t;
        pthread_create(match_worker);  /* ¡HILO NUEVO! */
        pthread_detach(tid);
    }
}
```

| Pregunta | Tu respuesta |
|---|---|
| ¿Cómo soporta varios clientes? | Con **un hilo por partida**. El lobby sigue aceptando conexiones; cada pareja corre en su propio `pthread`. |
| ¿Por qué `pthread_detach`? | Para que el hilo **libere sus recursos solos** al terminar, sin que el hilo principal tenga que hacer `join`. |
| ¿Por qué `malloc` para el contexto de partida? | Porque el hilo lo usa **después** de que la función retorne; si fuera local, se destruiría. Cada hilo hace su `free`. |
| ¿Qué pasa con el jugador que espera si nadie llega? | Queda en `waiting_player`; al cerrar el servidor (Ctrl+C) se cierra su socket. |
| ¿Cómo se maneja Ctrl+C? | Con `sigaction(SIGINT, ...)`: pone `g_server_running = 0` y el bucle sale limpiamente. |

#### (c) `match_worker()` — el **game loop** (tu aporte)

Un hilo por partida que simula a ~60 fps hasta que alguien llega a 5 puntos.

**Preguntas del profe:**

| Pregunta | Tu respuesta |
|---|---|
| ¿Quién decide el estado, cliente o servidor? | El **servidor** (autoritativo). El cliente solo manda su dirección; el servidor calcula y difunde. |
| ¿Por qué el servidor y no el cliente? | Evita **trampas** y **desincronización**: ambas pantallas ven lo mismo porque el servidor es la única fuente de verdad. |
| ¿Cómo evitas que un `recv` bloquee la simulación? | Con **un hilo lector por jugador** (`input_reader_thread`) que recibe movimientos en paralelo; el game loop solo lee la variable `direction`. |
| ¿Cómo sincronizas el reloj? | Con `nanosleep` a 16 ms por tick (~60 fps). |
| ¿Qué pasa si un cliente se desconecta? | Su hilo lector pone `alive = 0` y el game loop termina; se envía `MSG_GAME_OVER` y se cierran sockets. |

#### (d) `input_reader_thread()` — el hilo lector

Recibe `MSG_PLAYER_MOVE` de **un** jugador y guarda su última dirección.

| Pregunta | Tu respuesta |
|---|---|
| ¿Por qué un hilo solo para leer? | Porque `recv()` es **bloqueante**: si el game loop leyera, se congelaría esperando al cliente. |
| ¿Qué datos comparte con el game loop? | La estructura `player_input_t` con `direction` y `alive`. |

---

### 3.5 `src/server/logger.c` + `include/logger.h` — El logger

**Requisito explícito del enunciado.** Escribe a **consola y archivo**.

```c
static pthread_mutex_t log_mutex = PTHREAD_MUTEX_INITIALIZER;
void logger_log(level, format, ...) {
    pthread_mutex_lock(&log_mutex);   /* ← ¡thread-safe! */
    ... time(), strftime, vprintf, vfprintf ...
    pthread_mutex_unlock(&log_mutex);
}
```

| Pregunta | Tu respuesta |
|---|---|
| ¿Por qué el **mutex**? | Porque varios hilos (varias partidas) escriben al log a la vez; sin mutex, las líneas se **entremezclarían**. |
| ¿Cómo logra la marca de tiempo? | `time()` + `localtime_r()` + `strftime()` → formato `YYYY-MM-DD HH:MM:SS`. |
| ¿Qué son `va_list` / `vprintf`? | Mecanismo de C para funciones con **argumentos variables** (como `printf`). `va_copy` permite usar los argumentos **dos veces** (consola y archivo). |
| ¿Por qué `fflush`? | Para **forzar la escritura a disco** de inmediato y no perder logs si el programa muere. |
| ¿Los 4 niveles? | `INFO`, `WARN`, `ERROR`, `DEBUG` (enum `log_level_t`). |

---

### 3.6 `src/client/main.c` — El cliente en C

Secuencia: `socket()` → `connect()` → enviar registro → recibir confirmación → recibir inicio de partida.

| Pregunta | Tu respuesta |
|---|---|
| ¿Pasos de un cliente TCP? | `socket()`, `connect()`, luego `send()`/`recv()`. |
| ¿Diferencia con el servidor? | El cliente **inicia** la conexión (`connect`); el servidor **espera** (`bind`+`listen`+`accept`). |

---

### 3.7 `Makefile` — La compilación

| Pregunta | Tu respuesta |
|---|---|
| ¿Qué hace `make`? | Compila `server` y `client` con `gcc -Wall -Wextra -pthread -Iinclude -g`. |
| ¿Qué es `-Wall -Wextra`? | Activan **todas las advertencias**: nuestro código compila **sin ninguna**. |
| ¿Qué es `-pthread`? | Enlaza la librería de **hilos POSIX** (necesaria para `pthread_create`). |
| ¿Qué es `-Iinclude`? | Le dice a gcc dónde buscar los `.h`. |

---

## 4. Orden de la sustentación (guion sugerido)

1. **Arquitectura** → dibújala y di la frase de apertura.
2. **Protocolo** → muestra el header de 3 bytes y un par de opcodes.
3. **Flujo completo** → registro → lobby → emparejamiento → partida → game over.
4. **Concurrencia** → un hilo por partida (el punto que más impresiona).
5. **Logger** → requisito del enunciado, y el mutex (thread-safe).
6. **Justificación TCP** → cierra fuerte con el `docs/socket-tcp-justification.txt`.

---

## 5. Preguntas "trampa" y cómo responderlas

| Pregunta difícil | Respuesta honesta y correcta |
|---|---|
| *"¿Por qué TCP si los juegos usan UDP?"* | "Porque el protocolo debe ser íntegro y ordenado, y Pong genera poquísimos datos. El *overhead* de TCP es despreciable. En un juego masivo usaríamos UDP o QUIC para el estado y TCP para el control." |
| *"¿Qué pasa si se cae TCP?"* | "Como es orientado a conexión, `recv()` devuelve 0 o error; nuestro hilo lector lo detecta (`alive=0`) y termina la partida." |
| *"¿Dónde está el cuello de botella?"* | "El lobby es secuencial, pero el `accept()` es rapidísimo. El juego corre en hilos aparte, así que escala bien." |
| *"¿Cómo garantizas que ambos clientes vean lo mismo?"* | "Porque el servidor es autoritativo: **él** calcula y emite `MSG_GAME_STATE` a los dos; ninguno decide su propio estado." |
| *"¿Qué pasa si un cliente manda basura?"* | "`recv_msg` valida el header y `max_payload_len`; si el mensaje es inválido, el servidor cierra esa conexión sin afectar a las demás." |
| *"¿Por qué no usaron `select()`/`poll()`?"* | "Optamos por hilos porque el enunciado lo permite y es más legible; `select()` sería la alternativa con un solo hilo." |

---

## 6. Datos que NO puedes fallar (repásalos el día antes)

```
Socket          : AF_INET, SOCK_STREAM (TCP)
Header          : 3 bytes  →  type(1) + length(2)
Opcodes         : 0x01 reg-req · 0x02 reg-resp · 0x03 match-start
                  0x04 player-move · 0x05 game-state · 0x06 game-over · 0x07 disconnect
Comando servidor: ./server <PORT> <LogFile>
Puerto válido   : 1024 – 65535
Gana quien      : llegue a 5 puntos
Concurrencia    : un pthread por partida
Sync del log    : pthread_mutex_t
```

---

## 7. Checklist final (repasa el día antes)

- [ ] Sé dibujar el diagrama Cliente/Servidor **sin mirar**
- [ ] Sé dibujar la jerarquía de protocolos (Figura 3)
- [ ] Explico el header de 3 bytes y **por qué** lleva longitud
- [ ] Explico `send_all`/`recv_all` (que `send` puede enviar menos)
- [ ] Explico la secuencia `socket→bind→listen→accept`
- [ ] Explico **un hilo por partida** (concurrencia)
- [ ] Explico el **game loop** y el hilo lector
- [ ] Explico el **logger** y el mutex
- [ ] Tengo lista la **justificación TCP vs UDP**
- [ ] Sé correr `make` y `./server 8080 pong.log`

---

> 💡 **Último consejo:** practica explicar en voz alta, cronometrado, como si el
> profe estuviera enfrente. Si puedes explicar **la concurrencia** y **el
> protocolo** sin mirar el código, estás listo.
