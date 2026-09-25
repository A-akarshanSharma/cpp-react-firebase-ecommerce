#!/usr/bin/env python3
"""Read-only preflight. Prints missing requirements, never config or secret values."""
import ipaddress
import json
import pathlib
import re
import shlex
import sys

root = pathlib.Path(__file__).resolve().parent
errors = []
config = {}
try:
    for line in (root / ".env").read_text().splitlines():
        if not line.strip() or line.lstrip().startswith("#"):
            continue
        key, value = line.split("=", 1)
        parsed = shlex.split(value, comments=True)
        config[key.strip()] = " ".join(parsed)
except (OSError, ValueError):
    errors.append("Create a valid deploy/.env from deploy/.env.example.")
for key in ["SITE_HOST", "ACME_EMAIL", "PREVIEW_ALLOWED_IPS", "FIREBASE_PROJECT_ID",
            "VITE_FIREBASE_API_KEY", "VITE_FIREBASE_AUTH_DOMAIN", "VITE_FIREBASE_APP_ID"]:
    value = config.get(key, "")
    if not value or "example" in value or value.startswith("your-"):
        errors.append(f"Set a real preview value for {key}.")
host = config.get("SITE_HOST", "")
if (len(host) > 253 or "." not in host or all(part.isdigit() for part in host.split("."))
        or any(not re.fullmatch(r"[a-zA-Z0-9](?:[a-zA-Z0-9-]{0,61}[a-zA-Z0-9])?", label) for label in host.split("."))):
    errors.append("SITE_HOST must be a DNS hostname without scheme, path or port.")
try:
    networks = [ipaddress.ip_network(item, strict=False) for item in config.get("PREVIEW_ALLOWED_IPS", "").split()]
    if not networks or any(n.prefixlen == 0 or not n.network_address.is_global for n in networks):
        raise ValueError()
except ValueError:
    errors.append("Set real public tester IPs/CIDRs; documentation addresses and unrestricted /0 networks are not allowed.")
try:
    account = json.loads((root / "secrets/service-account.json").read_text())
    if account.get("project_id") != config.get("FIREBASE_PROJECT_ID"):
        errors.append("The service account must belong to the configured demo Firebase project.")
    if account.get("type") != "service_account" or not account.get("private_key") or not account.get("client_email"):
        errors.append("The service-account file is incomplete.")
except (OSError, ValueError):
    errors.append("Provide a readable deploy/secrets/service-account.json (run preflight with sudo if protected).")
if errors:
    print("Preview configuration is not ready:")
    for error in errors:
        print("- " + error)
    sys.exit(1)
print("Preview configuration fields checked. Also verify DNS, Firebase indexes/rules and tester IPs before launch.")
