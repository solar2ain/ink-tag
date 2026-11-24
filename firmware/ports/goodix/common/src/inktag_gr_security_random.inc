bool inktag_gr_security_random(void *context, uint8_t *output, uint16_t length) {
    rng_handle_t rng;
    (void)context;
    if (output == NULL || length == 0u) return false;
    memset(&rng, 0, sizeof(rng));
    rng.p_instance = RNG;
    rng.init.seed_mode = RNG_SEED_FR0_S0;
    rng.init.lfsr_mode = RNG_LFSR_MODE_59BIT;
    rng.init.out_mode = RNG_OUTPUT_FR0_S0;
    rng.init.post_mode = RNG_POST_PRO_NOT;
    (void)hal_rng_deinit(&rng);
    if (hal_rng_init(&rng) != HAL_OK) return false;
    while (length > 0u) {
        uint32_t value = 0u;
        uint16_t chunk = length > sizeof(value) ? sizeof(value) : length;
        if (hal_rng_generate_random_number(&rng, NULL, &value) != HAL_OK) {
            (void)hal_rng_deinit(&rng);
            return false;
        }
        memcpy(output, &value, chunk);
        output += chunk;
        length -= chunk;
    }
    (void)hal_rng_deinit(&rng);
    return true;
}

