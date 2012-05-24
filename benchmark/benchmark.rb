#!/usr/bin/ruby
require 'socket'
require 'digest/md5'
require 'rubygems'
require 'dnsruby'
include Dnsruby

def range (min, max)
    rand * (max-min) + min
end

min = 1
max = 5

#lookup address
names = Array["www.youtube.net"] 

res = Dnsruby::Resolver.new
res.do_caching = false 

while true do
	sleep(0.5)
	# Use the system configured nameservers to run a query
	#names.each_index { | index | puts index }
	#res = Dnsruby::Resolver.new
	#res.do_caching = false
	puts "log: trying to resolve name"	
	begin
		ret = res.query(names[range(0,0).to_i]);
	rescue Dnsruby::ResolvTimeout
		puts "log: failed to get dns response"
		next
	end
	puts "log: name resolved"
	#puts "*** ***"
	#puts ret.answer.first
	ip = ret.answer.first.rdata_to_string
	#ip = 1;
	#puts ARGV.first
	puts "log: trying to connect to socket"	
	clientSession = TCPSocket.new( ARGV.first, 2114 )
	puts "log: starting connection"
	puts "log: sending request to worker:#{ip}"
	#generate dummy loads
	cpu_usage = range(min, max).to_f 
	io_usage = range(min, max).to_f
	#cpu_idle = range(min, max)
	total_traffic = range(min, max).to_f
	liveness_period = range(60, 120).to_i
	#liveness_period = range(5, 15).to_i
	#generate request message (we do not add request here)
	message = "WORKLOAD##{ip}$#{io_usage}$#{cpu_usage}$#{total_traffic}$#{liveness_period}#none#\n"
	#send request message
	clientSession.write(message)
	
	puts "log: closing connection"
	clientSession.close	
end
