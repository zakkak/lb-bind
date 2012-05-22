echo 'Running namebench "'$1'" times in parallel'

rm out*

for i in `seq $1`
do
	./namebench.py -x -O 192.168.10.10 -i hostnames > out$i &
done

wait
grep "^192.168.10.10" out*
