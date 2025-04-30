
# Make char devices in /dev for PS2 MRP.

myname="`basename $0`"

if [ "`id -nu`" != "root" ]; then
	echo "$myname: Must be root for making nodes"
	exit 1
fi

major=`grep mrp /proc/devices | sed -e 's/ .*//'`
for minor in 0 1 2 3 ; do
	rm -f /dev/mrp${minor} /dev/mrp${minor}c
done
if [ ! -z "$major" ]; then
	echo "Creating device files for mrp driver"
	for minor in 0 1 2 3 ; do
		if [ "${rm_only}" != "yes" ]; then
			mknod /dev/mrp${minor} c ${major} ${minor}
			chmod 666 /dev/mrp${minor}
			mknod /dev/mrp${minor}c c ${major} `expr ${minor} + 64`
			chmod 666 /dev/mrp${minor}c
		fi
	done
fi

exit 0
