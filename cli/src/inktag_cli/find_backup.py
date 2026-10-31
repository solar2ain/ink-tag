"""Password-encrypted, cross-machine Find identities. No Apple session material."""
from __future__ import annotations

import base64
from datetime import datetime
import hashlib
import json
import math
import re
import secrets

from cryptography.exceptions import InvalidTag
from cryptography.hazmat.primitives.ciphers.aead import AESGCM

from . import find_public_only

PREFIX = "IKTFIND1."
AAD = b"InkTag/Find/portable-backup/v1"
ITERATIONS = 600_000
MAX_PLAIN_BYTES = 8 * 1024 * 1024
MAX_TEXT_CHARS = 12 * 1024 * 1024
HTTP_LIMIT = 16 * 1024 * 1024
MAX_POINTS = 100_000


def password_bytes(password: str) -> bytes:
    if not isinstance(password, str) or not 6 <= len(password) <= 512:
        raise ValueError("迁移口令需要6–512个字符；口令不会保存，遗忘后无法恢复。")
    try:
        return password.encode("utf-8")
    except UnicodeError as error:
        raise ValueError("迁移口令包含无效字符。") from error


def encrypt_backup(payload: dict, password: str) -> str:
    plain = json.dumps(payload, ensure_ascii=False, separators=(",", ":"), allow_nan=False).encode("utf-8")
    if len(plain) > MAX_PLAIN_BYTES:
        raise ValueError("Find备份超过8MiB，请取消‘包含历史轨迹’后先备份标签。")
    salt, nonce = secrets.token_bytes(16), secrets.token_bytes(12)
    key = hashlib.pbkdf2_hmac("sha256", password_bytes(password), salt, ITERATIONS, 32)
    packed = b"\x01" + salt + nonce + AESGCM(key).encrypt(nonce, plain, AAD)
    return PREFIX + base64.urlsafe_b64encode(packed).rstrip(b"=").decode("ascii")


def _unique_object(pairs):
    result = {}
    for key, value in pairs:
        if key in result:
            raise ValueError("备份中存在重复字段。")
        result[key] = value
    return result


def plain_backup(payload: dict) -> str:
    text = json.dumps(validate_payload(payload), ensure_ascii=False, separators=(",", ":"), allow_nan=False)
    if len(text.encode('utf-8')) > MAX_PLAIN_BYTES:
        raise ValueError("Find备份超过8MiB。")
    return text


def read_backup(text: str, password: str, allow_plaintext: bool = False) -> dict:
    if isinstance(text, str) and text.lstrip().startswith('{'):
        if allow_plaintext is not True:
            raise ValueError("此文件未加密，请先确认隐私泄露风险。")
        if len(text) > MAX_TEXT_CHARS or len(text.encode('utf-8')) > MAX_PLAIN_BYTES:
            raise ValueError("Find备份超过8MiB。")
        try:
            return validate_payload(json.loads(text, object_pairs_hook=_unique_object))
        except (ValueError, RecursionError) as error:
            raise ValueError("未加密的Find备份格式无效。") from error
    return decrypt_backup(text, password)


def decrypt_backup(text: str, password: str) -> dict:
    secret = password_bytes(password)
    if not isinstance(text, str) or len(text) > MAX_TEXT_CHARS:
        raise ValueError("迁移资料过大或格式无效。")
    text = text.strip()
    if not re.fullmatch(r"IKTFIND1\.[A-Za-z0-9_-]+", text):
        raise ValueError("请提供完整的InkTag加密迁移字符串。")
    try:
        encoded = text[len(PREFIX):]
        raw = base64.b64decode(encoded + "=" * (-len(encoded) % 4), altchars=b"-_", validate=True)
        if (len(raw) < 46 or len(raw) > MAX_PLAIN_BYTES + 45 or raw[0] != 1 or
                base64.urlsafe_b64encode(raw).rstrip(b"=").decode() != encoded):
            raise ValueError("invalid envelope")
        salt, nonce = raw[1:17], raw[17:29]
        key = hashlib.pbkdf2_hmac("sha256", secret, salt, ITERATIONS, 32)
        plain = AESGCM(key).decrypt(nonce, raw[29:], AAD)
        payload = json.loads(plain, object_pairs_hook=_unique_object)
    except (ValueError, InvalidTag, UnicodeError, RecursionError) as error:
        raise ValueError("迁移口令不正确，或迁移资料已损坏。") from error
    return validate_payload(payload)


