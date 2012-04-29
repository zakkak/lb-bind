#!/usr/bin/ruby
require 'socket'
require 'digest/md5'
require 'daemons'

puts "Starting up worker..."
server = TCPServer.new(2113)

while (session = server.accept)
	Thread.start do
	  puts "log: Connection from #{session.peeraddr[2]} at #{session.peeraddr[3]}"
	  puts "log: got input from client"

	  req = session.recv(256)
		profiler_message = (req.chomp).split("#")
		request = profiler_message.shift
		req_digest = profiler_message.shift
		req_end = profiler_message.shift
		#puts request
		#puts req_digest.unpack('H*')	
	 	check_digest = Digest::MD5.digest(request)
		#puts check_digest.unpack('H*')
		#close session if digests do not match
		if(!(request.eql?("REQSTATS")) or req_digest != check_digest) 
			session.close
		end 
		#session.puts "Server: Welcome #{session.peeraddr[2]}\n"
		#get statistics
		cpu_stats = `sar 1 1 | grep "Average"`
		net_stats = `sar -n DEV 1 1 | grep "Average"`

		#puts "Parsing cpu statistics"
		cpu_usage = cpu_stats.split[2].to_f + cpu_stats.split[3].to_f + cpu_stats.split[4].to_f
		io_usage = cpu_stats.split[5]
		cpu_idle = cpu_stats.split[7]

		#puts "Parsing network statistics"
		total_traffic = 0
		net_stats.each do |line|
			total_traffic += line.split[4].to_f + line.split[5].to_f
		end

		timestamp = Time.now.utc.iso8601
		message = "#{io_usage}$#{cpu_usage}$#{total_traffic}$#{timestamp}"		
		checksum = Digest::MD5.hexdigest(message)
		message << "##{checksum}"
  	session.puts message
		#puts "log: sending goodbye"
		#session.puts "Server: Goodbye"	
	end
end

