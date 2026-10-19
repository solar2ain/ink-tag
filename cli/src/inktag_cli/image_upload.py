from __future__ import annotations

import asyncio
import re
import struct
import time
from collections.abc import Callable
from dataclasses import asdict, dataclass
from typing import TYPE_CHECKING

from .generated_commands_v2 import (
    CapabilityV2,
    CommandV2,
    TransferDescriptorFlagV2,
    TransferObjectV2,
)
from .image_encoding import (
    CONTENT_TYPE_IDS,
    EncodedImage,
    image_crc32,
)
from .models import ScreenConfig
from .packbits import (
    PackBitsBlock,
    build_packbits_blocks,
    packbits_is_worthwhile,
    packbits_transfer_wire_bytes,
    raw_transfer_wire_bytes,
    subsequent_segment_starts,
)
from .protocol import encode_image_name, encode_screen_config
from .transport import InkTagError

if TYPE_CHECKING:
    from .transport import InkTagClient


ProgressCallback = Callable[[str, int, int, str], None]


def normalize_wire_encoding(value: str) -> str:
    normalized = value.strip().lower()
    if normalized not in {"auto", "none", "packbits-rle"}:
        raise ValueError("wire encoding must be auto, none or packbits-rle")
    return normalized


@dataclass(frozen=True)
class ImageUploadResult:
    stored_id: int | None
    replaced_slot: int | None
    bytes_sent: int
    chunks_sent: int
    transfer_seconds: float
    refresh_reply: str
    wire_encoding: str
    encoded_bytes: int
    transfer_write_bytes_no_retries: int

    def to_dict(self) -> dict[str, int | float | str | None]:
        return asdict(self)


async def upload_encoded_image(
    client: "InkTagClient",
    encoded: EncodedImage,
    screen: ScreenConfig,
    *,
    name: str,
    save_to_slot: bool = False,
    content_type: str = "image",
    replace_slot: int | None = None,
    display_after_store: bool = True,
    refresh_timeout: float = 120.0,
    progress: ProgressCallback | None = None,
    wire_encoding: str = "auto",
) -> ImageUploadResult:
    return await _upload_encoded_image_v2(
        client,
        encoded,
        screen,
        name=name,
        save_to_slot=save_to_slot,
        content_type=content_type,
        replace_slot=replace_slot,
        display_after_store=display_after_store,
        refresh_timeout=refresh_timeout,
        progress=progress,
        wire_encoding=wire_encoding,
    )


