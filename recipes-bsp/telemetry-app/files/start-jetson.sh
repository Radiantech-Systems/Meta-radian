#!/usr/bin/env bash

set -e

APP_DIR="/opt/telemetry-app"

SERVICES=(
    telemetry-agent
    footage-recorder
    live_stream
)

echo "=============================================="
echo " Radian Telemetry Application"
echo "=============================================="
echo "==> Application directory: $APP_DIR"

# Check application directory
if [ ! -d "$APP_DIR" ]; then
    echo "ERROR: $APP_DIR not found"
    exit 1
fi

# Check .env
if [ ! -f "$APP_DIR/.env" ]; then
    echo "ERROR: $APP_DIR/.env not found"
    exit 1
fi

# Check Python
if [ ! -x /usr/bin/python3 ]; then
    echo "ERROR: /usr/bin/python3 not found"
    exit 1
fi

echo "==> Python:"
/usr/bin/python3 --version

echo "==> Checking Python dependencies..."

if ! /usr/bin/python3 -c "import psutil, requests, dotenv" 2>/dev/null; then
    echo "ERROR: Required Python dependencies are missing"
    exit 1
fi

echo "    Python dependencies: OK"

echo "==> Reloading systemd..."
systemctl daemon-reload

echo "==> Enabling and starting services..."

for svc in "${SERVICES[@]}"; do

    if systemctl list-unit-files | grep -q "^${svc}.service"; then
        echo "    Starting $svc..."
        systemctl enable "$svc"
        systemctl restart "$svc"
    else
        echo "ERROR: $svc.service not found"
        exit 1
    fi

done

echo
echo "=============================================="
echo " Service Status"
echo "=============================================="

FAILED=0

for svc in "${SERVICES[@]}"; do
    state=$(systemctl is-active "$svc" 2>/dev/null || true)

    printf "    %-25s %s\n" "$svc" "${state:-not-installed}"

    if [ "$state" != "active" ]; then
        FAILED=1
    fi
done

echo

if [ "$FAILED" -ne 0 ]; then
    echo "ERROR: One or more services failed to start."
    echo
    echo "Check logs with:"
    echo "  journalctl -u telemetry-agent -n 50 --no-pager"
    echo "  journalctl -u footage-recorder -n 50 --no-pager"
    echo "  journalctl -u live_stream -n 50 --no-pager"
    exit 1
fi

echo "=============================================="
echo " Telemetry Application Started Successfully"
echo "=============================================="

echo
echo "Logs:"
echo "  journalctl -u telemetry-agent -f"
echo "  journalctl -u footage-recorder -f"
echo "  journalctl -u live_stream -f"
