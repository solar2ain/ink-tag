"""Frozen screen wire constants; original inputs bound in provenance.json."""
from enum import IntEnum

SCREEN_PROFILE_V2_VERSION = 1

SCREEN_PROFILE_V2_WIRE_SIZE = 17

class ScreenControllerV2(IntEnum):
    UC8XXX = 0x10
    UC8151 = 0x11
    UC8159 = 0x12
    UC8176 = 0x13
    UC8179 = 0x14
    UC8276 = 0x15
    SSD1619 = 0x20
    SSD1675 = 0x21
    SSD1677 = 0x22
    SSD1680 = 0x23
    SSD1683 = 0x24
    SSD1XXX = 0x2F
    JD79661 = 0x30
    JD79665AA = 0x31
    JD79665CA = 0x32
    JD79668AA = 0x33
    JD79653A = 0x34
    IST7513 = 0x40
    IST7601 = 0x41
    JD79XXX = 0x3F

class ScreenColorV2(IntEnum):
    BW = 0x01
    BWR = 0x02
    BWRY = 0x03
    GRAY4 = 0x04
    BWY = 0x05
    BWB = 0x06
    BWG = 0x07
    BWO = 0x08
    GRAY16 = 0x09
    BWRYGB = 0x0A
    BWRYGBO = 0x0B

