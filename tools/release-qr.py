#!/usr/bin/env python3
"""Writes docs/install-qr.svg: a QR code that FBI scans (Remote Install ->
Scan QR Code) to install the newest release straight from GitHub. The link
always points to the latest release, so the code only changes if the
repository moves."""
import importlib.util
import os

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
URL = "https://github.com/tj537/Wiggle3DS/releases/latest/download/Wiggle3DS.cia"

spec = importlib.util.spec_from_file_location("send_cia", os.path.join(ROOT, "tools", "send-cia.py"))
send_cia = importlib.util.module_from_spec(spec)
spec.loader.exec_module(send_cia)

out = os.path.join(ROOT, "docs", "install-qr.svg")
send_cia.write_svg(send_cia.qr_rows(URL), out, scale=8)
print(f"{out}: {URL}")
