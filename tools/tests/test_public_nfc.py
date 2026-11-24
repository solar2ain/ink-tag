from pathlib import Path
import struct
import sys
from types import SimpleNamespace

from PIL import Image
import pytest
import serial

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / 'tools/hardware'))
import inktag_nfc as nfc
import inktag_nfc_ndef as ndef
import inktag_nfc_reader as reader
from ink_nfc_fmsc import pn532


class MockReader:
    uid = bytes.fromhex('040123456789AB')

    def __init__(self, kind, response_uid=None):
        self.kind = kind
        self.response_uid = response_uid or self.uid
        self.grouped = False
        self.sent = []
        self.closed = False

    def connect(self):
        pass

    def exchange(self, packet):
        self.sent.append(packet)
        if packet == nfc.D1:
            flags = b'\xe2\x02\x01\x07' if self.kind == 'type2-sram' else b''
            geometry = struct.pack('>6H3B', 8, 8, 0, 0, 8, 8, 1, 1, 0)
            return (b'\xc0\x04IKT1\xc1\x07' + self.response_uid +
                    b'\xe0\x0f' + geometry + b'\xd1\x07\x01\0\0\0\0\0\0' + flags + b'\x90\0')
        if packet == nfc.DE:
            return b'\0\x90\0'
        return b'\x90\0'

    def set_streamed(self, enabled):
        pass

    def enable_compact(self):
        pass

    def enable_grouped(self):
        assert self.kind == 'type2-sram'
        self.grouped = True

    def exchange_group(self, commands):
        self.sent.extend(commands)

    def set_wait(self, seconds):
        pass

    def close(self):
        self.closed = True


@pytest.mark.parametrize('kind', ['type2-sram', 'iso-dep'])
def test_public_image_transfer_uses_both_transports(tmp_path, monkeypatch, capsys, kind):
    path = tmp_path / 'image.png'
    Image.new('RGB', (8, 8), 'white').save(path)
    link = MockReader(kind)
    monkeypatch.setattr(reader, 'Link', lambda args: link)
    assert nfc.main(['send-image', str(path), '--port', 'MOCK', '--wait-refresh']) == 0
    assert link.sent[0] == nfc.D1
    assert any(packet[:2] == b'\xf0\xd3' for packet in link.sent)
    assert link.sent[-2:] == [nfc.D4, nfc.DE]
    assert link.closed
    assert '刷新完成' in capsys.readouterr().out
    assert list(tmp_path.iterdir()) == [path]


def test_wrong_identity_blocks_image_and_refresh(tmp_path, monkeypatch):
    path = tmp_path / 'image.png'
    Image.new('RGB', (8, 8), 'white').save(path)
    link = MockReader('type2-sram', bytes(7))
    monkeypatch.setattr(reader, 'Link', lambda args: link)
    assert nfc.main(['send-image', str(path), '--port', 'MOCK']) == 1
    assert link.sent == [nfc.D1]
    assert link.closed


@pytest.mark.parametrize('kind', ['type2-sram', 'iso-dep'])
def test_image_name_sent_as_plain_text(monkeypatch, kind):
    link = MockReader(kind)
    captured = []

    class Ndef:
        def __init__(self, selected):
            assert selected is link

        def command(self, record):
            captured.extend(ndef.decode_records(record))

    monkeypatch.setattr(reader, 'Link', lambda args: link)
    monkeypatch.setattr(ndef, 'Type4Ndef', Ndef)
    monkeypatch.setattr(ndef, 'type2_command', lambda selected, record: Ndef(selected).command(record))
    assert nfc.main(['send-cmd', '日落', '--port', 'MOCK']) == 0
    assert captured[0]['text'] == '日落'


@pytest.mark.parametrize('options', [
    ['send-test-image'], ['send-image', 'image.png', '--gt-mode', '40'],
    ['read', '--log-dir', 'logs'], ['read', '--iso-max-inf-bytes', '30'],
])
def test_public_parser_excludes_debug_controls(options):
    with pytest.raises(SystemExit) as error:
        nfc.parser().parse_args(options)
    assert error.value.code == 2


@pytest.mark.parametrize(('sak', 'expected'), [(0, 'type2-sram'), (0x20, 'iso-dep')])
def test_link_selects_either_tag_without_a_write_on_read(monkeypatch, sak, expected):
    operations = []
    selected = b'\1\1\0\0' + bytes((sak, len(MockReader.uid))) + MockReader.uid
    if sak:
        selected += b'\1'

    class Serial:
        def __init__(self, *args, **kwargs):
            pass

        def open(self):
            pass

        def write(self, _):
            pass

        def flush(self):
            pass

        def close(self):
            pass

    class Reader:
        def __init__(self, *args, **kwargs):
            self.rf_on = False

        def open(self):
            pass

        def command(self, message, response, *, name):
            operations.append(name)
            return selected if name == 'InListPassiveTarget' else b''

    monkeypatch.setattr(serial, 'Serial', Serial)
    monkeypatch.setattr(pn532, 'Pn532', Reader)
    monkeypatch.setattr(reader.time, 'sleep', lambda seconds: None)
    link = reader.Link(SimpleNamespace(port='MOCK', uid=MockReader.uid, command='read'))
    link.connect()
    assert link.kind == expected and link.uid == MockReader.uid
    assert operations == ['SetParameters AutoRATS', 'InListPassiveTarget']
