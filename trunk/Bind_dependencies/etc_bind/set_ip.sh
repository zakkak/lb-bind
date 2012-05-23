#!/bin/sh

for i in in/*
do
	rm -f ${i#in/}; sed 's/@UPDATE_INTERVAL@/100/g' $i > ${i#in/};
done

for i in in/* named.conf*
do
	sed -i 's/@MY_IP@/'$1'/g' ${i#in/};
done
