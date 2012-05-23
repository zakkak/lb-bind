$TTL @UPDATE_INTERVAL@
;
; dns zone for for schroder.net
;
; any time you make a change to the domain, bump the
; "serial" setting below. the format is easy:
; YYYYMMDDI, with the I being an iterator in case you
; make more than one change during any one day
$ORIGIN vr.es.
@ IN SOA ns1.vr.es. admin.vr.es. (
                        201205026 ; serial
                        8h        ; refresh
                        4h        ; retry
                        4w        ; expire
                        1d )      ; minimum
	IN NS ns1.vr.es.
	IN NS ns2.vr.es.

;vr.es IN A 192.168.17.254 
ns1	IN A @MY_IP@
ns2	IN A @MY_IP@
www 	IN A 192.168.17.0
 	IN A 192.168.17.1
 	IN A 192.168.17.2
 	IN A 192.168.17.3
 	IN A 192.168.17.4
 	IN A 192.168.17.5
mail 	IN A 192.168.17.10
 	IN A 192.168.17.11
 	IN A 192.168.17.12
 	IN A 192.168.17.13
 	IN A 192.168.17.14
 	IN A 192.168.17.15
 	IN A 192.168.17.13
 	IN A 192.168.17.13
