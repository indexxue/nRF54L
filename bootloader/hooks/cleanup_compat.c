/*
 * When CONFIG_MCUBOOT_NRF_CLEANUP_PERIPHERAL=n, mcuboot main.c still calls
 * nrf_crypto_keys_housekeeping() but does not include nrf_cleanup.h (which
 * normally provides an empty macro). Provide a weak stub for that case.
 */
void nrf_crypto_keys_housekeeping(void) __attribute__((weak));

void nrf_crypto_keys_housekeeping(void)
{
}