def _text(value, maximum=128):
    if not isinstance(value, str) or len(value) > maximum or any(ord(c) < 32 for c in value):
        raise ValueError("备份标签信息无效。")
    return value


def _timestamp(value):
    value = _text(value, 64)
    try:
        parsed = datetime.fromisoformat(value.replace("Z", "+00:00"))
        if parsed.tzinfo is None:
            raise ValueError()
    except ValueError as error:
        raise ValueError("备份时间格式无效。") from error
    return value


def validate_payload(payload: dict) -> dict:
    if (not isinstance(payload, dict) or payload.get("format") != "inktag-find-backup" or
            type(payload.get("version")) is not int or payload["version"] != 1 or
            not isinstance(payload.get("identities"), list) or not 1 <= len(payload["identities"]) <= 256):
        raise ValueError("Find备份版本或标签数量无效。")
    identities, fingerprints, points = [], set(), 0
    for entry in payload["identities"]:
        if not isinstance(entry, dict):
            raise ValueError("Find备份身份材料无效。")
        history_only = entry.get("historyOnly", False)
        if type(history_only) is not bool:
            raise ValueError("Find备份资料类型无效。")
        if history_only:
            if "materialHex" in entry or not re.fullmatch(r"[0-9a-f]{32}", str(entry.get("fingerprint", ""))):
                raise ValueError("仅轨迹资料不能包含查询密钥。")
            fingerprint = entry["fingerprint"]
        else:
            if not re.fullmatch(r"[0-9A-Fa-f]{88}", str(entry.get("materialHex", ""))):
                raise ValueError("Find备份身份材料无效。")
            material = bytes.fromhex(entry["materialHex"])
            if material[28:] in (bytes(16), b"\xff" * 16):
                raise ValueError("Find备份身份材料无效。")
            public = find_public_only.master_public_bytes(int.from_bytes(material[:28], "big"))
            fingerprint = find_public_only.identity_fingerprint(public, material[28:])
        if (entry.get("fingerprint") != fingerprint or fingerprint in fingerprints or
                type(entry.get("derivationVersion")) is not int or entry["derivationVersion"] != 2 or
                type(entry.get("periodSeconds")) is not int or entry["periodSeconds"] != 900):
            raise ValueError("Find备份身份指纹、派生参数或重复标签无效。")
        fingerprints.add(fingerprint)
        normalized = {name: _text(entry.get(name, ""), 128 if name == "deviceId" else 64) for name in
                      ("deviceId", "deviceName", "bluetoothName", "findNote")}
        normalized["findNote"] = normalized["findNote"] or normalized["deviceName"] or normalized["bluetoothName"] or "InkTag"
        normalized.update(fingerprint=fingerprint, derivationVersion=2,
                          periodSeconds=900, createdAt=_timestamp(entry.get("createdAt")), tracks=[])
        if history_only:
            normalized["historyOnly"] = True
        else:
            normalized["materialHex"] = material.hex()
        tracks = entry.get("tracks", [])
        if not isinstance(tracks, list):
            raise ValueError("备份轨迹无效。")
        points += len(tracks)
        if points > MAX_POINTS:
            raise ValueError("备份轨迹数量过多。")
        for point in tracks:
            if not isinstance(point, dict):
                raise ValueError("备份轨迹无效。")
            row = {"timestamp": _timestamp(point.get("timestamp")), "receivedAt": _timestamp(point.get("receivedAt"))}
            for name, low, high in (("latitude", -90, 90), ("longitude", -180, 180),
                                    ("horizontalAccuracy", 0, 1e9)):
                value = point.get(name)
                if type(value) not in (float, int) or not math.isfinite(value) or not low <= value <= high:
                    raise ValueError("备份轨迹坐标无效。")
                row[name] = value
            for name in ("confidence", "status", "sourceSlot"):
                value = point.get(name, -1 if name == "sourceSlot" else 0)
                if type(value) is not int or not -1 <= value <= 2**53 - 1:
                    raise ValueError("备份轨迹状态无效。")
                row[name] = value
            normalized["tracks"].append(row)
        identities.append(normalized)
    selected = payload.get("selectedFingerprint", "")
    if not isinstance(selected, str) or (selected and selected not in fingerprints):
        raise ValueError("备份选中标签无效。")
    return {"format": "inktag-find-backup", "version": 1, "identities": identities,
            "selectedFingerprint": selected}


