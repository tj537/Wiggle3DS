#!/usr/bin/env python3
"""Serves a CIA from this Mac over the LAN so FBI can install it over WiFi.

    python3 tools/send-cia.py Wiggle3DS.cia               # QR code for FBI: Remote Install -> Scan QR Code
    python3 tools/send-cia.py Wiggle3DS.cia --push 3DS_IP # FBI: Remote Install -> Receive URLs over the network

The server stops after the 3DS has downloaded the whole file (or on Ctrl+C).
"""
import argparse
import http.server
import os
import socket
import struct
import subprocess
import sys
import threading
import urllib.parse

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))


def lan_ip():
	# Connecting a UDP socket sends nothing; it only picks the interface with the default route.
	try:
		with socket.socket(socket.AF_INET, socket.SOCK_DGRAM) as s:
			s.connect(("10.255.255.255", 1))
			ip = s.getsockname()[0]
			if not ip.startswith("127."):
				return ip
	except OSError:
		pass
	for iface in ("en0", "en1"):
		ip = subprocess.run(["ipconfig", "getifaddr", iface], capture_output=True, text=True).stdout.strip()
		if ip:
			return ip
	sys.exit("No LAN IP found. Is WiFi on?")


def qr_rows(text):
	"""QR modules as rows of booleans, via tools/qrtext.c built with the app's qrcodegen."""
	exe = os.path.join(ROOT, "build", "qrtext")
	src = [os.path.join(ROOT, "tools", "qrtext.c"), os.path.join(ROOT, "source", "qrcodegen.c")]
	if not os.path.exists(exe) or any(os.path.getmtime(f) > os.path.getmtime(exe) for f in src):
		os.makedirs(os.path.dirname(exe), exist_ok=True)
		subprocess.run(["cc", "-O2", "-I", os.path.join(ROOT, "source"), "-o", exe] + src, check=True)
	out = subprocess.run([exe, text], capture_output=True, text=True, check=True).stdout
	return [[c == "#" for c in line] for line in out.splitlines()]


def print_qr(rows, quiet=2):
	# Two modules per character cell (upper half block), with explicit colours so it
	# scans in dark terminal themes too.
	n = len(rows) + 2 * quiet
	grid = [[False] * n for _ in range(quiet)] + \
		[[False] * quiet + r + [False] * quiet for r in rows] + \
		[[False] * n for _ in range(quiet + 1)]
	col = lambda dark: 0 if dark else 255
	for y in range(0, n, 2):
		line = "".join(f"\x1b[38;5;{col(grid[y][x])};48;5;{col(grid[y + 1][x])}m▀" for x in range(n))
		print("  " + line + "\x1b[0m")


def write_svg(rows, path, scale=12, quiet=4):
	n = len(rows) + 2 * quiet
	rects = "".join(f'<rect x="{x + quiet}" y="{y + quiet}" width="1" height="1"/>'
	                for y, r in enumerate(rows) for x, dark in enumerate(r) if dark)
	with open(path, "w") as f:
		f.write(f'<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 {n} {n}" width="{n * scale}" height="{n * scale}" '
		        f'shape-rendering="crispEdges"><rect width="{n}" height="{n}" fill="#fff"/><g fill="#000">{rects}</g></svg>')


class Handler(http.server.BaseHTTPRequestHandler):
	cia = ""
	done = threading.Event()

	def do_GET(self):
		if urllib.parse.unquote(self.path) != "/" + os.path.basename(self.cia):
			self.send_error(404)
			return
		size = os.path.getsize(self.cia)
		self.send_response(200)
		self.send_header("Content-Type", "application/octet-stream")
		self.send_header("Content-Length", str(size))
		self.end_headers()
		sent = 0
		with open(self.cia, "rb") as f:
			while chunk := f.read(64 * 1024):
				try:
					self.wfile.write(chunk)
				except (BrokenPipeError, ConnectionResetError):
					break
				sent += len(chunk)
				print(f"\r  {self.client_address[0]}: {sent * 100 // size:3d} %", end="", flush=True)
		print()
		if sent == size:
			self.done.set()

	def log_message(self, *args):
		pass


def push(ip_3ds, url):
	# FBI "Receive URLs over the network" (like servefiles.py): 4-byte big-endian length,
	# then the URLs separated by newlines; FBI answers with one byte when it is done.
	payload = url.encode()
	try:
		with socket.create_connection((ip_3ds, 5000), timeout=10) as s:
			s.sendall(struct.pack("!L", len(payload)) + payload)
			s.settimeout(None)
			s.recv(1)
	except OSError as e:
		print(f"  Could not reach FBI at {ip_3ds}:5000 ({e}).")
		os._exit(1)


def main():
	ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
	ap.add_argument("cia")
	ap.add_argument("--port", type=int, default=8080)
	ap.add_argument("--push", metavar="3DS_IP", help="send the URL to FBI's 'Receive URLs over the network'")
	ap.add_argument("--open", action="store_true", help="also open the QR code large in the browser")
	args = ap.parse_args()

	Handler.cia = os.path.abspath(args.cia)
	if not os.path.isfile(Handler.cia):
		sys.exit(f"{args.cia} not found. Run 'make cia' first.")
	ip = lan_ip()
	server = http.server.ThreadingHTTPServer(("", args.port), Handler)
	threading.Thread(target=server.serve_forever, daemon=True).start()
	url = f"http://{ip}:{args.port}/" + urllib.parse.quote(os.path.basename(Handler.cia))
	size = os.path.getsize(Handler.cia) / 1024

	print(f"\n  {os.path.basename(Handler.cia)} ({size:.0f} KB) at {url}\n")
	if args.push:
		print(f"  FBI on the 3DS: Remote Install -> Receive URLs over the network. Sending to {args.push} ...")
		threading.Thread(target=push, args=(args.push, url), daemon=True).start()
	else:
		rows = qr_rows(url)
		print_qr(rows)
		if args.open:
			svg = os.path.join(ROOT, "build", "cia-qr.svg")
			write_svg(rows, svg)
			subprocess.run(["open", svg])
		print("\n  FBI on the 3DS: Remote Install -> Scan QR Code. Close Wiggle3DS first.")
	print("  Waiting for the download (Ctrl+C to stop) ...")
	try:
		Handler.done.wait()
		print("  Done. FBI installs it now.")
	except KeyboardInterrupt:
		print()
	server.shutdown()


if __name__ == "__main__":
	main()
