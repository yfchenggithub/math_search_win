#!/usr/bin/env python3
# -*- coding: utf-8 -*-

"""
generate_activation_code.py

用途：
1. 生成轻授权激活码：
   MSW1.<payload_base64url>.<check8>

2. 可选生成 license.dat 内容，方便本地测试正式版流程

协议约定：
- payload 是 UTF-8 JSON
- 中间段使用 Base64Url（无 '=' padding）
- check8 = 对原始 JSON 文本做 CRC32，输出 8 位大写十六进制

示例：
python generate_activation_code.py ^
  --device ABCD-EFGH-IJKL ^
  --serial LIC-2026-0001 ^
  --watermark WM-0001 ^
  --issued-at 2026-04-20
"""

from __future__ import annotations

import argparse
import base64
import json
import pathlib
import sys
import zlib
from datetime import date, datetime
from typing import Iterable, List


PRODUCT_PREFIX = "MSW1"
PRODUCT_CODE = "msw"
LICENSE_FORMAT = "msw-license-v1"
PRODUCT_NAME = "math_search_win"

# 完整功能名 -> 短码
FEATURE_TO_SHORT = {
    "basic_search_preview": "bsp",
    "full_search": "fs",
    "full_detail": "fd",
    "favorites": "fav",
    "advanced_filter": "af",
}

# 短码 -> 完整功能名
SHORT_TO_FEATURE = {v: k for k, v in FEATURE_TO_SHORT.items()}

DEFAULT_FULL_FEATURES = [
    "basic_search_preview",
    "full_search",
    "full_detail",
    "favorites",
    "advanced_filter",
]


def b64url_encode_no_padding(data: bytes) -> str:
    return base64.urlsafe_b64encode(data).decode("utf-8").rstrip("=")


def crc32_hex_upper(text: str) -> str:
    value = zlib.crc32(text.encode("utf-8")) & 0xFFFFFFFF
    return f"{value:08X}"


def parse_iso_date(value: str, field_name: str) -> date:
    try:
        return datetime.strptime(value, "%Y-%m-%d").date()
    except ValueError as exc:
        raise ValueError(f"{field_name} 格式错误，必须是 YYYY-MM-DD：{value}") from exc


def validate_dates(issued_at: str, expire_at: str) -> None:
    issued_date = parse_iso_date(issued_at, "issued_at")

    # 空字符串表示永久授权
    if not expire_at.strip():
        return

    expire_date = parse_iso_date(expire_at, "expire_at")
    today = date.today()

    if expire_date < today:
        raise ValueError(
            f"expire_at 已过期：{expire_at}，当前日期为 {today.isoformat()}，拒绝生成激活码"
        )

    if expire_date < issued_date:
        raise ValueError(
            f"expire_at 早于 issued_at：issued_at={issued_at}, expire_at={expire_at}"
        )


def validate_business_fields(device: str, serial: str, watermark: str, edition: str) -> None:
    if not device.strip():
        raise ValueError("device 不能为空")
    if not serial.strip():
        raise ValueError("serial 不能为空")
    if not watermark.strip():
        raise ValueError("watermark 不能为空")
    if edition.strip() != "full":
        raise ValueError(f"当前仅支持生成正式版激活码，edition 必须为 full，实际为：{edition}")


def normalize_features(raw_items: Iterable[str]) -> List[str]:
    """
    支持两种输入：
    1. 完整功能名：basic_search_preview
    2. 短码：bsp

    也支持逗号分隔输入。
    """
    result: List[str] = []
    seen = set()

    for item in raw_items:
        if not item:
            continue
        for part in item.split(","):
            token = part.strip()
            if not token:
                continue

            if token in FEATURE_TO_SHORT:
                full_name = token
            elif token in SHORT_TO_FEATURE:
                full_name = SHORT_TO_FEATURE[token]
            else:
                raise ValueError(f"未知功能项: {token}")

            if full_name not in seen:
                seen.add(full_name)
                result.append(full_name)

    return result


def features_to_short_codes(features: Iterable[str]) -> List[str]:
    short_codes: List[str] = []
    for feature in features:
        if feature not in FEATURE_TO_SHORT:
            raise ValueError(f"不支持的功能名: {feature}")
        short_codes.append(FEATURE_TO_SHORT[feature])
    return short_codes


def build_payload(
    *,
    version: int,
    product: str,
    serial: str,
    watermark: str,
    edition: str,
    device: str,
    features: List[str],
    issued_at: str,
    expire_at: str,
) -> dict:
    return {
        "v": version,
        "p": product,
        "s": serial,
        "w": watermark,
        "e": edition,
        "d": device,
        "f": features_to_short_codes(features),
        "iat": issued_at,
        "exp": expire_at,
    }


