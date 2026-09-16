#!/bin/sh

set -u

APN="jionet"
IP_TYPE="ipv4v6"
CHECK_INTERVAL=15
REGISTRATION_TIMEOUT=150

log()
{
    echo "[radian-5g] $*"
}

find_modem()
{
    mmcli -L 2>/dev/null | \
        sed -n 's#.*Modem/\([0-9][0-9]*\).*RM520N.*#\1#p' | head -n 1
}

find_connected_bearer()
{
    MODEM="$1"

    for BEARER in $(mmcli -m "$MODEM" 2>/dev/null | \
        sed -n 's#.*Bearer/\([0-9][0-9]*\).*#\1#p'); do

        if mmcli -b "$BEARER" 2>/dev/null | \
            grep -q "connected: yes"; then
            echo "$BEARER"
            return 0
        fi
    done

    return 1
}

configure_connection()
{
    MODEM="$1"

    log "Checking modem state..."

    STATE=$(mmcli -m "$MODEM" 2>/dev/null | \
        sed -n 's/.*state: //p' | head -n 1 || true)

    log "Modem state: $STATE"

    if [ "$STATE" = "disabled" ]; then
        log "Enabling modem..."

        if ! mmcli -m "$MODEM" --enable; then
            log "ERROR: Failed to enable modem"
            return 1
        fi
    fi

    log "Waiting for Jio registration..."

    i=0
    REGISTRATION_CHECK_INTERVAL=2
    REGISTRATION_MAX_CHECKS=$((REGISTRATION_TIMEOUT / REGISTRATION_CHECK_INTERVAL))

    while [ "$i" -lt "$REGISTRATION_MAX_CHECKS" ]; do

        STATE=$(mmcli -m "$MODEM" 2>/dev/null | \
            sed -n 's/.*state: //p' | head -n 1 || true)

        if [ "$STATE" = "registered" ] || \
           [ "$STATE" = "connected" ]; then
            log "Modem registered"
            break
        fi

        log "Jio registration state: ${STATE:-unknown} - waiting..."

        sleep "$REGISTRATION_CHECK_INTERVAL"
        i=$((i + 1))
    done

    if [ "$STATE" != "registered" ] && \
       [ "$STATE" != "connected" ]; then

        log "ERROR: Modem did not register within ${REGISTRATION_TIMEOUT} seconds"
        log "Resetting RM520N modem..."

        if mmcli -m "$MODEM" --reset; then
            log "RM520N modem reset command successful"
        else
            log "ERROR: RM520N modem reset failed"
            return 1
        fi

        log "Waiting for modem to re-enumerate..."

        sleep 10

        MODEM=""
        i=0

        while [ "$i" -lt 30 ]; do

            MODEM=$(find_modem || true)

            if [ -n "$MODEM" ]; then
                log "RM520N modem reappeared with ID: $MODEM"
                break
            fi

            sleep 2
            i=$((i + 1))
        done

        if [ -z "$MODEM" ]; then
            log "ERROR: RM520N modem did not reappear after reset"
            return 1
        fi

        log "Re-checking modem after reset..."

        STATE=$(mmcli -m "$MODEM" 2>/dev/null | \
            sed -n 's/.*state: //p' | head -n 1 || true)

        log "Modem state after reset: $STATE"

        if [ "$STATE" = "disabled" ]; then
            log "Enabling modem after reset..."

            if ! mmcli -m "$MODEM" --enable; then
                log "ERROR: Failed to enable modem after reset"
                return 1
            fi
        fi

        log "Waiting for Jio registration after reset..."

        i=0

        while [ "$i" -lt "$REGISTRATION_MAX_CHECKS" ]; do

            STATE=$(mmcli -m "$MODEM" 2>/dev/null | \
                sed -n 's/.*state: //p' | head -n 1 || true)

            if [ "$STATE" = "registered" ] || \
               [ "$STATE" = "connected" ]; then
                log "Modem registered after reset"
                break
            fi

            log "Post-reset registration state: ${STATE:-unknown} - waiting..."

            sleep "$REGISTRATION_CHECK_INTERVAL"
            i=$((i + 1))
        done

        if [ "$STATE" != "registered" ] && \
           [ "$STATE" != "connected" ]; then
            log "ERROR: Modem still did not register after reset"
            return 1
        fi
    fi

    # Check whether a data bearer already exists.
    BEARER=$(find_connected_bearer "$MODEM" || true)

    if [ -n "$BEARER" ]; then
        log "Existing connected bearer found: $BEARER"
    else
        log "Starting Jio data connection..."

        if ! mmcli -m "$MODEM" \
            --simple-connect="apn=$APN,ip-type=$IP_TYPE"; then
            log "ERROR: Failed to establish data connection"
            return 1
        fi

        log "Data connection established"

        i=0
        BEARER=""

        while [ "$i" -lt 30 ]; do

            BEARER=$(find_connected_bearer "$MODEM" || true)

            if [ -n "$BEARER" ]; then
                break
            fi

            sleep 1
            i=$((i + 1))
        done
    fi

    if [ -z "$BEARER" ]; then
        log "ERROR: Connected bearer not found"
        return 1
    fi

    log "Bearer ID: $BEARER"

    BEARER_INFO=$(mmcli -b "$BEARER" 2>/dev/null || true)

    INTERFACE=$(echo "$BEARER_INFO" | \
        sed -n 's/.*interface: \([^ ]*\).*/\1/p' | head -n 1)

    if [ -z "$INTERFACE" ]; then
        log "ERROR: WWAN interface not found"
        return 1
    fi

    log "WWAN interface: $INTERFACE"

    # Extract IPv4 values only from the IPv4 section.
    IPV4=$(echo "$BEARER_INFO" | awk '
        /IPv4 configuration/ { in4=1; next }
        /IPv6 configuration/ { in4=0 }
        in4 && /address:/ {
            print $NF
            exit
        }
    ')

    PREFIX=$(echo "$BEARER_INFO" | awk '
        /IPv4 configuration/ { in4=1; next }
        /IPv6 configuration/ { in4=0 }
        in4 && /prefix:/ {
            print $NF
            exit
        }
    ')

    GATEWAY=$(echo "$BEARER_INFO" | awk '
        /IPv4 configuration/ { in4=1; next }
        /IPv6 configuration/ { in4=0 }
        in4 && /gateway:/ {
            print $NF
            exit
        }
    ')

    MTU=$(echo "$BEARER_INFO" | awk '
        /IPv4 configuration/ { in4=1; next }
        /IPv6 configuration/ { in4=0 }
        in4 && /mtu:/ {
            print $NF
            exit
        }
    ')

    log "IPv4: $IPV4/$PREFIX"
    log "Gateway: $GATEWAY"
    log "MTU: $MTU"

    log "Bringing $INTERFACE up"

    ip link set "$INTERFACE" up

    # Only flush IPv4. Do not remove IPv6 configuration.
    ip -4 addr flush dev "$INTERFACE" scope global 2>/dev/null || true

    if [ -n "$IPV4" ] && [ -n "$PREFIX" ]; then
        ip addr add "$IPV4/$PREFIX" dev "$INTERFACE" 2>/dev/null || true
    fi

    if [ -n "$MTU" ]; then
        ip link set "$INTERFACE" mtu "$MTU" 2>/dev/null || true
    fi

    if [ -n "$GATEWAY" ]; then
        ip route del "$GATEWAY" dev "$INTERFACE" 2>/dev/null || true
        ip route add "$GATEWAY" dev "$INTERFACE" 2>/dev/null || true

        # Prefer 5G when multiple Internet connections exist.
        ip route del default dev "$INTERFACE" 2>/dev/null || true
        ip route add default via "$GATEWAY" dev "$INTERFACE" metric 5 \
            2>/dev/null || true
    fi

    log "Final WWAN status:"
    ip -br addr show "$INTERFACE"

    log "WWAN routes:"
    ip route show dev "$INTERFACE"

    log "Testing Internet through 5G..."

    if ping -I "$INTERFACE" -c 3 -W 5 8.8.8.8 \
        >/dev/null 2>&1; then

        log "5G INTERNET CONNECTIVITY: OK"
        log "RM520N 5G setup complete"
        return 0
    fi

    log "WARNING: 5G bearer connected but Internet test failed"

    return 1
}

log "Starting RM520N 5G automatic connection"

# --------------------------------------------------
# Wait for ModemManager
# --------------------------------------------------

while ! systemctl is-active --quiet ModemManager; do
    log "Waiting for ModemManager..."
    sleep 2
done

log "ModemManager is running"

# --------------------------------------------------
# Main monitoring loop
# --------------------------------------------------

while true; do

    MODEM=$(find_modem || true)

    if [ -z "$MODEM" ]; then
        log "RM520N not found - waiting..."
        sleep "$CHECK_INTERVAL"
        continue
    fi

    log "RM520N modem ID: $MODEM"

    # Check if a connected bearer already exists.
    BEARER=$(find_connected_bearer "$MODEM" || true)

    if [ -n "$BEARER" ]; then

        log "5G connection already active - monitoring"

    else

        log "5G data connection not active"

        if configure_connection "$MODEM"; then
            log "5G connection established - monitoring"
        else
            log "Connection attempt failed - retrying later"
        fi
    fi

    sleep "$CHECK_INTERVAL"

done