def library_payload(session, include_history: bool = True, fingerprints=None, include_keys: bool = True) -> dict:
    if type(include_history) is not bool or type(include_keys) is not bool or not (include_history or include_keys):
        raise ValueError("请至少选择设备资料或历史轨迹。")
    if fingerprints is None:
        selected_fps = list(session.identities)
    elif (not isinstance(fingerprints, list) or not 1 <= len(fingerprints) <= 256 or
          any(not isinstance(fp, str) or fp not in session.identities for fp in fingerprints) or
          len(set(fingerprints)) != len(fingerprints)):
        raise ValueError("请选择有效的设备。")
    else:
        selected_fps = fingerprints
    if not selected_fps:
        raise ValueError("没有可导出的设备。")
    if session.identity_database.execute("SELECT 1 FROM find_owner_rotations LIMIT 1").fetchone():
        raise ValueError("有尚未确认的Find登记，请先完成或恢复登记，再导出全部标签。")
    entries = []
    for fingerprint in selected_fps:
        entry = session.identities[fingerprint]
        if entry["derivationVersion"] != 2:
            raise ValueError("身份库含旧格式标签，当前无法完整导出；请先处理旧标签。")
        entries.append({name: entry[name] for name in ("deviceId", "deviceName", "bluetoothName", "findNote", "createdAt")})
        entries[-1].update(fingerprint=fingerprint, derivationVersion=2, periodSeconds=900, tracks=[])
        if include_keys and not entry.get("historyOnly"):
            entries[-1]["materialHex"] = session.identity_seeds[fingerprint].hex()
        else:
            entries[-1]["historyOnly"] = True
    by_fp = {e["fingerprint"]: e for e in entries}
    rows = session.identity_database.execute("""SELECT key_fingerprint, report_timestamp, received_at,
        latitude, longitude, horizontal_accuracy, confidence, status, source_slot
        FROM find_track_points WHERE key_fingerprint IN (%s) ORDER BY id LIMIT ?""" %
        ','.join('?' for _ in selected_fps), (*selected_fps, MAX_POINTS + 1)).fetchall() if include_history else []
    if len(rows) > MAX_POINTS:
        raise ValueError("历史轨迹过多，请减少导出设备数量或分别导出设备资料与轨迹；未截断任何记录。")
    for fp, *values in rows:
        if fp in by_fp:
            by_fp[fp]["tracks"].append(dict(zip(("timestamp", "receivedAt", "latitude", "longitude",
                                                "horizontalAccuracy", "confidence", "status", "sourceSlot"), values)))
    return validate_payload({"format": "inktag-find-backup", "version": 1, "identities": entries,
                             "selectedFingerprint": session.selected_key_fingerprint if session.selected_key_fingerprint in by_fp else selected_fps[0]})