SCREEN_RULES_V2 = {
    (0x2F, 0x01): {'colors': frozenset((2,)), 'align': 8, 'max': (200, 200), 'fixed_sizes': ((200, 200),), 'color_fixed_sizes': {}, 'transfer_format': 0},
    (0x2F, 0x02): {'colors': frozenset((2,)), 'align': 8, 'max': (200, 200), 'fixed_sizes': ((200, 200),), 'color_fixed_sizes': {}, 'transfer_format': 0, 'full_viewport': True},
    (0x34, 0x01): {'colors': frozenset((2,)), 'align': 8, 'max': (200, 200), 'fixed_sizes': ((200, 200),), 'color_fixed_sizes': {}, 'transfer_format': 0},
    (0x3F, 0x02): {'colors': frozenset((3,)), 'align': 4, 'max': (200, 200), 'fixed_sizes': ((200, 200),), 'color_fixed_sizes': {}, 'transfer_format': 3},
    (0x3F, 0x03): {'colors': frozenset((3,)), 'align': 4, 'max': (152, 296), 'fixed_sizes': ((152, 296),), 'color_fixed_sizes': {}, 'transfer_format': 3},
    (0x3F, 0x04): {'colors': frozenset((3,)), 'align': 4, 'max': (200, 200), 'fixed_sizes': ((200, 200),), 'color_fixed_sizes': {}, 'transfer_format': 3},
    (0x41, 0x01): {'colors': frozenset((10,)), 'align': 2, 'max': (400, 400), 'fixed_sizes': ((400, 400),), 'color_fixed_sizes': {}, 'transfer_format': 5},
    (0x41, 0x02): {'colors': frozenset((10,)), 'align': 2, 'max': (720, 480), 'fixed_sizes': ((720, 480),), 'color_fixed_sizes': {}, 'transfer_format': 5},
    (0x40, 0x01): {'colors': frozenset((10,)), 'align': 2, 'max': (240, 240), 'fixed_sizes': ((240, 240),), 'color_fixed_sizes': {}, 'transfer_format': 5},
    (0x10, 0x00): {'colors': frozenset((1, 2)), 'align': 8, 'max': (800, 600), 'fixed_sizes': (), 'color_fixed_sizes': {}, 'transfer_format': 0},
    (0x10, 0x01): {'colors': frozenset((2,)), 'align': 8, 'max': (128, 250), 'fixed_sizes': ((128, 250),), 'color_fixed_sizes': {}, 'transfer_format': 0},
    (0x11, 0x00): {'colors': frozenset((2,)), 'align': 8, 'max': (152, 296), 'fixed_sizes': ((104, 212), (152, 296)), 'color_fixed_sizes': {}, 'transfer_format': 0},
    (0x11, 0x01): {'colors': frozenset((2,)), 'align': 8, 'max': (104, 212), 'fixed_sizes': ((104, 212),), 'color_fixed_sizes': {}, 'transfer_format': 0},
    (0x11, 0x02): {'colors': frozenset((2,)), 'align': 8, 'max': (152, 296), 'fixed_sizes': ((152, 296),), 'color_fixed_sizes': {}, 'transfer_format': 0},
    (0x12, 0x00): {'colors': frozenset((1, 2)), 'align': 2, 'max': (800, 600), 'fixed_sizes': (), 'color_fixed_sizes': {}, 'transfer_format': 5},
    (0x13, 0x00): {'colors': frozenset((1, 2)), 'align': 8, 'max': (800, 600), 'fixed_sizes': (), 'color_fixed_sizes': {}, 'transfer_format': 0},
    (0x13, 0x01): {'colors': frozenset((2,)), 'align': 8, 'max': (400, 300), 'fixed_sizes': ((400, 300),), 'color_fixed_sizes': {}, 'transfer_format': 0},
    (0x14, 0x00): {'colors': frozenset((1, 2)), 'align': 8, 'max': (960, 680), 'fixed_sizes': (), 'color_fixed_sizes': {}, 'transfer_format': 0},
    (0x15, 0x00): {'colors': frozenset((1, 2)), 'align': 8, 'max': (800, 600), 'fixed_sizes': (), 'color_fixed_sizes': {}, 'transfer_format': 0},
    (0x15, 0x01): {'colors': frozenset((2,)), 'align': 8, 'max': (400, 300), 'fixed_sizes': ((400, 300),), 'color_fixed_sizes': {}, 'transfer_format': 0},
    (0x20, 0x00): {'colors': frozenset((1, 2)), 'align': 8, 'max': (400, 300), 'fixed_sizes': (), 'color_fixed_sizes': {}, 'transfer_format': 0},
    (0x20, 0x01): {'colors': frozenset((2,)), 'align': 8, 'max': (128, 250), 'fixed_sizes': ((128, 250),), 'color_fixed_sizes': {}, 'transfer_format': 0},
    (0x21, 0x00): {'colors': frozenset((1, 2)), 'align': 8, 'max': (152, 152), 'fixed_sizes': ((152, 152),), 'color_fixed_sizes': {}, 'transfer_format': 0},
    (0x21, 0x01): {'colors': frozenset((2,)), 'align': 8, 'max': (152, 152), 'fixed_sizes': ((152, 152),), 'color_fixed_sizes': {}, 'transfer_format': 0},
    (0x22, 0x00): {'colors': frozenset((1, 2, 4)), 'align': 8, 'max': (960, 680), 'fixed_sizes': (), 'color_fixed_sizes': {4: ((800, 480),)}, 'transfer_format': 0},
    (0x23, 0x00): {'colors': frozenset((1, 2)), 'align': 8, 'max': (128, 250), 'fixed_sizes': ((128, 250),), 'color_fixed_sizes': {}, 'transfer_format': 0},
    (0x23, 0x01): {'colors': frozenset((1, 2)), 'align': 8, 'max': (128, 250), 'fixed_sizes': ((128, 250),), 'color_fixed_sizes': {}, 'transfer_format': 0},
    (0x24, 0x00): {'colors': frozenset((1, 2)), 'align': 8, 'max': (400, 300), 'fixed_sizes': (), 'color_fixed_sizes': {}, 'transfer_format': 0},
    (0x24, 0x01): {'colors': frozenset((1, 2)), 'align': 8, 'max': (400, 300), 'fixed_sizes': (), 'color_fixed_sizes': {}, 'transfer_format': 0},
    (0x30, 0x00): {'colors': frozenset((3,)), 'align': 4, 'max': (400, 300), 'fixed_sizes': (), 'color_fixed_sizes': {}, 'transfer_format': 3},
    (0x30, 0x01): {'colors': frozenset((3,)), 'align': 4, 'max': (128, 250), 'fixed_sizes': ((128, 250),), 'color_fixed_sizes': {}, 'transfer_format': 3},
    (0x31, 0x00): {'colors': frozenset((3,)), 'align': 4, 'max': (800, 600), 'fixed_sizes': (), 'color_fixed_sizes': {}, 'transfer_format': 3},
    (0x31, 0x01): {'colors': frozenset((3,)), 'align': 4, 'max': (768, 552), 'fixed_sizes': ((768, 552),), 'color_fixed_sizes': {}, 'transfer_format': 3},
    (0x31, 0x02): {'colors': frozenset((3,)), 'align': 4, 'max': (768, 552), 'fixed_sizes': ((768, 552),), 'color_fixed_sizes': {}, 'transfer_format': 3},
    (0x32, 0x01): {'colors': frozenset((10,)), 'align': 4, 'max': (760, 568), 'fixed_sizes': ((760, 568),), 'color_fixed_sizes': {}, 'transfer_format': 6},
    (0x33, 0x00): {'colors': frozenset((2, 3)), 'align': 4, 'max': (400, 300), 'fixed_sizes': (), 'color_fixed_sizes': {}, 'transfer_format': 3},
    (0x33, 0x01): {'colors': frozenset((3,)), 'align': 4, 'max': (400, 300), 'fixed_sizes': ((400, 300),), 'color_fixed_sizes': {}, 'transfer_format': 3},
    (0x33, 0x02): {'colors': frozenset((3,)), 'align': 4, 'max': (400, 300), 'fixed_sizes': ((400, 300),), 'color_fixed_sizes': {}, 'transfer_format': 3},
    (0x3F, 0x00): {'colors': frozenset((2, 3)), 'align': 4, 'max': (800, 600), 'fixed_sizes': (), 'color_fixed_sizes': {}, 'transfer_format': 3},
    (0x3F, 0x01): {'colors': frozenset((2, 3)), 'align': 4, 'max': (128, 250), 'fixed_sizes': ((128, 250),), 'color_fixed_sizes': {}, 'transfer_format': 3},
    (0x3F, 0x05): {'colors': frozenset((3,)), 'align': 4, 'max': (176, 480), 'fixed_sizes': ((176, 480),), 'color_fixed_sizes': {}, 'transfer_format': 3},
}
