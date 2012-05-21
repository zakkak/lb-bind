#/bin/sh
cp resolv.conf /etc/resolv.conf
/usr/local/sbin/named -c /etc/bind/named.conf
dig www.hahakios.net
