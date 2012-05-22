echo 'Running namebench "'$1'" times in parallel'

rm -f out*

for i in `seq $1`
do
	./namebench.py -x -O 192.168.10.10 -i hostnames -r4 -m random -s12 > out$i &
done

wait
grep "^192.168.10.10" out*
