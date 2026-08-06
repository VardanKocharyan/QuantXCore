import time
import hmac
import hashlib
import requests
import json

# Քո API Key-երը
API_KEY = "cBZCBNsE7j8KdamZdDGf7Rkm3iPmufGYbrMrZs9oCq4W4E3RZTsIR04CgyVmurx9"
# Տեղադրիր քո SECRET KEY-ը այստեղ
API_SECRET = "LIu9CRSFJUfepJQPIdGUNBuetm54hzmqHcRugsXNw2SpmxHTJikiS29LWgRDbhpP" 

BASE_URL = "https://testnet.binance.vision"

timestamp = int(time.time() * 1000)
query_string = f"timestamp={timestamp}"

signature = hmac.new(
    API_SECRET.encode('utf-8'),
    query_string.encode('utf-8'),
    hashlib.sha256
).hexdigest()

url = f"{BASE_URL}/api/v3/account?{query_string}&signature={signature}"
headers = {"X-MBX-APIKEY": API_KEY}

response = requests.get(url, headers=headers)

if response.status_code == 200:
    data = response.json()
    print("\n=== BINANCE TESTNET BALANCES ===")
    for asset in data.get("balances", []):
        free = float(asset["free"])
        locked = float(asset["locked"])
        if free > 0 or locked > 0:
            print(f"Asset: {asset['asset']:<6} | Free: {free:<12} | Locked: {locked}")
    print("================================\n")
else:
    print("Error:", response.status_code, response.text)
EOF
