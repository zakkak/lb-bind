#/bin/sh
cp resolv.conf /etc/resolv.conf
TTL=0 /usr/local/sbin/named -n2 -c /etc/bind/named.conf
dig www.hahakios.net
