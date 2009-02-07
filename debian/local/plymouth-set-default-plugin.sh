#!/bin/sh

set -e

Usage ()
{
	echo "Usage: plymouth-set-default-plugin { --list | --reset | <plugin-name> [ --rebuild-initrd ] }"
}

List_plugins ()
{
	for PLUGIN in /usr/lib/plymouth/*.so
	do
		[ -f "${PLUGIN}" ] || continue
		[ -L "${PLUGIN}" ] && continue

		if nm -D "${PLUGIN}" | grep -q ply_boot_splash_plugin_get_interface
		then
			echo "$(basename ${PLUGIN} .so)"
		fi
	done
}

Get_default_plugin ()
{
	PLUGIN_NAME="$(basename $(readlink /usr/lib/plymouth/default.so) .so)"

	if [ "${PLUGIN_NAME}" = ".so" ]
	then
		${0} --reset

		PLUGIN_NAME="$(basename $(readlink /usr/lib/plymouth/default.so) .so)"
	fi

	[ "${PLUGIN_NAME}" = ".so" ] || echo "${PLUGIN_NAME}" && exit 1
}

DO_RESET="0"
DO_INITRD_REBUILD="0"
DO_LIST="0"
PLUGIN_NAME=""

while [ ${#} -gt 0 ]
do
	case "${1}" in
		--list)
			if [ -n "${PLUGIN_NAME}" ]
			then
				echo "You can only specify --list or a plugin name, not both" > /dev/stderr
				echo "$(Usage)" > /dev/stderr

				exit 1
			fi

			if [ ${DO_RESET} -ne 0 ]
			then
				echo "You can only specify --reset or --list, not both" > /dev/stderr
				echo "$(Usage)" > /dev/stderr

				exit 1
			fi

			DO_LIST="1"
			;;

		--rebuild-initrd)
			DO_INITRD_REBUILD="1"
			;;

		--reset|default)
			if [ -n "${PLUGIN_NAME}" ]
			then
				echo "You can only specify --reset or a plugin name, not both" > /dev/stderr
				echo "$(Usage)" > /dev/stderr

				exit 1
			fi

			if [ ${DO_LIST} -ne 0 ]
			then
				echo "You can only specify --reset or --list, not both" > /dev/stderr
				echo "$(Usage)" > /dev/stderr

				exit 1
			fi

			DO_RESET="1"
			;;

		*)
			if [ -n "${PLUGIN_NAME}" ]
			then
				echo "You can only specify one plugin at a time" > /dev/stderr
				echo "$(Usage)" > /dev/stderr

				exit 1
			fi

			if [ ${DO_RESET} -ne 0 ]
			then
				echo "You can only specify --reset or a plugin name, not both" > /dev/stderr
				echo "$(Usage)" > /dev/stderr

				exit 1
			fi

			if [ ${DO_LIST} -ne 0 ]
			then
				echo "You can only specify --list or a plugin name, not both" > /dev/stderr
				echo "$(Usage)" > /dev/stderr

				exit 1
			fi

			PLUGIN_NAME="${1}"
			;;
	esac

	shift
done

if [ ${DO_LIST} -ne 0 ]
then
	List_plugins
	exit ${?}
fi

if [ ${DO_RESET} -eq 0 ] && [ ${DO_INITRD_REBUILD} -eq 0 ] && [ -z ${PLUGIN_NAME} ]
then
	Get_default_plugin
	exit ${?}
fi

if [ $(id -u) -ne 0 ]
then
	echo "This program must be run as root" > /dev/stderr
	exit 1
fi

if [ ${DO_RESET} -ne 0 ]
then
	PLUGIN_NAME="$(basename $(ls -1 -t /usr/lib/plymouth/*.so 2> /dev/null | grep -v default.so | tail -n 1) .so)"

	if [ "${PLUGIN_NAME}" = ".so" ]
	then
		rm -f /usr/lib/plymouth/default.so
		exit 0
	fi
fi

if [ ! -e /usr/lib/plymouth/${PLUGIN_NAME}.so ]
then
	echo "/usr/lib/plymouth/${PLUGIN_NAME}.so does not exist" > /dev/stderr
	exit 1
fi

( cd /usr/lib/plymouth;
	ln -sf ${PLUGIN_NAME}.so default.so && \
	[ ${DO_INITRD_REBUILD} -ne 0 ] && \
	update-initramfs -u )
