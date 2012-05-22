#!/usr/bin/ruby
require 'socket'
require 'digest/md5'
require 'rubygems'
require 'dnsruby'
include Dnsruby

def range (min, max)
    rand * (max-min) + min
end

min = 0
max = 10

#lookup address
names = Array["www.hahakios.net"] 

res = Dnsruby::Resolver.new
res.do_caching = false 

while true do
	sleep(0.5)

	# Use the system configured nameservers to run a query
	#names.each_index { | index | puts index }
	#res = Dnsruby::Resolver.new
	#res.do_caching = false	
	begin
		ret = res.query(names[range(0,0).to_i]);
	rescue Dnsruby::ResolvTimeout
		puts "log: failed to get dns response"
		next
	end
	#puts "*** ***"
	#puts ret.answer.first
	ip = ret.answer.first.rdata_to_string
	#ret.answer.each { |entry|puts entry }
	#puts ret.answer.first
	#resolv = Dnsruby::DNS.new
	#resolv.do_caching = false;
	#ip = resolv.getaddress(names[range(0,0).to_i])
  #p Dnsruby::Resolv.getname("210.251.121.214")
	#ip = 1;
	clientSession = TCPSocket.new( "192.168.1.73", 2113 )
	puts "log: starting connection"
	puts "log: sending request to worker:#{ip}"
	#generate dummy loads
	cpu_usage = range(min, max) 
	io_usage = range(min, max)
	#cpu_idle = range(min, max)
	total_traffic = range(min, max)
	liveness_period = range(1, 5).to_i
	#generate request message (we do not add request here)
	message = "WORKLOAD##{ip}$#{io_usage}$#{cpu_usage}$#{total_traffic}$#{liveness_period}#none#\n"
	#send request message
	clientSession.write(message)
	
	puts "log: closing connection"
	clientSession.close	
end
