#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
=============================================================================
 PongClient  ·  "MyAppGameProtocol"  ·  Interfaz Futurista (Neon)
=============================================================================
 Proyecto N1 - Internet: Arquitectura y Protocolos
 Cliente en Python 3.x usando la API de Sockets Berkeley (socket/send/recv)
 y el protocolo binario definido en protocol.h / opcode-specification.md.

 IMPORTANTE (decisiones de protocolo, idénticas al lado C):
   * El encabezado (header) de 3 bytes viaja en Network Byte Order -> struct "!BH".
   * Los payloads viajan en LITTLE-ENDIAN ("<"), tal como los escribe el
     compilador C en x86 / ARM. Solo el campo 'length' del header usa htons().
   * No se usan librerías de sockets de alto nivel (asyncio, twisted, etc.).
     Se usa el módulo 'socket' de la stdlib, que ES el binding de Berkeley.

 Modos:
   * MODO RED   : conecta al PongServer en C, registra, espera rival y juega.
   * PRÁCTICA   : simulación local sin servidor, para probar la interfaz.

 Uso:
   python3 Interfaz.py
=============================================================================
"""

import math
import os
import sys
import queue
import random
import socket
import struct
import threading

import pygame

# =============================================================================
#  CONSTANTES DEL PROTOCOLO  (espejo exacto de include/protocol.h)
# =============================================================================
# Opcodes
MSG_REGISTER_REQ = 0x01
MSG_REGISTER_RESP = 0x02
MSG_MATCH_START = 0x03
MSG_PLAYER_MOVE = 0x04
MSG_GAME_STATE = 0x05
MSG_GAME_OVER = 0x06
MSG_DISCONNECT = 0x07

# Roles
ROLE_PLAYER_1 = 1   # Pala izquierda
ROLE_PLAYER_2 = 2   # Pala derecha

# Direcciones de movimiento
MOVE_UP = -1
MOVE_STILL = 0
MOVE_DOWN = 1

# Estados de registro
STATUS_REGISTER_OK = 0
STATUS_LOBBY_FULL = 1

# Tamaños fijos del protocolo
MAX_NICKNAME_LEN = 32
MAX_EMAIL_LEN = 32

# Formatos de payload en little-endian (empaquetados, sin padding)
FMT_REGISTER_REQ = "<32s32s"        # 64 bytes
FMT_REGISTER_RESP = "<BB"           # 2 bytes
FMT_MATCH_START = "<BB32s"          # 34 bytes
FMT_PLAYER_MOVE = "<b"              # 1 byte
FMT_GAME_STATE = "<hhhhBBIH"        # 16 bytes
FMT_GAME_OVER = "<BBB"              # 3 bytes

# Espacio lógico del campo. SERVIDOR Y CLIENTE DEBEN ACORDAR ESTAS MEDIDAS.
# El servidor envía coordenadas en este sistema; el cliente las escala a pantalla.
LOGICAL_W = 1000
LOGICAL_H = 600

# Primer mensaje que el servidor puede devolver tras el registro
KNOWN_OPCODES = {
    MSG_REGISTER_RESP,
    MSG_MATCH_START,
    MSG_PLAYER_MOVE,
    MSG_GAME_STATE,
    MSG_GAME_OVER,
    MSG_DISCONNECT,
}


# =============================================================================
#  COLORES NEON / FUTURISTAS
# =============================================================================
BG = (6, 8, 18)                  # Fondo casi negro azulado
GRID = (18, 28, 54)              # Líneas de la retícula
CYAN = (0, 255, 255)             # Neón principal
MAGENTA = (255, 0, 200)          # Neón acento
YELLOW = (255, 240, 0)
WHITE = (235, 245, 255)
DIM = (90, 120, 170)
RED = (255, 60, 90)
GREEN = (0, 255, 150)


def clamp(v, lo, hi):
    return lo if v < lo else (hi if v > hi else v)


# =============================================================================
#  NetClient  ·  Sockets Berkeley + Protocolo binario
# =============================================================================
class NetClient:
    """Envoltorio delgado sobre la API de sockets de Berkeley.

    Expone exactamente las operaciones del lado C:
        send_all / recv_all / send_msg / recv_msg
    """

    def __init__(self):
        self.sock = None
        self.connected = False

    # -- conexión ------------------------------------------------------------
    def connect(self, ip, port, timeout=10.0):
        """socket(AF_INET, SOCK_STREAM) -> connect(). Devuelve error o None."""
        try:
            self.sock = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
            self.sock.settimeout(timeout)
            self.sock.connect((ip, int(port)))
            self.connected = True
            return None
        except OSError as e:
            self.connected = False
            return str(e)

    def close(self):
        self.connected = False
        if self.sock is not None:
            try:
                self.sock.close()
            except OSError:
                pass
            self.sock = None

    # -- primitivas de E/S bloqueantes exactas (send_all / recv_all) ---------
    def _send_all(self, data):
        total = 0
        while total < len(data):
            sent = self.sock.send(data[total:])
            if sent <= 0:
                return -1
            total += sent
        return 0

    def _recv_all(self, length):
        buf = bytearray()
        while len(buf) < length:
            chunk = self.sock.recv(length - len(buf))
            if not chunk:
                return None  # socket cerrado por el par
            buf += chunk
        return bytes(buf)

    # -- mensajes (header + payload) ----------------------------------------
    def send_msg(self, msg_type, payload=b""):
        header = struct.pack("!BH", msg_type, len(payload))
        if self._send_all(header) < 0:
            return -1
        if payload:
            if self._send_all(payload) < 0:
                return -1
        return 0

    def recv_msg(self):
        """Devuelve (type, payload_bytes) o lanza ConnectionError/struct.error."""
        hdr = self._recv_all(3)
        if hdr is None:
            raise ConnectionError("Servidor cerró la conexión")
        msg_type, length = struct.unpack("!BH", hdr)
        payload = b""
        if length > 0:
            payload = self._recv_all(length)
            if payload is None:
                raise ConnectionError("Conexión perdida durante el payload")
        return msg_type, payload

    # -- helpers de alto nivel ----------------------------------------------
    def register(self, nickname, email):
        nick = nickname.encode("utf-8")[:MAX_NICKNAME_LEN - 1]
        mail = email.encode("utf-8")[:MAX_EMAIL_LEN - 1]
        payload = struct.pack(FMT_REGISTER_REQ,
                              nick.ljust(MAX_NICKNAME_LEN, b"\x00"),
                              mail.ljust(MAX_EMAIL_LEN, b"\x00"))
        return self.send_msg(MSG_REGISTER_REQ, payload)

    def recv_register_resp(self):
        _t, p = self.recv_msg()
        status, player_id = struct.unpack(FMT_REGISTER_RESP, p[:2])
        return status, player_id

    def recv_match_start(self):
        _t, p = self.recv_msg()
        role, match_id, opp = struct.unpack(FMT_MATCH_START, p[:34])
        opponent = opp.split(b"\x00", 1)[0].decode("utf-8", "ignore")
        return role, match_id, opponent

    def send_move(self, direction):
        return self.send_msg(MSG_PLAYER_MOVE, struct.pack(FMT_PLAYER_MOVE, direction))


# =============================================================================
#  Modelo de estado del juego (compartido por modo red y práctica)
# =============================================================================
class GameState:
    def __init__(self):
        self.ball_x = LOGICAL_W // 2
        self.ball_y = LOGICAL_H // 2
        self.paddle1_y = LOGICAL_H // 2
        self.paddle2_y = LOGICAL_H // 2
        self.score_p1 = 0
        self.score_p2 = 0
        self.frame_id = 0

    def apply(self, ball_x, ball_y, p1, p2, s1, s2, frame):
        self.ball_x = ball_x
        self.ball_y = ball_y
        self.paddle1_y = p1
        self.paddle2_y = p2
        self.score_p1 = s1
        self.score_p2 = s2
        self.frame_id = frame


# =============================================================================
#  Motor de simulación local (modo PRÁCTICA, sin servidor)
# =============================================================================
class LocalEngine:
    """Simula Pong para poder ver la interfaz sin necesitar el servidor."""

    FIELD_W = LOGICAL_W
    FIELD_H = LOGICAL_H
    PADDLE_H = 110
    PADDLE_W = 16
    PADDLE_SPEED = 420          # unidades lógicas / segundo
    BALL_SPEED0 = 380
    BALL_SPEED_GAIN = 1.06
    WIN_SCORE = 5

    def __init__(self):
        self.state = GameState()
        self.reset_ball(random.choice([-1, 1]))
        self.vx = 0.0
        self.vy = 0.0
        self.p1 = float(self.FIELD_H // 2)
        self.p2 = float(self.FIELD_H // 2)
        self.over = False
        self.winner = 0

    def reset_ball(self, dirx):
        self.state.ball_x = self.FIELD_W // 2
        self.state.ball_y = self.FIELD_H // 2
        ang = random.uniform(-0.6, 0.6)
        sp = self.BALL_SPEED0
        self.vx = sp * dirx
        self.vy = sp * ang

    def step(self, dt, my_dir, my_role):
        if self.over:
            return
        # Mueve la pala del jugador local según su input
        my_speed = self.PADDLE_SPEED * dt
        if my_role == ROLE_PLAYER_1:
            self.p1 += my_dir * my_speed
            self.p1 = clamp(self.p1, self.PADDLE_H / 2, self.FIELD_H - self.PADDLE_H / 2)
            # IA para la pala derecha
            self.p2 = self._ai_track(self.p2, dt)
        else:
            self.p2 += my_dir * my_speed
            self.p2 = clamp(self.p2, self.PADDLE_H / 2, self.FIELD_H - self.PADDLE_H / 2)
            self.p1 = self._ai_track(self.p1, dt)

        # Física de la pelota
        self.state.ball_x += self.vx * dt
        self.state.ball_y += self.vy * dt

        # Rebote superior/inferior
        r = 12
        if self.state.ball_y - r <= 0:
            self.state.ball_y = r
            self.vy = abs(self.vy)
        elif self.state.ball_y + r >= self.FIELD_H:
            self.state.ball_y = self.FIELD_H - r
            self.vy = -abs(self.vy)

        # Colisión con palas (simple, por AABB)
        pw = self.PADDLE_W
        ph = self.PADDLE_H
        bx, by = self.state.ball_x, self.state.ball_y
        # Pala izquierda
        if (bx - r <= pw and self.p1 - ph / 2 <= by <= self.p1 + ph / 2 and self.vx < 0):
            self.vx = -self.vx * self.BALL_SPEED_GAIN
            off = (by - self.p1) / (ph / 2)
            self.vy += off * 220
            bx = pw + r
        # Pala derecha
        if (bx + r >= self.FIELD_W - pw and self.p2 - ph / 2 <= by <= self.p2 + ph / 2 and self.vx > 0):
            self.vx = -self.vx * self.BALL_SPEED_GAIN
            off = (by - self.p2) / (ph / 2)
            self.vy += off * 220
            bx = self.FIELD_W - pw - r
        self.state.ball_x = bx

        # Puntos
        if self.state.ball_x < -20:
            self.state.score_p2 += 1
            self.reset_ball(1)
        elif self.state.ball_x > self.FIELD_W + 20:
            self.state.score_p1 += 1
            self.reset_ball(-1)

        # Fin de partida
        if self.state.score_p1 >= self.WIN_SCORE:
            self.over, self.winner = True, ROLE_PLAYER_1
        elif self.state.score_p2 >= self.WIN_SCORE:
            self.over, self.winner = True, ROLE_PLAYER_2

    def _ai_track(self, y, dt):
        # IA con error para que sea divertido
        target = self.state.ball_y + random.uniform(-18, 18)
        diff = target - y
        step = clamp(diff, -1, 1) * self.PADDLE_SPEED * 0.82 * dt
        y += step
        return clamp(y, self.PADDLE_H / 2, self.FIELD_H - self.PADDLE_H / 2)


# =============================================================================
#  Widget: campo de texto editable
# =============================================================================
class InputField:
    def __init__(self, x, y, w, h, label, value="", placeholder="", secret=False):
        self.rect = pygame.Rect(x, y, w, h)
        self.label = label
        self.value = value
        self.placeholder = placeholder
        self.secret = secret
        self.focus = False
        self.blink = 0.0

    def handle(self, event):
        if event.type == pygame.MOUSEBUTTONDOWN:
            self.focus = self.rect.collidepoint(event.pos)
            return False
        if not self.focus:
            return False
        if event.type == pygame.KEYDOWN:
            if event.key == pygame.K_BACKSPACE:
                self.value = self.value[:-1]
            elif event.key == pygame.K_TAB:
                self.focus = False
            elif event.unicode and event.unicode.isprintable() and len(self.value) < 28:
                self.value += event.unicode
            return True
        return False

    def draw(self, surface, font, dt):
        self.blink += dt
        # Marco
        col = CYAN if self.focus else DIM
        pygame.draw.rect(surface, (12, 18, 36), self.rect)
        pygame.draw.rect(surface, col, self.rect, 2)
        if self.focus:
            pygame.draw.rect(surface, (0, 120, 140), self.rect.inflate(8, 8), 1)
        # Etiqueta
        lab = font.render(self.label, True, DIM)
        surface.blit(lab, (self.rect.x, self.rect.y - 20))
        # Texto
        shown = ("*" * len(self.value)) if self.secret else self.value
        if not shown and not self.focus:
            txt = font.render(self.placeholder, True, (60, 80, 120))
        else:
            txt = font.render(shown + ("|" if self.focus and int(self.blink * 2) % 2 == 0 else ""), True, WHITE)
        surface.blit(txt, (self.rect.x + 10, self.rect.y + self.rect.height // 2 - txt.get_height() // 2))


# =============================================================================
#  Widget: botón neón con efecto hover
# =============================================================================
class Button:
    def __init__(self, x, y, w, h, label, color=CYAN):
        self.rect = pygame.Rect(x, y, w, h)
        self.label = label
        self.color = color
        self.hover = False
        self.glow = 0.0

    def handle(self, event):
        if event.type == pygame.MOUSEMOTION:
            self.hover = self.rect.collidepoint(event.pos)
        if event.type == pygame.MOUSEBUTTONDOWN and event.button == 1:
            if self.rect.collidepoint(event.pos):
                self.glow = 1.0
                return True
        return False

    def draw(self, surface, font, dt):
        self.glow = max(0.0, self.glow - dt * 2.5)
        if self.hover:
            self.glow = max(self.glow, 0.5)
        # Halo neón
        if self.glow > 0:
            halo = pygame.Surface((self.rect.w + 40, self.rect.h + 40), pygame.SRCALPHA)
            a = int(90 * self.glow)
            pygame.draw.rect(halo, self.color + (a,),
                             halo.get_rect(), border_radius=8)
            surface.blit(halo, (self.rect.x - 20, self.rect.y - 20))
        pygame.draw.rect(surface, (10, 16, 34), self.rect, border_radius=6)
        pygame.draw.rect(surface, self.color, self.rect, 2, border_radius=6)
        txt = font.render(self.label, True, self.color if self.hover else WHITE)
        surface.blit(txt, (self.rect.centerx - txt.get_width() // 2,
                           self.rect.centery - txt.get_height() // 2))


# =============================================================================
#  Efectos de fondo: estrellas + retícula + scanlines
# =============================================================================
class Background:
    def __init__(self, w, h):
        self.w, self.h = w, h
        self.stars = [[random.uniform(0, w), random.uniform(0, h),
                       random.uniform(0.3, 1.6), random.randint(60, 180)]
                      for _ in range(90)]
        self.scan = 0

    def update(self, dt):
        for s in self.stars:
            s[0] -= s[2] * 30 * dt
            if s[0] < 0:
                s[0] = self.w
                s[1] = random.uniform(0, self.h)
        self.scan = (self.scan + 60 * dt) % 4

    def draw(self, surface):
        surface.fill(BG)
        # Retícula tenue
        for x in range(0, self.w, 50):
            pygame.draw.line(surface, GRID, (x, 0), (x, self.h))
        for y in range(0, self.h, 50):
            pygame.draw.line(surface, GRID, (0, y), (self.w, y))
        # Estrellas
        for s in self.stars:
            surface.fill((s[3], s[3], min(255, s[3] + 40)), (s[0], s[1], 2, 2))


# =============================================================================
#  Aplicación principal (máquina de estados)
# =============================================================================
class PongApp:
    # Estados
    MENU = "menu"
    LOBBY = "lobby"
    PLAYING = "playing"
    GAMEOVER = "gameover"
    ERROR = "error"

    def __init__(self):
        pygame.init()
        pygame.display.set_caption("PONG · NEON CLIENT · MyAppGameProtocol")
        self.W, self.H = 1200, 720
        self.screen = pygame.display.set_mode((self.W, self.H))
        self.clock = pygame.time.Clock()
        self.bg = Background(self.W, self.H)

        self.font_lg = self._load_font(58, bold=True)
        self.font_md = self._load_font(30)
        self.font_sm = self._load_font(22)
        self.font_xl = self._load_font(96, bold=True)

        self.net = NetClient()
        self.state = self.MENU
        self.state_time = 0.0
        self.status_msg = ""
        self.error_msg = ""

        # Datos de partida en red
        self.role = ROLE_PLAYER_1
        self.match_id = 0
        self.opponent = "?"
        self.game_state = GameState()
        self.net_queue = queue.Queue()
        self.net_thread = None
        self.net_running = False
        self.last_sent_move = None

        # Motor local (práctica)
        self.engine = None

        # UI del menú
        self._build_menu()

        # Partículas de impacto
        self.particles = []
        self.shake = 0.0

    def _load_font(self, size, bold=False):
        for name in ("Menlo", "Monaco", "Consolas", "Courier New"):
            f = pygame.font.SysFont(name, size, bold=bold)
            if f:
                return f
        return pygame.font.Font(None, size)

    def _build_menu(self):
        cx = self.W // 2
        self.ip_field = InputField(cx - 240, 250, 480, 46, "SERVIDOR (IP)", "127.0.0.1")
        self.port_field = InputField(cx - 240, 330, 480, 46, "PUERTO", "8080")
        self.nick_field = InputField(cx - 240, 410, 235, 46, "NICKNAME", "Player1")
        self.mail_field = InputField(cx + 5, 410, 235, 46, "EMAIL", "player@mail.com")
        self.fields = [self.ip_field, self.port_field, self.nick_field, self.mail_field]

        self.btn_net = Button(cx - 250, 510, 240, 58, "JUGAR EN RED", CYAN)
        self.btn_prac = Button(cx + 10, 510, 240, 58, "PRÁCTICA (OFF)", MAGENTA)

        self.btn_retry = Button(cx - 250, 470, 240, 58, "REINTENTAR", CYAN)
        self.btn_back = Button(cx + 10, 470, 240, 58, "MENÚ", MAGENTA)

        self.btn_again = Button(cx - 250, 500, 240, 58, "REVANCHA", CYAN)
        self.btn_menu2 = Button(cx + 10, 500, 240, 58, "MENÚ", MAGENTA)

    # ------------------------------------------------------------------ red
    def start_network_game(self):
        self.status_msg = "Conectando al servidor..."
        self._enter_state(self.LOBBY)
        ip = self.ip_field.value.strip()
        port = self.port_field.value.strip() or "8080"
        nick = self.nick_field.value.strip() or "Player"
        mail = self.mail_field.value.strip() or "player@mail.com"
        threading.Thread(target=self._net_connect_worker,
                         args=(ip, port, nick, mail), daemon=True).start()

    def _net_connect_worker(self, ip, port, nick, mail):
        err = self.net.connect(ip, port)
        if err:
            self.net_queue.put(("error", f"No se pudo conectar: {err}"))
            return
        # 1) Registro
        if self.net.register(nick, mail) < 0:
            self.net_queue.put(("error", "Fallo al enviar registro."))
            return
        # 2) Espera de confirmación
        try:
            status, pid = self.net.recv_register_resp()
        except (ConnectionError, struct.error) as e:
            self.net_queue.put(("error", f"Registro fallido: {e}"))
            return
        if status != STATUS_REGISTER_OK:
            self.net_queue.put(("error", "El lobby está lleno. Intenta más tarde."))
            return
        self.net_queue.put(("registered", pid))
        # 3) Bucle de recepción (match / estado / fin)
        self.net_running = True
        try:
            while self.net_running:
                mtype, payload = self.net.recv_msg()
                if mtype == MSG_MATCH_START:
                    role, match_id, opp = struct.unpack(FMT_MATCH_START, payload[:34])
                    opp = opp.split(b"\x00", 1)[0].decode("utf-8", "ignore")
                    self.net_queue.put(("match", (role, match_id, opp)))
                elif mtype == MSG_GAME_STATE:
                    vals = struct.unpack(FMT_GAME_STATE, payload[:16])
                    self.net_queue.put(("state", vals))
                elif mtype == MSG_GAME_OVER:
                    vals = struct.unpack(FMT_GAME_OVER, payload[:3])
                    self.net_queue.put(("over", vals))
                elif mtype == MSG_DISCONNECT:
                    self.net_queue.put(("closed", None))
                    break
                else:
                    self.net_queue.put(("closed", f"Opcode inesperado 0x{mtype:02X}"))
                    break
        except (ConnectionError, struct.error, OSError) as e:
            self.net_queue.put(("closed", str(e)))

    def _net_send_move(self, direction):
        if direction != self.last_sent_move:
            if self.net.send_move(direction) == 0:
                self.last_sent_move = direction

    # -------------------------------------------------------------- estados
    def _enter_state(self, st):
        self.state = st
        self.state_time = 0.0

    def _drain_net_queue(self):
        try:
            while True:
                kind, data = self.net_queue.get_nowait()
                if kind == "error":
                    self.error_msg = data
                    self.net.close()
                    self._enter_state(self.ERROR)
                elif kind == "registered":
                    self.status_msg = "Registro OK. Buscando rival en el Lobby..."
                elif kind == "match":
                    self.role, self.match_id, self.opponent = data
                    self.game_state = GameState()
                    self.last_sent_move = None
                    self._enter_state(self.PLAYING)
                elif kind == "state":
                    # [JUAN] El payload MSG_GAME_STATE trae 8 campos
                    # (ball_x, ball_y, p1_y, p2_y, s1, s2, frame_id, reserved),
                    # pero apply() toma 7; ignoramos el campo 'reserved'.
                    self.game_state.apply(*data[:7])
                elif kind == "over":
                    winner, s1, s2 = data
                    self.game_state.score_p1 = s1
                    self.game_state.score_p2 = s2
                    self.winner = winner
                    self._enter_state(self.GAMEOVER)
                elif kind == "closed":
                    if self.state == self.PLAYING:
                        # El servidor aún no implementa el loop de juego.
                        self.error_msg = ("Conexión cerrada por el servidor.\n"
                                          "El PongServer todavía no implementa el\n"
                                          "bucle de juego (MSG_GAME_STATE).")
                        self._enter_state(self.ERROR)
        except queue.Empty:
            pass

    # ----------------------------------------------------------------- loop
    def run(self):
        while True:
            dt = self.clock.tick(60) / 1000.0
            self.state_time += dt
            self.bg.update(dt)

            for event in pygame.event.get():
                if event.type == pygame.QUIT:
                    self._shutdown()
                    return
                if event.type == pygame.KEYDOWN and event.key == pygame.K_ESCAPE:
                    self._shutdown()
                    return
                self._handle_event(event)

            self._drain_net_queue()
            if self.state == self.PLAYING:
                self._update_playing(dt)

            self._draw(dt)
            pygame.display.flip()

    def _handle_event(self, event):
        if self.state == self.MENU:
            for f in self.fields:
                f.handle(event)
            if self.btn_net.handle(event):
                self.start_network_game()
            if self.btn_prac.handle(event):
                self.role = ROLE_PLAYER_1
                self.engine = LocalEngine()
                self.match_id = 0
                self.opponent = "CPU"
                self._enter_state(self.PLAYING)
        elif self.state == self.ERROR:
            if self.btn_retry.handle(event):
                self._enter_state(self.MENU)
                self.start_network_game()
            if self.btn_back.handle(event):
                self._enter_state(self.MENU)
        elif self.state == self.GAMEOVER:
            if self.btn_again.handle(event):
                if self.engine:
                    self.engine = LocalEngine()
                    self._enter_state(self.PLAYING)
                else:
                    self._enter_state(self.MENU)
            if self.btn_menu2.handle(event):
                self.engine = None
                self.net.close()
                self._enter_state(self.MENU)

    def _current_dir(self):
        keys = pygame.key.get_pressed()
        if keys[pygame.K_UP] or keys[pygame.K_w]:
            return MOVE_UP
        if keys[pygame.K_DOWN] or keys[pygame.K_s]:
            return MOVE_DOWN
        return MOVE_STILL

    def _update_playing(self, dt):
        d = self._current_dir()
        if self.engine:
            # Modo práctica: simulación local
            prev_vx = self.engine.vx
            self.engine.step(dt, d, self.role)
            self.game_state = self.engine.state
            # Partículas cuando la pelota rebota en una pala
            if (prev_vx < 0) != (self.engine.vx < 0):
                rect, scale = self._play_rect()
                px = rect.x + self.engine.state.ball_x * scale
                py = rect.y + self.engine.state.ball_y * scale
                self._spawn_burst(px, py, YELLOW)
            if self.engine.over and self.state != self.GAMEOVER:
                self.winner = self.engine.winner
                self._enter_state(self.GAMEOVER)
        else:
            # Modo red: enviamos la intención de movimiento al servidor
            self._net_send_move(d)

    # ---------------------------------------------------------------- dibujo
    def _draw(self, dt):
        self.bg.draw(self.screen)

        if self.state == self.MENU:
            self._draw_menu(dt)
        elif self.state == self.LOBBY:
            self._draw_lobby(dt)
        elif self.state == self.PLAYING:
            self._draw_game(dt)
        elif self.state == self.GAMEOVER:
            self._draw_gameover(dt)
        elif self.state == self.ERROR:
            self._draw_error(dt)

        self._draw_scanlines()
        if self.shake > 0:
            self.shake = max(0.0, self.shake - dt * 4)

    def _glow_text(self, text, font, color, pos, center=True, glow=6):
        base = font.render(text, True, color)
        x, y = pos
        if center:
            x -= base.get_width() // 2
            y -= base.get_height() // 2
        # Capas de halo para simular bloom
        for i in range(glow, 0, -1):
            a = max(0, 40 - i * 5)
            halo = font.render(text, True, tuple(max(0, c - 60) for c in color))
            for dx, dy in ((-i, 0), (i, 0), (0, -i), (0, i)):
                self.screen.blit(halo, (x + dx, y + dy))
        self.screen.blit(base, (x, y))

    def _draw_menu(self, dt):
        self._glow_text("P O N G", self.font_xl, CYAN, (self.W // 2, 130))
        self._glow_text("NEON CLIENT · MyAppGameProtocol v1.0", self.font_sm,
                        MAGENTA, (self.W // 2, 200))
        for f in self.fields:
            f.draw(self.screen, self.font_sm, dt)
        self.btn_net.draw(self.screen, self.font_md, dt)
        self.btn_prac.draw(self.screen, self.font_md, dt)
        help_txt = self.font_sm.render(
            "W/↑ y S/↓ mueven la pala · ESC sale", True, DIM)
        self.screen.blit(help_txt, (self.W // 2 - help_txt.get_width() // 2, 610))

    def _draw_lobby(self, dt):
        self._glow_text("L O B B Y", self.font_lg, CYAN, (self.W // 2, 180))
        msg = self.font_md.render(self.status_msg, True, WHITE)
        self.screen.blit(msg, (self.W // 2 - msg.get_width() // 2, 300))
        # Puntos animados de espera
        dots = "." * (int(self.state_time * 2) % 4)
        dm = self.font_md.render(dots, True, CYAN)
        self.screen.blit(dm, (self.W // 2 + msg.get_width() // 2 + 6, 300))
        info = self.font_sm.render(
            f"Conectado a {self.ip_field.value}:{self.port_field.value}  ·  "
            f"Nick: {self.nick_field.value}", True, DIM)
        self.screen.blit(info, (self.W // 2 - info.get_width() // 2, 360))

    def _play_rect(self):
        # Área de juego con relación de aspecto 1000:600 centrada
        margin_top = 120
        avail_w = self.W - 80
        avail_h = self.H - margin_top - 60
        scale = min(avail_w / LOGICAL_W, avail_h / LOGICAL_H)
        w = int(LOGICAL_W * scale)
        h = int(LOGICAL_H * scale)
        x = (self.W - w) // 2
        y = margin_top + (avail_h - h) // 2
        return pygame.Rect(x, y, w, h), scale

    def _draw_game(self, dt):
        gs = self.game_state
        rect, scale = self._play_rect()
        ox, oy = rect.x, rect.y

        def L(x, y):
            return (ox + x * scale, oy + y * scale)

        # Marco del campo neón
        pygame.draw.rect(self.screen, (14, 22, 44), rect)
        pygame.draw.rect(self.screen, CYAN, rect, 3)

        # Línea central discontinua
        for yy in range(0, LOGICAL_H, 40):
            pygame.draw.line(self.screen, DIM, L(LOGICAL_W // 2, yy),
                             L(LOGICAL_W // 2, yy + 22), 3)

        # Marcador
        self._glow_text(str(gs.score_p1), self.font_xl, CYAN,
                        (self.W // 2 - 120, 60))
        self._glow_text(str(gs.score_p2), self.font_xl, MAGENTA,
                        (self.W // 2 + 120, 60))

        # Palas (con halo)
        pw = int(LocalEngine.PADDLE_W * scale)
        ph = int(LocalEngine.PADDLE_H * scale)
        p1 = L(0, gs.paddle1_y - LocalEngine.PADDLE_H / 2)
        p2 = L(LOGICAL_W - LocalEngine.PADDLE_W, gs.paddle2_y - LocalEngine.PADDLE_H / 2)
        self._neon_rect(pygame.Rect(p1[0], p1[1], pw, ph), CYAN)
        self._neon_rect(pygame.Rect(p2[0], p2[1], pw, ph), MAGENTA)

        # Pelota (con halo + estela)
        bx, by = L(gs.ball_x, gs.ball_y)
        r = int(12 * scale)
        self._neon_circle((bx, by), r + 10, YELLOW, alpha=60)
        self._neon_circle((bx, by), r, WHITE)

        # HUD inferior
        mode = "PRÁCTICA (offline)" if self.engine else f"RED · Partida #{self.match_id}"
        role = "Pala Izquierda (P1)" if self.role == ROLE_PLAYER_1 else "Pala Derecha (P2)"
        hud = self.font_sm.render(f"{mode}  ·  Tu rol: {role}  ·  Rival: {self.opponent}",
                                  True, DIM)
        self.screen.blit(hud, (self.W // 2 - hud.get_width() // 2, self.H - 40))

        # Partículas
        self._update_particles(dt)

    def _neon_rect(self, rect, color):
        s = pygame.Surface((rect.w + 24, rect.h + 24), pygame.SRCALPHA)
        pygame.draw.rect(s, color + (50,), s.get_rect(), border_radius=4)
        self.screen.blit(s, (rect.x - 12, rect.y - 12))
        pygame.draw.rect(self.screen, color, rect, border_radius=3)
        inner = pygame.Surface((max(2, rect.w - 6), max(2, rect.h - 6)), pygame.SRCALPHA)
        inner.fill((255, 255, 255, 120))
        self.screen.blit(inner, (rect.x + 3, rect.y + 3))

    def _neon_circle(self, center, r, color, alpha=255):
        s = pygame.Surface((r * 2 + 4, r * 2 + 4), pygame.SRCALPHA)
        pygame.draw.circle(s, color + (alpha // 3,), (r + 2, r + 2), r)
        self.screen.blit(s, (center[0] - r - 2, center[1] - r - 2))
        pygame.draw.circle(self.screen, color, center, r)

    def _update_particles(self, dt):
        for p in self.particles[:]:
            p[0] += p[2] * dt
            p[1] += p[3] * dt
            p[4] -= dt
            if p[4] <= 0:
                self.particles.remove(p)
        for p in self.particles:
            a = clamp(int(p[4] * 255), 0, 255)
            pygame.draw.circle(self.screen, YELLOW + (a,),
                               (int(p[0]), int(p[1])), 3)

    def _spawn_burst(self, x, y, color):
        for _ in range(24):
            ang = random.uniform(0, 6.28)
            sp = random.uniform(60, 260)
            self.particles.append([x, y, math.cos(ang) * sp, math.sin(ang) * sp,
                                   random.uniform(0.4, 0.9)])
        self.shake = 1.0

    def _draw_gameover(self, dt):
        self._draw_game(dt * 0.2)
        veil = pygame.Surface((self.W, self.H), pygame.SRCALPHA)
        veil.fill((4, 6, 14, 190))
        self.screen.blit(veil, (0, 0))
        win = (self.winner == self.role) if not self.engine else (self.winner == self.role)
        if win:
            self._glow_text("VICTORIA", self.font_xl, GREEN, (self.W // 2, 220))
        else:
            self._glow_text("DERROTA", self.font_xl, RED, (self.W // 2, 220))
        sc = self.font_md.render(
            f"MARCADOR FINAL  {self.game_state.score_p1}  -  {self.game_state.score_p2}",
            True, WHITE)
        self.screen.blit(sc, (self.W // 2 - sc.get_width() // 2, 340))
        self.btn_again.draw(self.screen, self.font_md, dt)
        self.btn_menu2.draw(self.screen, self.font_md, dt)

    def _draw_error(self, dt):
        self._glow_text("ERROR DE CONEXIÓN", self.font_lg, RED, (self.W // 2, 180))
        lines = self.error_msg.split("\n")
        y = 280
        for ln in lines:
            t = self.font_md.render(ln, True, WHITE)
            self.screen.blit(t, (self.W // 2 - t.get_width() // 2, y))
            y += 40
        self.btn_retry.draw(self.screen, self.font_md, dt)
        self.btn_back.draw(self.screen, self.font_md, dt)

    def _draw_scanlines(self):
        # Bandas horizontales sutiles estilo CRT
        for y in range(0, self.H, 4):
            if (y + int(self.bg.scan)) % 8 == 0:
                s = pygame.Surface((self.W, 1), pygame.SRCALPHA)
                s.fill((0, 0, 0, 22))
                self.screen.blit(s, (0, y))

    def _shutdown(self):
        if self.net.connected:
            try:
                self.net.send_msg(MSG_DISCONNECT)
            except OSError:
                pass
        self.net_running = False
        self.net.close()
        pygame.quit()
        sys.exit(0)


if __name__ == "__main__":
    os.environ.setdefault("SDL_VIDEO_CENTERED", "1")
    PongApp().run()
