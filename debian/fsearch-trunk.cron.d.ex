#
# Regular cron jobs for the fsearch-trunk package
#
0 4	* * *	root	[ -x /usr/bin/fsearch-trunk_maintenance ] && /usr/bin/fsearch-trunk_maintenance
