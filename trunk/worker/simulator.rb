#!/usr/bin/ruby
require 'socket'
require 'digest/md5'
require 'daemons'

def range (min, max)
    rand * (max-min) + min
end

def proc_workload_message (message)
	
	data = message.split("$")
	ip = data.shift
	cpu_usage = data.shift
	io_usage = data.shift
	cpu_idle = data.shift
	total_traffic = data.shift
	liveness_period = data.shift
end

puts "Starting up worker simulator..."
server = TCPServer.new(2113)

while (session = server.accept)
	Thread.start do
	  puts "log: Connection from #{session.peeraddr[2]} at #{session.peeraddr[3]}"
	  puts "log: got input from client"

	  req = session.recv(256)
		profiler_message = (req.chomp).split("#")
		request = profiler_message.shift
		data = profiler_message.shift
		req_digest = profiler_message.shift
		req_end = profiler_message.shift
		#puts ip 
		#puts request
		#puts req_digest.unpack('H*')	
	 	check_digest = Digest::MD5.digest(request)
		#puts check_digest.unpack('H*')
		if(request.eql?("WORKLOAD"))
			

		end		
		#close session if digests do not match
		elsif(!(request.eql?("REQSTATS")) or req_digest != check_digest) 
			session.close
			next
		end 
		#session.puts "Server: Welcome #{session.peeraddr[2]}\n"
		#get statistics
		#cpu_stats = `sar 1 1 | grep "Average"`
		#net_stats = `sar -n DEV 1 1 | grep "Average"`

		#puts "Parsing cpu statistics"
		#cpu_usage = cpu_stats.split[2].to_f + cpu_stats.split[3].to_f + cpu_stats.split[4].to_f
		#io_usage = cpu_stats.split[5]
		#cpu_idle = cpu_stats.split[7]
		min = 0;
		max = 100;
		#puts "checkpoint 1"
		cpu_usage = range(min, max) 
		io_usage = range(min, max)
		cpu_idle = range(min, max)
		total_traffic = range(min, max)
		#puts "checkpoint 2"
		#puts "Parsing network statistics"
		#total_traffic = 0
		#net_stats.each do |line|
		#	total_traffic += line.split[4].to_f + line.split[5].to_f
		#end

		timestamp = Time.now.utc.iso8601
		message = "#{io_usage}$#{cpu_usage}$#{total_traffic}$#{timestamp}"		
		checksum = Digest::MD5.digest(message)
		message << "##{checksum}"
		#puts message
		#puts checksum.unpack('H*')
  	session.puts message
		#puts "log: sending goodbye"
		#session.puts "Server: Goodbye"	
	end
end

