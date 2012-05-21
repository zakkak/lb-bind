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
max = 100

#lookup address
names = Array["www.youtube.net"] 

while true do
	sleep(1)

	# Use the system configured nameservers to run a query
	#names.each_index { | index | puts index }
	res = Dnsruby::Resolver.new	
	#ret = res.query("www.hahakios.net")
	#puts ret.answer
	ip = Dnsruby::Resolv.getaddress(names[range(0,0).to_i])
  #p Dnsruby::Resolv.getname("210.251.121.214")

	clientSession = TCPSocket.new( "localhost", 2113 )
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
