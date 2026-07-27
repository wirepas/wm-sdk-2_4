/* Copyright 2025 licensed under Apache License, Version 2.0
 *
 * gw26 wires the nRF9160<->nRF52840 UART directly (PCB traces), unlike the
 * nRF9160 DK's companion nRF52840 which needs its analog switches enabled
 * here (see board/nrf9160dk_nrf52840/board_custom_init.c) - nothing to do
 * before the rest of the app starts.
 */
void Board_custom_init(void)
{
}
