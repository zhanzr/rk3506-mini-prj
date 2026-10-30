#!/bin/bash
# Find the rk3506-mini board on the local /24 and update the
# rk3506-buildroot ssh alias in ~/.ssh/config.
# The board's DHCP address changes between reboots (seen: .196 and .198).
set -u

SUBNET=${1:-}
if [ -z "$SUBNET" ]; then
	# derive from the default route interface address
	SUBNET=$(ip -4 route show default | awk '{print $3}' | cut -d. -f1-3)
fi
if [ -z "$SUBNET" ]; then
	echo "cannot determine subnet, pass one: $0 192.168.5" >&2
	exit 1
fi

echo "scanning $SUBNET.1-254 port 22 ..."
FOUND=""
for i in $(seq 1 254); do
	(timeout 0.4 bash -c "echo > /dev/tcp/$SUBNET.$i/22" 2>/dev/null && echo "$SUBNET.$i") &
done
wait > /tmp/.rkscan.$$

for ip in $(cat /tmp/.rkscan.$$); do
	host=$(ssh -o BatchMode=yes -o ConnectTimeout=3 \
		-o StrictHostKeyChecking=no -o UserKnownHostsFile=/dev/null \
		root@$ip hostname 2>/dev/null)
	if [ "$host" = "rk3506-buildroot" ]; then
		echo "FOUND board at $ip"
		FOUND=$ip
		break
	fi
done
rm -f /tmp/.rkscan.$$

if [ -z "$FOUND" ]; then
	echo "board not found on $SUBNET.0/24" >&2
	exit 1
fi

# rewrite the Host block in ~/.ssh/config
awk -v ip="$FOUND" '
	/^Host rk3506-buildroot$/ {
		skip = 1
		print "Host rk3506-buildroot"
		print "    HostName " ip
		print "    User root"
		print "    StrictHostKeyChecking no"
		print "    UserKnownHostsFile /dev/null"
		print "    LogLevel ERROR"
		next
	}
	/^[ \t]/ { if (skip) next }
	{ skip = 0; print }
' ~/.ssh/config > ~/.ssh/config.new && mv ~/.ssh/config.new ~/.ssh/config
chmod 600 ~/.ssh/config

echo "ssh alias rk3506-buildroot -> $FOUND"
ssh -o ConnectTimeout=5 rk3506-buildroot hostname
