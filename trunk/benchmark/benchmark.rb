#!/usr/bin/ruby
require 'socket'
require 'digest/md5'

def range (min, max)
    rand * (max-min) + min
end

min = 0;
max = 100;

#lookup address
ip = "192.168.1.48"

clientSession = TCPSocket.new( "localhost", 2113 )
puts "log: starting connection"
puts "log: sending request to worker"
#generate dummy loads
cpu_usage = range(min, max) 
io_usage = range(min, max)
cpu_idle = range(min, max)
total_traffic = range(min, max)
liveness_period = range(1, 5)
#generate request message (we do not add request here)
message = "WORKLOAD##{ip}$#{io_usage}$#{cpu_usage}$#{total_traffic}$#{liveness_period}#none"
#send request message
clientSession.puts "message"
	
puts "log: closing connection"
clientSession.close	
end
