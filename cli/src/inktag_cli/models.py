from __future__ import annotations

from dataclasses import asdict, dataclass, field
from typing import Any


@dataclass(frozen=True)
class ScreenConfig:
    schema_version: int
    driver: int
    color: int
    transform: int
    width: int
    height: int
    visible_x: int
    visible_y: int
    visible_width: int
    visible_height: int
    recipe: int = 0
    native_ui_scale: int = 0

    def to_dict(self) -> dict[str, Any]:
        result = asdict(self)
        result["driverHex"] = f"0x{self.driver:02X}"
        result["recipeHex"] = f"0x{self.recipe:02X}"
        return result


@dataclass(frozen=True)
class SettingsCapabilities:
    protocol_version: int = 0
    features: int = 0
    bluetooth_name_max_bytes: int = 0
    device_label_max_bytes: int = 0
    nfc_action_mask: int = 0
    button_action_mask: int = 0
    button_gesture_mask: int = 0
    display_action_mask: int = 0
    device_info_layout_mask: int = 0

    @property
    def bluetooth_name(self) -> bool:
        return bool(self.features & 0x01)

    @property
    def native_device_info(self) -> bool:
        return bool(self.features & 0x04)

    @property
    def device_label(self) -> bool:
        return bool(self.features & 0x80)

    @property
    def device_info_qr(self) -> bool:
        return bool(self.features & 0x08)

    @property
    def custom_button_gpio(self) -> bool:
        return bool(self.features & 0x10)

    @property
    def custom_buzzer_gpio(self) -> bool:
        return bool(self.features & 0x20)

    @property
    def status_led_settings(self) -> bool:
        return bool(self.features & 0x40)

    def to_dict(self) -> dict[str, Any]:
        return {
            "protocolVersion": self.protocol_version,
            "features": self.features,
            "bluetoothName": self.bluetooth_name,
            "bluetoothNameMaxBytes": self.bluetooth_name_max_bytes,
            "deviceLabel": self.device_label,
            "deviceLabelMaxBytes": self.device_label_max_bytes,
            "nfcActionMask": self.nfc_action_mask,
            "buttonActionMask": self.button_action_mask,
            "buttonGestureMask": self.button_gesture_mask,
            "displayActionMask": self.display_action_mask,
            "nativeDeviceInfo": self.native_device_info,
            "deviceInfoQr": self.device_info_qr,
            "deviceInfoLayoutMask": self.device_info_layout_mask,
            "customButtonGpio": self.custom_button_gpio,
            "customBuzzerGpio": self.custom_buzzer_gpio,
            "statusLedSettings": self.status_led_settings,
        }


@dataclass(frozen=True)
class ImageStoreInfo:
    width: int = 0
    height: int = 0
    bytes_per_image: int = 0
    required_sectors: int = 0
    total_sectors: int = 0
    free_sectors: int = 0
    largest_free_run: int = 0
    fit: int = 0
    contiguous_fit: int = 0
    saved: int = 0
    compatible: int = 0
    playable: int = 0
    incompatible: int = 0
    slideshow: bool = False
    interval_seconds: int = 0
    fixed_slot_count: int | None = None
    occupied_ids: tuple[int, ...] | None = None

    def to_dict(self) -> dict[str, Any]:
        return asdict(self)

@dataclass(frozen=True)
class ImageStoreItem:
    id: int
    name: str
    width: int
    height: int
    bytes: int
    format: int
    segments: int
    created_at: int
    compatible: bool
    content_type: str

    def to_dict(self) -> dict[str, Any]:
        return asdict(self)


@dataclass(frozen=True)
class ActivationStatus:
    state: str = "unsupported"
    activation_id: str = ""
    scheme: int = 0
    last_result: int = 0
    result_name: str = "ok"
    code_length: int = 0
    key_id: int = 0
    sequence: int = 0
    feature_mask: int = 0
    request_active: bool = False
    active: bool = False

    def to_dict(self) -> dict[str, Any]:
        return {
            "state": self.state,
            "edition": "advanced" if self.active else "free",
            "activationId": self.activation_id,
            "scheme": self.scheme,
            "lastResult": self.result_name,
            "codeLength": self.code_length,
            "keyId": self.key_id,
            "sequence": self.sequence,
            "featureMask": f"0x{self.feature_mask:08X}",
            "requestActive": self.request_active,
            "permanent": self.active,
        }


@dataclass(frozen=True)
class ActivationRequest:
    scheme: int
    key_id: int
    product_id: int
    security_epoch: int
    supported_features: int
    installed_sequence: int
    device_id: str
    challenge: str
    request_code: str

    def to_dict(self) -> dict[str, Any]:
        return {
            "format": "IKAC1",
            "requestCode": self.request_code,
            "deviceId": self.device_id,
            "challenge": self.challenge,
            "scheme": self.scheme,
            "keyId": self.key_id,
            "productId": self.product_id,
            "securityEpoch": self.security_epoch,
            "supportedFeatures": f"0x{self.supported_features:08X}",
            "installedSequence": self.installed_sequence,
        }


@dataclass
class DeviceSnapshot:
    name: str = ""
    address: str = ""
    protocol_version: int | None = None
    firmware_version: str = ""
    board_id: str = ""
    profile: str = ""
    device_name: str = ""
    device_label: str = ""
    ble_address: str = ""
    voltage: float | None = None
    temperature: int | None = None
    mcu_temperature: float | None = None
    panel_temperature: float | None = None
    application_version: int | None = None
    application_size: int | None = None
    application_checksum: int | None = None
    swap_state: int | None = None
    swap_generation: int | None = None
    screen: ScreenConfig | None = None
    settings_capabilities: SettingsCapabilities = field(default_factory=SettingsCapabilities)
    activation: ActivationStatus = field(default_factory=ActivationStatus)
    services: list[str] = field(default_factory=list)
    messages: list[str] = field(default_factory=list)
    password_diagnostic: str | None = None
    screen_diagnostic: str | None = None

    def to_dict(self) -> dict[str, Any]:
        return {
            "name": self.name,
            "address": self.address,
            "deviceName": self.device_label or self.device_name or self.name,
            "bluetoothName": self.device_name or self.name,
            "bleAddress": self.ble_address,
            "boardId": self.board_id,
            "profile": self.profile,
            "firmwareVersion": self.firmware_version,
            "protocolVersion": self.protocol_version,
            "voltage": self.voltage,
            "temperature": self.temperature,
            "mcuTemperature": self.mcu_temperature,
            "panelTemperature": self.panel_temperature,
            "applicationVersion": self.application_version,
            "applicationSize": self.application_size,
            "applicationChecksum": (
                f"0x{self.application_checksum:08X}"
                if self.application_checksum is not None else None
            ),
            "swapState": self.swap_state,
            "swapGeneration": self.swap_generation,
            "screen": self.screen.to_dict() if self.screen else None,
            "settingsCapabilities": self.settings_capabilities.to_dict(),
            "activation": self.activation.to_dict(),
            "services": self.services,
            **({"passwordDiagnostic": self.password_diagnostic} if self.password_diagnostic else {}),
            **({"screenDiagnostic": self.screen_diagnostic} if self.screen_diagnostic else {}),
        }


@dataclass(frozen=True)
class DiscoveredDevice:
    name: str
    address: str
    rssi: int | None
    services: tuple[str, ...]

    def to_dict(self) -> dict[str, Any]:
        return asdict(self)
