#include <assert.h>
#include <stdint.h>
#include <stdio.h>

#include "inktag_protocol_v2.h"
#include "inktag_protocol_core.h"
#include "inktag_release_v1.h"

static void test_capability_requires_every_layer(void) {
    inktag_capability_gate_t gate = {true, true, true, true};
    assert(inktag_capability_available(&gate));
    assert(inktag_capability_add(0u, INKTAG_CAPABILITY_NFC, &gate) ==
           INKTAG_CAPABILITY_NFC);
    gate.board_qualified = false;
    assert(!inktag_capability_available(&gate));
    assert(inktag_capability_add(INKTAG_CAPABILITY_TIME,
                                 INKTAG_CAPABILITY_NFC, &gate) ==
           INKTAG_CAPABILITY_TIME);
    gate.board_qualified = true;
    gate.runtime_available = false;
    assert(!inktag_capability_available(&gate));
    assert(!inktag_capability_available(NULL));
}

static void test_persistent_mutation_classification(void) {
    const uint8_t reset[] = {INKTAG_SYSTEM_ACTION_RESET};
    const uint8_t sleep[] = {INKTAG_SYSTEM_ACTION_SLEEP};
    const uint8_t clear[] = {INKTAG_SYSTEM_ACTION_CLEAR_USER_DATA};
    const uint8_t retired[] = {INKTAG_SYSTEM_ACTION_FACTORY_RESET};
    const uint8_t clear_find[] = {INKTAG_FIND_ACTION_CLEAR_CONFIG};
    assert(inktag_protocol_command_mutates_persistent_state(
        INKTAG_CMD_SET_TIME, NULL, 0u));
    assert(!inktag_protocol_command_mutates_persistent_state(
        INKTAG_CMD_GET_TIME, NULL, 0u));
    assert(!inktag_protocol_command_mutates_persistent_state(
        INKTAG_CMD_SYSTEM_ACTION, reset, sizeof(reset)));
    assert(!inktag_protocol_command_mutates_persistent_state(
        INKTAG_CMD_SYSTEM_ACTION, retired, sizeof(retired)));
    assert(inktag_protocol_command_mutates_persistent_state(
        INKTAG_CMD_SYSTEM_ACTION, sleep, sizeof(sleep)));
    assert(inktag_protocol_command_mutates_persistent_state(
        INKTAG_CMD_SYSTEM_ACTION, clear, sizeof(clear)));
    assert(!inktag_protocol_command_mutates_persistent_state(
        INKTAG_CMD_SET_FIND_CONFIG, NULL, 0u));
    assert(!inktag_protocol_command_mutates_persistent_state(
        INKTAG_CMD_FIND_ACTION, clear_find, sizeof(clear_find)));
    assert(!inktag_protocol_command_mutates_persistent_state(
        INKTAG_CMD_FIND_ACTION, NULL, 0u));
    assert(!inktag_protocol_command_mutates_persistent_state(
        INKTAG_CMD_SYSTEM_ACTION, NULL, 0u));
}

static void test_request_validation(void) {
    const uint8_t valid[] = {INKTAG_CMD_HELLO, 7u, INKTAG_RELEASE_FRAME_TAG};
    const uint8_t bad_transaction[] = {INKTAG_CMD_HELLO, 0u, 0u};
    const uint8_t bad_flags[] = {INKTAG_CMD_HELLO, 1u, 0x80u};
    inktag_protocol_request_t request;
    assert(inktag_protocol_parse_request(valid, sizeof(valid), &request) ==
           INKTAG_STATUS_OK);
    assert(request.opcode == INKTAG_CMD_HELLO);
    assert(request.transaction_id == 7u);
    assert(request.payload_length == 0u);
    assert(inktag_protocol_parse_request(bad_transaction,
                                         sizeof(bad_transaction), &request) ==
           INKTAG_STATUS_INVALID_REQUEST);
    assert(inktag_protocol_parse_request(bad_flags, sizeof(bad_flags), &request) ==
           INKTAG_STATUS_INVALID_REQUEST);
    assert(inktag_protocol_parse_request(valid, 2u, &request) ==
           INKTAG_STATUS_INVALID_REQUEST);
    /* Check every wire flag and transaction boundary against the production
     * parser; rejected old tags cannot partially overwrite a request. */
    for (unsigned wire = 0u; wire < 256u; ++wire) {
        uint8_t packet[] = {INKTAG_CMD_CANCEL, 1u, (uint8_t)wire};
        bool accepted = (wire & 0xfcu) == 0x10u;
        request.opcode = 0xa5u;
        assert((inktag_protocol_parse_request(packet, sizeof(packet), &request) ==
                INKTAG_STATUS_OK) == accepted);
        if (accepted) assert(request.flags == (wire & 3u));
        else assert(request.opcode == 0xa5u);
        packet[1] = 0u;
        assert(inktag_protocol_parse_request(packet, sizeof(packet), &request) ==
               INKTAG_STATUS_INVALID_REQUEST);
    }
}

