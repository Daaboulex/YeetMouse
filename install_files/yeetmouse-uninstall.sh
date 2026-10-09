#!/bin/bash

set -euo pipefail

DKMS_NAME="yeetmouse-driver"
SERVICE_NAME="yeetmouse.service"
CONFIG_FILE="/etc/yeetmouse.conf"
CONFIG_DIR="/etc/yeetmouse"
SEED_DIR="/usr/share/yeetmouse"

USR_BIN_CTL="/usr/bin/yeetmousectl"
USR_BIN_GUI="/usr/bin/yeetmouse"
USR_BIN_UNINSTALL="/usr/bin/yeetmouse-uninstall"

DESKTOP_FILE="/usr/share/applications/yeetmouse.desktop"
ICON_FILE="/usr/share/icons/hicolor/256x256/apps/yeetmouse.png"
SYSTEMD_UNIT="/usr/lib/systemd/system/${SERVICE_NAME}"
TMPFILES_CONF="/usr/lib/tmpfiles.d/yeetmouse.conf"
SYSUSERS_CONF="/usr/lib/sysusers.d/yeetmouse.conf"

KEEP_CONFIG=""
PURGE_CONFIG=""

usage() {
	echo "Usage: yeetmouse-uninstall [--keep-config | --purge-config]"
}

for arg in "$@"; do
	case "$arg" in
		--keep-config)
			KEEP_CONFIG="yes"
			;;
		--purge-config)
			PURGE_CONFIG="yes"
			;;
		-h|--help)
			usage
			exit 0
			;;
		*)
			echo "Unknown argument: $arg"
			usage
			exit 1
			;;
	esac
done

if [[ -n "$KEEP_CONFIG" && -n "$PURGE_CONFIG" ]]; then
	echo "Please use only one of --keep-config or --purge-config."
	exit 1
fi

if [[ "${EUID:-$(id -u)}" -ne 0 ]]; then
	echo "This script must be run as root."
	echo "Please run: sudo yeetmouse-uninstall"
	exit 1
fi

echo "Stopping YeetMouse service if present..."
if command -v systemctl >/dev/null 2>&1 && [[ -d /run/systemd/system ]]; then
	systemctl disable --now "$SERVICE_NAME" 2>/dev/null || true
	systemctl daemon-reload || true
fi

echo "Unloading kernel module if loaded..."
modprobe -r yeetmouse 2>/dev/null || true
rmmod yeetmouse 2>/dev/null || true

echo "Removing DKMS installations..."
mapfile -t installed_versions < <(
	dkms status | awk -F'[/:, ]+' -v name="$DKMS_NAME" '$1 == name {print $2}' | sort -u
)

if [[ "${#installed_versions[@]}" -gt 0 ]]; then
	for version in "${installed_versions[@]}"; do
		echo "Removing DKMS module ${DKMS_NAME}/${version}"
		dkms remove "${DKMS_NAME}/${version}" --all || true
		rm -rf "/usr/src/${DKMS_NAME}-${version}"
	done
else
	echo "No installed DKMS versions found for ${DKMS_NAME}."
fi

echo "Removing installed files..."
rm -f "$USR_BIN_CTL"
rm -f "$USR_BIN_GUI"
rm -f "$USR_BIN_UNINSTALL"
rm -f "$DESKTOP_FILE"
rm -f "$ICON_FILE"
rm -f "$SYSTEMD_UNIT"
rm -f "$TMPFILES_CONF"
rm -f "$SYSUSERS_CONF"
rm -rf "$SEED_DIR"

if [[ -z "$KEEP_CONFIG" && -z "$PURGE_CONFIG" ]]; then
	if [[ -f "$CONFIG_FILE" || -d "$CONFIG_DIR" ]]; then
		read -r -p "Remove ${CONFIG_FILE} and the profiles in ${CONFIG_DIR} too? [y/N] " reply
		case "$reply" in
			[yY]|[yY][eE][sS])
				PURGE_CONFIG="yes"
				;;
			*)
				KEEP_CONFIG="yes"
				;;
		esac
	else
		KEEP_CONFIG="yes"
	fi
fi

if [[ -n "$PURGE_CONFIG" ]]; then
	echo "Removing the config and profiles..."
	rm -f "$CONFIG_FILE"
	rm -rf "$CONFIG_DIR"
else
	echo "Keeping the config and profiles."
fi

echo "YeetMouse uninstallation complete."
echo "Note: the 'yeetmouse' group was left intact."