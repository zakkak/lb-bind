#!/bin/sh

for i in in/*
do
	rm -f ${i#in/}; sed 's/@UPDATE_INTERVAL@/'$1'/g' $i > ${i#in/};
done