static void test_transfer_descriptor_and_crc(void) {
    const uint8_t payload[] = {
        INKTAG_TRANSFER_OBJECT_DIRECT_FRAME, 2u, 2u, 1u,
        6u, 0u, 0u, 0u,
        0u, 0u, 0u, 0u,
        4u, 0u, 0u, 0u,
        2u, 0u, 0u, 0u,
    };
    const uint8_t check[] = "123456789";
    inktag_transfer_descriptor_t descriptor;
    assert(inktag_transfer_descriptor_decode(payload, sizeof(payload),
                                              &descriptor) == INKTAG_STATUS_OK);
    assert(inktag_transfer_descriptor_matches(&descriptor, 2u, 2u, 6u));
    assert(!inktag_transfer_descriptor_matches(&descriptor, 1u, 2u, 6u));
    assert((inktag_crc32_update(0xFFFFFFFFu, check, 9u) ^ 0xFFFFFFFFu) ==
           0xCBF43926u);
}

static void test_device_info_qr_transfer_contract(void) {
    inktag_transfer_descriptor_t descriptor = {
        INKTAG_TRANSFER_OBJECT_DEVICE_INFO_QR,
        33u,
        1u,
        0u,
        137u,
        0u,
        {137u, 0u},
    };
    assert(inktag_device_info_qr_transfer_valid(&descriptor));
    descriptor.black_mask = 1u;
    assert(!inktag_device_info_qr_transfer_valid(&descriptor));
    descriptor.black_mask = 0u;
    descriptor.format = 34u;
    assert(!inktag_device_info_qr_transfer_valid(&descriptor));
    descriptor.format = 33u;
    descriptor.total_size--;
    assert(!inktag_device_info_qr_transfer_valid(&descriptor));
}

static void test_image_codec_admission(void) {
    uint8_t payload[20] = {INKTAG_TRANSFER_OBJECT_DIRECT_FRAME, 1u, 1u, 0u};
    inktag_transfer_descriptor_t descriptor;
    inktag_write_u32_le(payload + 4u, 8u);
    inktag_write_u32_le(payload + 12u, 8u);
    for (unsigned object = 0u; object < 2u; ++object) {
        payload[0] = object ? INKTAG_TRANSFER_OBJECT_STORED_IMAGE :
                              INKTAG_TRANSFER_OBJECT_DIRECT_FRAME;
        for (unsigned flags = 0u; flags < 256u; ++flags) {
            inktag_status_v2_t expected = INKTAG_STATUS_OK;
            payload[3] = (uint8_t)flags;
            if ((flags & 0x30u) || (flags & 0xc0u) == 0xc0u)
                expected = INKTAG_STATUS_INVALID_REQUEST;
            else if (((flags & 0x80u) && !INKTAG_TRANSFER_PACKBITS_ENABLED) ||
                     ((flags & 0x40u) && !INKTAG_TRANSFER_LZO1X_ENABLED))
                expected = INKTAG_STATUS_UNSUPPORTED;
            assert(inktag_transfer_descriptor_decode(payload, sizeof(payload), &descriptor) == expected);
        }
    }
}

int main(void) {
    test_capability_requires_every_layer();
    test_persistent_mutation_classification();
    test_request_validation();
    test_transfer_descriptor_and_crc();
    test_device_info_qr_transfer_contract();
    test_image_codec_admission();
    puts("Protocol core tests passed.");
    return 0;
}
