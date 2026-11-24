bool inktag_activation_derive_device_id(
    uint16_t platform_id, uint16_t product_id,
    const uint8_t *raw_uid, uint16_t raw_uid_length,
    uint8_t output[INKTAG_ACTIVATION_ID_SIZE]) {
    uint8_t input[sizeof(k_device_id_domain) + 6u + INKTAG_ACTIVATION_RAW_UID_MAX_SIZE];
    uint8_t digest[32];
    uint16_t offset = 0u;
    if (output == NULL || raw_uid == NULL || platform_id == 0u || product_id == 0u ||
        raw_uid_length == 0u || raw_uid_length > INKTAG_ACTIVATION_RAW_UID_MAX_SIZE) return false;
    memcpy(&input[offset], k_device_id_domain, sizeof(k_device_id_domain));
    offset += sizeof(k_device_id_domain);
    put_u16_be(&input[offset], platform_id);
    offset += 2u;
    put_u16_be(&input[offset], product_id);
    offset += 2u;
    put_u16_be(&input[offset], raw_uid_length);
    offset += 2u;
    memcpy(&input[offset], raw_uid, raw_uid_length);
    offset += raw_uid_length;
    inktag_sha256(input, offset, digest);
    memcpy(output, digest, INKTAG_ACTIVATION_ID_SIZE);
    memset(digest, 0, sizeof(digest));
    memset(input, 0, sizeof(input));
    return true;
}

