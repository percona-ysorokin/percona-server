#! /bin/sh
#
# A simple startup script for mysqld_multi by Tim Smith and Jani Tolonen.
# This script assumes that my.cnf file exists either in /etc/my.cnf or
# /root/.my.cnf and has groups [mysqld_multi] and [mysqldN]. See the
# mysqld_multi documentation for detailed instructions.
#
# This script can be used as /etc/init.d/mysql.server
#

basedir=/usr/local/mysql
bindir=/usr/local/mysql/bin

if test -x $bindir/mysqld_multi
then
  mysqld_multi= "$bindir/mysqld_multi";
else
  echo "Can't execute $bindir/mysqld_multi from dir $basedir"
fi

case "$1" in
    start )
        "$mysqld_multi" start
        ;;
    stop )
        "$mysqld_multi" stop
        ;;
    report )
        "$mysqld_multi" report
        ;;
    restart )
        "$mysqld_multi" stop
        "$mysqld_multi" start
        ;;
    *)
        echo "Usage: $0 {start|stop|report|restart}" >&2
        ;;
esac