async def _upload_encoded_image_v2(
    client: "InkTagClient",
    encoded: EncodedImage,
    screen: ScreenConfig,
    *,
    name: str,
    save_to_slot: bool,
    content_type: str,
    replace_slot: int | None,
    display_after_store: bool,
    refresh_timeout: float,
    progress: ProgressCallback | None,
    wire_encoding: str,
) -> ImageUploadResult:
    wire_encoding = normalize_wire_encoding(wire_encoding)
    rle_supported = client.supports_capability(CapabilityV2.TRANSFER_PACKBITS_RLE)
    if wire_encoding == "packbits-rle" and not rle_supported:
        raise InkTagError("Device does not advertise PackBits RLE image transfer support")
    if content_type not in CONTENT_TYPE_IDS:
        raise ValueError(f"unsupported image content type: {content_type}")

    metadata_supported = save_to_slot and client.supports_capability(CapabilityV2.IMAGE_METADATA)
    safe_name = (encode_image_name(name) if metadata_supported else
                 re.sub(r"[^a-zA-Z0-9_-]", "_", name or "image")[:19].encode("ascii"))
    metadata_size = (
        39 + len(safe_name) + (1 if replace_slot is not None else 0)
        if save_to_slot else 20
    )
    begin = bytearray(metadata_size)
    begin[0] = int(
        TransferObjectV2.STORED_IMAGE if save_to_slot else TransferObjectV2.DIRECT_FRAME
    )
    begin[1] = encoded.format
    begin[2] = len(encoded.transfers)
    begin[3] = encoded.black_mask
    struct.pack_into("<I", begin, 4, encoded.total_bytes)
    struct.pack_into("<I", begin, 8, image_crc32(encoded))
    struct.pack_into("<I", begin, 12, len(encoded.transfers[0].data))
    struct.pack_into(
        "<I", begin, 16,
        len(encoded.transfers[1].data) if len(encoded.transfers) > 1 else 0,
    )
    if save_to_slot:
        begin[20] = CONTENT_TYPE_IDS[content_type]
        begin[21:38] = encode_screen_config(screen)
        begin[38] = len(safe_name)
        begin[39:39 + len(safe_name)] = safe_name
        if replace_slot is not None:
            begin[39 + len(safe_name)] = replace_slot

    def compression_plan(chunk_size: int) -> list[tuple[PackBitsBlock, str]]:
        blocks: list[tuple[PackBitsBlock, str]] = []
        decoded_offset = 0
        for transfer in encoded.transfers:
            blocks.extend(
                (block, transfer.step)
                for block in build_packbits_blocks(
                    transfer.data,
                    decoded_offset=decoded_offset,
                    decoded_limit=0xFFFF,
                    encoded_limit=chunk_size - 2,
                )
            )
            decoded_offset += len(transfer.data)
        return blocks

    segment_lengths = [len(transfer.data) for transfer in encoded.transfers]
    # Some multi-plane controllers run their intermediate waveform when the
    # next plane begins. That synchronous work delays the ACK for the first
    # block of the subsequent segment, not the final block of the prior one.
    segment_starts = subsequent_segment_starts(segment_lengths)
    negotiated_chunk = max(20, client._v2_write_payload)
    compressed_blocks = compression_plan(negotiated_chunk)
    use_packbits = (
        wire_encoding != "none" and rle_supported
        and (wire_encoding == "packbits-rle" or packbits_is_worthwhile(
            [block for block, _step in compressed_blocks],
            segment_lengths,
            negotiated_chunk,
        ))
    )
    if use_packbits:
        begin[3] |= int(TransferDescriptorFlagV2.PACKBITS_RLE)

    transaction = client._next_v2_transaction()
    transfer_active = False
    completion = None
    completed_chunks = 0
    try:
        _report(
            progress,
            "store" if save_to_slot else "upload",
            0,
            1,
            "Preparing a transactional image slot" if save_to_slot else "Preparing image transfer",
        )
        begin_timeout = max(60.0, refresh_timeout) if not save_to_slot else 30.0
        begin_reply = await client.request_v2(
            CommandV2.TRANSFER_BEGIN,
            bytes(begin),
            transaction_id=transaction,
            timeout=begin_timeout,
        )
        transfer_active = True
        device_chunk = struct.unpack_from("<H", begin_reply, 2)[0] if len(begin_reply) >= 4 else 0
        chunk_size = max(20, min(client._v2_write_payload, device_chunk or client._v2_write_payload))
        if use_packbits and chunk_size != negotiated_chunk:
            compressed_blocks = compression_plan(chunk_size)
            if wire_encoding == "auto" and not packbits_is_worthwhile(
                [block for block, _step in compressed_blocks],
                segment_lengths,
                chunk_size,
            ):
                await client.request_v2(
                    CommandV2.TRANSFER_ABORT,
                    transaction_id=transaction,
                    timeout=5.0,
                )
                transfer_active = False
                use_packbits = False
                begin[3] &= ~int(TransferDescriptorFlagV2.PACKBITS_RLE)
                transaction = client._next_v2_transaction()
                begin_reply = await client.request_v2(
                    CommandV2.TRANSFER_BEGIN,
                    bytes(begin),
                    transaction_id=transaction,
                    timeout=begin_timeout,
                )
                transfer_active = True
                device_chunk = struct.unpack_from("<H", begin_reply, 2)[0] if len(begin_reply) >= 4 else 0
                chunk_size = max(20, min(client._v2_write_payload, device_chunk or client._v2_write_payload))
        total_chunks = len(compressed_blocks) if use_packbits else sum(
            (length + chunk_size - 1) // chunk_size for length in segment_lengths
        )
        global_offset = 0
        transfer_started = time.perf_counter()
        _report(progress, "upload", 0, total_chunks, "Uploading image layers")
        if use_packbits:
            for block, step in compressed_blocks:
                await client._write_v2_transfer_chunk(
                    transaction_id=transaction,
                    object_type=int(begin[0]),
                    total_size=encoded.total_bytes,
                    offset=block.decoded_offset,
                    data=block.encoded,
                    decoded_length=block.decoded_length,
                    timeout=(
                        90.0 if not save_to_slot and block.decoded_offset in segment_starts else 12.0
                    ),
                )
                global_offset += block.decoded_length
                completed_chunks += 1
                _report(
                    progress,
                    "upload",
                    completed_chunks,
                    total_chunks,
                    f"Uploading {step} layer",
                )
                if completed_chunks % 8 == 0:
                    await asyncio.sleep(0)
        else:
            for transfer in encoded.transfers:
                for offset in range(0, len(transfer.data), chunk_size):
                    chunk = transfer.data[offset : offset + chunk_size]
                    await client._write_v2_transfer_chunk(
                        transaction_id=transaction,
                        object_type=int(begin[0]),
                        total_size=encoded.total_bytes,
                        offset=global_offset,
                        data=chunk,
                        timeout=(
                            90.0 if not save_to_slot and global_offset in segment_starts else 12.0
                        ),
                    )
                    global_offset += len(chunk)
                    completed_chunks += 1
                    _report(
                        progress,
                        "upload",
                        completed_chunks,
                        total_chunks,
                        f"Uploading {transfer.step} layer",
                    )
                    if completed_chunks % 8 == 0:
                        await asyncio.sleep(0)

        transfer_seconds = max(0.001, time.perf_counter() - transfer_started)
        if not save_to_slot:
            completion = client._prepare_v2_completion(transaction, CommandV2.TRANSFER_COMMIT)
        # COMMIT transfers ownership to the device.  Do not abort an accepted
        # display merely because its ACK is delayed behind a synchronous panel
        # waveform; the transport verifies status/completion in that case.
        transfer_active = False
        commit, completion_consumed = await client._commit_v2_transfer(
            transaction_id=transaction,
            object_type=int(begin[0]),
            total_size=encoded.total_bytes,
            completion=completion,
            timeout=refresh_timeout,
        )

        if completion is not None:
            _report(progress, "refresh", 0, 1, "Waiting for the e-paper refresh")
            if not completion_consumed:
                await client._await_v2_completion(transaction, completion, refresh_timeout)
            _report(progress, "refresh", 1, 1, "E-paper refresh completed")
            refresh_reply = "picture:done"
            stored_id = None
        else:
            stored_id = commit[0] if commit else None
            if stored_id is None or stored_id == 0xFF:
                raise InkTagError("Device did not return the committed image-slot ID")
            _report(progress, "store", 1, 1, "Image slot committed")
            if metadata_supported:
                _info, items = await client.list_slots()
                effective = next((item for item in items if item.id == stored_id), None)
                if (effective is None or effective.name != safe_name.decode("utf-8")
                        or effective.content_type != content_type):
                    raise InkTagError("Image committed but metadata readback differs; inspect slots, do not re-upload")
            if display_after_store:
                _report(progress, "refresh", 0, 1, "Waiting for the e-paper refresh")
                refresh_reply = await client.show_slot(stored_id, timeout=refresh_timeout)
                _report(progress, "refresh", 1, 1, "E-paper refresh completed")
            else:
                refresh_reply = "not-requested"
            # The backend owns replacement; never delete an extra record here.
            # Fixed-slot in-place replacement may lose the target on interruption.

        return ImageUploadResult(
            stored_id=stored_id,
            replaced_slot=replace_slot,
            bytes_sent=encoded.total_bytes,
            chunks_sent=completed_chunks,
            transfer_seconds=transfer_seconds,
            refresh_reply=refresh_reply,
            wire_encoding="packbits-rle" if use_packbits else "raw",
            encoded_bytes=(sum(len(block.encoded) for block, _ in compressed_blocks)
                           if use_packbits else encoded.total_bytes),
            transfer_write_bytes_no_retries=(
                packbits_transfer_wire_bytes([block for block, _ in compressed_blocks])
                if use_packbits else raw_transfer_wire_bytes(segment_lengths, chunk_size)
            ),
        )
    except BaseException:
        if completion is not None and not completion.done():
            completion.cancel()
        if transfer_active:
            try:
                await client.request_v2(
                    CommandV2.TRANSFER_ABORT,
                    transaction_id=transaction,
                    timeout=5.0,
                )
            except BaseException:
                pass
        raise


def _report(
    callback: ProgressCallback | None,
    stage: str,
    completed: int,
    total: int,
    message: str,
) -> None:
    if callback:
        callback(stage, completed, total, message)