def import_library(session, payload: dict) -> dict:
    """Validate/prepare the entire batch before one SQL transaction; no overwrite."""
    payload = validate_payload(payload)
    entries = payload["identities"]
    additions = [e for e in entries if e["fingerprint"] not in session.identities]
    if len(session.identities) + len(additions) > 256:
        raise ValueError("导入后标签数将超过256条上限。")
    for e in entries:
        fp = e["fingerprint"]
        if fp in session.identities:
            if (session.identities[fp]["deviceId"].lower() != e["deviceId"].lower() or
                    (not e.get("historyOnly") and fp in session.identity_seeds and
                     session.identity_seeds[fp] != bytes.fromhex(e["materialHex"]))):
                raise ValueError("已有标签的身份资料冲突，未导入任何内容。")
    upgrades = [e for e in entries if e["fingerprint"] in session.identities and
                session.identities[e["fingerprint"]].get("historyOnly") and not e.get("historyOnly")]
    protected = {e["fingerprint"]: (b'' if e.get("historyOnly") else
                 session._protect_seed(bytes.fromhex(e["materialHex"]))) for e in additions + upgrades}
    selected = session.selected_key_fingerprint or payload["selectedFingerprint"] or entries[0]["fingerprint"]
    track_count = 0
    with session.identity_database:
        for e in additions:
            session.identity_database.execute("""INSERT INTO find_identities
                (key_fingerprint,label,device_id,device_name,bluetooth_name,find_note,tracking_enabled,
                 derivation_version,derivation_seed,created_at,period_seconds,history_only) VALUES (?,?,?,?,?,?,0,2,?,?,900,?)""",
                (e["fingerprint"], e["findNote"], e["deviceId"], e["deviceName"], e["bluetoothName"],
                 e["findNote"], protected[e["fingerprint"]], e["createdAt"], int(e.get("historyOnly", False))))
        for e in upgrades:
            session.identity_database.execute("""UPDATE find_identities SET history_only=0,
                derivation_seed=?, tracking_enabled=0 WHERE key_fingerprint=?""",
                (protected[e["fingerprint"]], e["fingerprint"]))
        for e in entries:
            for t in e["tracks"]:
                cursor = session.identity_database.execute("""INSERT OR IGNORE INTO find_track_points
                    (key_fingerprint,report_timestamp,received_at,latitude,longitude,horizontal_accuracy,
                     confidence,status,source_slot) VALUES (?,?,?,?,?,?,?,?,?)""",
                    (e["fingerprint"], t["timestamp"], t["receivedAt"], t["latitude"], t["longitude"],
                     t["horizontalAccuracy"], t["confidence"], t["status"], t["sourceSlot"]))
                track_count += cursor.rowcount
        session.identity_database.execute("""INSERT INTO find_settings(name,value) VALUES ('selectedKeyFingerprint',?)
            ON CONFLICT(name) DO UPDATE SET value=excluded.value""", (selected,))
    for e in additions:
        fp = e["fingerprint"]
        session.identities[fp] = {k: e[k] for k in ("deviceId", "deviceName", "bluetoothName", "findNote", "createdAt")}
        session.identities[fp].update(keyFingerprint=fp, derivationVersion=2, periodSeconds=900,
            trackingEnabled=False, lastAutoQueryAt=None, lastAutoError=None, lastQueriedAt=None, lastReportAt=None)
        session.identities[fp]["historyOnly"] = e.get("historyOnly", False)
        if not e.get("historyOnly"):
            session.identity_seeds[fp] = bytes.fromhex(e["materialHex"])
    for e in upgrades:
        fp = e["fingerprint"]
        session.identities[fp].update(historyOnly=False, trackingEnabled=False)
        session.identity_seeds[fp] = bytes.fromhex(e["materialHex"])
    session.selected_key_fingerprint = selected
    session._secure_identity_database_files()
    return {"importedCount": len(additions), "existingCount": len(entries) - len(additions),
            "importedTrackCount": track_count}