def compact_json(payload: dict) -> str:
    """
    使用稳定、紧凑的 JSON 文本，确保 CRC32 输入一致。
    """
    return json.dumps(payload, ensure_ascii=False, separators=(",", ":"))


def build_activation_code(payload_json: str) -> str:
    payload_b64 = b64url_encode_no_padding(payload_json.encode("utf-8"))
    check8 = crc32_hex_upper(payload_json)
    return f"{PRODUCT_PREFIX}.{payload_b64}.{check8}"


def build_license_dat(
    *,
    serial: str,
    watermark: str,
    edition: str,
    device: str,
    features: List[str],
    issued_at: str,
    expire_at: str,
    activation_check: str,
    payload_ver: int = 1,
    issuer: str = "offline-manual",
    source: str = "activation_code",
    status: str = "valid",
) -> str:
    lines = [
        f"format={LICENSE_FORMAT}",
        f"product={PRODUCT_NAME}",
        f"serial={serial}",
        f"watermark={watermark}",
        f"edition={edition}",
        f"device={device}",
        f"features={','.join(features)}",
        f"issued_at={issued_at}",
        f"expire_at={expire_at}",
        f"issuer={issuer}",
        f"source={source}",
        f"payload_ver={payload_ver}",
        f"activation_prefix={PRODUCT_PREFIX}",
        f"activation_check={activation_check}",
        f"status={status}",
    ]
    return "\n".join(lines) + "\n"


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(
        description="生成 math_search_win 轻授权激活码，并可输出 license.dat。"
    )
    parser.add_argument("--device", required=True, help="绑定设备码，例如 ABCD-EFGH-IJKL")
    parser.add_argument("--serial", required=True, help="授权编号，例如 LIC-2026-0001")
    parser.add_argument("--watermark", required=True, help="水印编号，例如 WM-0001")
    parser.add_argument("--edition", default="full", help="授权类型，当前仅支持 full")
    parser.add_argument(
        "--issued-at",
        default=str(date.today()),
        help="签发日期，格式 YYYY-MM-DD，默认今天",
    )
    parser.add_argument(
        "--expire-at",
        default="",
        help="到期日期，格式 YYYY-MM-DD；永久授权可留空",
    )
    parser.add_argument(
        "--features",
        nargs="*",
        default=DEFAULT_FULL_FEATURES,
        help=(
            "功能列表，支持完整名或短码；"
            "可写成 --features basic_search_preview full_search "
            "或 --features bsp,fs,fd,fav,af"
        ),
    )
    parser.add_argument(
        "--save-license",
        default="",
        help="可选：将生成的 license.dat 写入指定路径",
    )
    parser.add_argument(
        "--save-code",
        default="",
        help="可选：将激活码写入指定文本文件",
    )
    return parser.parse_args()


def main() -> int:
    args = parse_args()

    try:
        device = args.device.strip()
        serial = args.serial.strip()
        watermark = args.watermark.strip()
        edition = args.edition.strip()
        issued_at = args.issued_at.strip()
        expire_at = args.expire_at.strip()

        normalized_features = normalize_features(args.features)
        validate_business_fields(device, serial, watermark, edition)
        validate_dates(issued_at, expire_at)
    except ValueError as exc:
        print(f"[错误] {exc}", file=sys.stderr)
        return 2

    payload = build_payload(
        version=1,
        product=PRODUCT_CODE,
        serial=serial,
        watermark=watermark,
        edition=edition,
        device=device,
        features=normalized_features,
        issued_at=issued_at,
        expire_at=expire_at,
    )

    payload_json = compact_json(payload)
    activation_code = build_activation_code(payload_json)
    check8 = crc32_hex_upper(payload_json)

    license_dat = build_license_dat(
        serial=serial,
        watermark=watermark,
        edition=edition,
        device=device,
        features=normalized_features,
        issued_at=issued_at,
        expire_at=expire_at,
        activation_check=check8,
        payload_ver=1,
    )

    print("========== Payload JSON ==========")
    print(payload_json)
    print()
    print("========== Activation Code ==========")
    print(activation_code)
    print()
    print("========== license.dat Preview ==========")
    print(license_dat)

    if args.save_license:
        license_path = pathlib.Path(args.save_license)
        license_path.parent.mkdir(parents=True, exist_ok=True)
        license_path.write_text(license_dat, encoding="utf-8")
        print(f"[已写入] license.dat -> {license_path}")

    if args.save_code:
        code_path = pathlib.Path(args.save_code)
        code_path.parent.mkdir(parents=True, exist_ok=True)
        code_path.write_text(activation_code + "\n", encoding="utf-8")
        print(f"[已写入] 激活码 -> {code_path}")

    return 0


if __name__ == "__main__":
    raise SystemExit(main())