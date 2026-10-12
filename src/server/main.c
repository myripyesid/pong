#include <stdio.h>
#include "protocol.h"

int main(void) {
    printf("=== Verificando tamaños de estructuras de MyAppGameProtocol ===\n");
    printf("Header:          %zu bytes (esperado: 3)\n", sizeof(msg_header_t));
    printf("Register Req:    %zu bytes (esperado: 64)\n", sizeof(msg_register_req_t));
    printf("Register Resp:   %zu bytes (esperado: 2)\n", sizeof(msg_register_resp_t));
    printf("Match Start:     %zu bytes (esperado: 34)\n", sizeof(msg_match_start_t));
    printf("Player Move:     %zu bytes (esperado: 1)\n", sizeof(msg_player_move_t));
    printf("Game State:      %zu bytes (esperado: 16)\n", sizeof(msg_game_state_t));
    printf("Game Over:       %zu bytes (esperado: 3)\n", sizeof(msg_game_over_t));
    return 0;
}
