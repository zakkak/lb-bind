#!/usr/bin/ruby
require 'socket'
require 'digest/md5'
require 'daemons'

#def every_n_seconds(n)
#  loop do
#    before = Time.now
#    yield
#    interval = n-(Time.now-before)
#    sleep(interval) if interval > 0
#  end
#end

#every_n_seconds(5) do
# puts "#{Time.now.strftime("%X")}... beep!"
#end


$workers = Hash.new

def range (min, max)
    rand * (max-min) + min
end

def simulate_system_tick
	#puts "****** TICK ******"
	$workers.each { | ip, val |
		if($workers[ip].loads_num > 0)
			$workers[ip].loads.each_index { | i |
				$workers[ip].loads[i].liveness_period = $workers[ip].loads[i].liveness_period-1
					if($workers[ip].loads[i].liveness_period == 0)
						#remove the finished job
						#puts "log: Removing job #{i} from #{ip}"
						if($workers[ip].cpu_usage < $workers[ip].loads[i].cpu_usage)
							put "************ WE DRUNK HIM! ***************"
							$workers[ip].cpu_usage = 0
						end
						if($workers[ip].io_usage < $workers[ip].loads[i].io_usage)
							put "************ WE DRUNK HIM! ***************"
							$workers[ip].cpu_usage = 0
						end
						if($workers[ip].total_traffic < $workers[ip].loads[i].total_traffic)
							put "************ WE DRUNK HIM! ***************"
							$workers[ip].cpu_usage = 0
						end
						$workers[ip].cpu_usage = $workers[ip].cpu_usage - $workers[ip].loads[i].cpu_usage
						$workers[ip].io_usage = $workers[ip].io_usage - $workers[ip].loads[i].io_usage
						$workers[ip].total_traffic = $workers[ip].total_traffic - $workers[ip].loads[i].total_traffic
						$workers[ip].loads.delete_at(i)
						$workers[ip].loads_num = $workers[ip].loads_num-1		
					end
			}
		end
	}
	#puts "****** TACK ******"
end

def proc_workload_message (message)
	
	data = message.split("$")
	ip = data.shift
	cpu_usage = data.shift
	io_usage = data.shift
	total_traffic = data.shift
	liveness_period = data.shift
	load_num = 0
	#puts "log: Adding new load to #{ip}"
	if(!$workers.has_key?(ip))
		new_load = Struct.new(:cpu_usage, :io_usage, :total_traffic, :liveness_period)
		#loads = Array()
		loads = Array[new_load.new(cpu_usage.to_f, io_usage.to_f, total_traffic.to_f, liveness_period.to_i)]
		workloads = Struct.new(:loads_num, :loads, :cpu_usage, :io_usage, :total_traffic)
		$workers[ip] = workloads.new(1, loads, cpu_usage.to_f, io_usage.to_f, total_traffic.to_f)
		#load_num = $workers[ip].loads_num
	else
		new_load = Struct.new(:cpu_usage, :io_usage, :total_traffic, :liveness_period)
		$workers[ip].loads_num = $workers[ip].loads_num+1 #can't get it working with ++
		load_num = $workers[ip].loads_num-1
		$workers[ip].loads[load_num] = new_load.new(cpu_usage.to_f, io_usage.to_f, total_traffic.to_f, liveness_period.to_i)
		#add new loads to total loads
		$workers[ip].cpu_usage = $workers[ip].cpu_usage + $workers[ip].loads[load_num].cpu_usage
		$workers[ip].io_usage = $workers[ip].io_usage + $workers[ip].loads[load_num].io_usage
		$workers[ip].total_traffic = $workers[ip].total_traffic + $workers[ip].loads[load_num].total_traffic
		#$workers[ip] = workloads.new(0, loads.new(cpu_usage, io_usage, total_traffic, liveness_period), cpu_usage, io_usage, total_traffic)
	end
	#puts load_num
	#puts "log: cpu_usage:#{$workers[ip].cpu_usage}"
	#puts "log: io_usage:#{$workers[ip].io_usage}"
	#puts "log: total_traffic:#{$workers[ip].total_traffic}"
	#puts cpu_usage
	#puts io_usage
	#puts total_traffic
	#puts liveness_period
end

#this thread dumps worker loads to a file
Thread.start do
	File.open("loads_#{Time.now.strftime("%X")}.txt", 'a') do | f |
		loop do
			sleep(60)			
			f.puts "Probe at #{Time.now.strftime("%X")}"
			$workers.each { | ip, val | 
				f.puts "Worker #{ip}"
				f.puts "\tcpu load=#{val.cpu_usage}"
				f.puts "\tio load=#{val.io_usage}"
				f.puts "\tnet load =#{val.total_traffic}"
			}
			f.puts ""
		end
	end
end

#puts "Starting up worker simulator..."
server = TCPServer.new(2113)

#while (session = server.accept)
loop do
	Thread.start server.accept do | session |
	  #puts "log: Connection from #{session.peeraddr[2]} at #{session.peeraddr[3]}"
	  #puts "log: got input from client"

	  req = session.recv(256)
		profiler_message = req.split("#")
		request = profiler_message.shift
		data = profiler_message.shift
		req_digest = profiler_message.shift
		req_end = profiler_message.shift
		#puts req_digest.unpack('H*')	
	 	check_digest = Digest::MD5.digest(request)
		#puts check_digest.unpack('H*')
		#puts data
		if(request.eql?("WORKLOAD"))
			proc_workload_message(data)
		#close session if digests do not match
		elsif(!(request.eql?("REQSTATS")) or req_digest != check_digest) 
			session.close
		else 
			ip = data
			#session.puts "Server: Welcome #{session.peeraddr[2]}\n"
			#get statistics
			#cpu_stats = `sar 1 1 | grep "Average"`
			#net_stats = `sar -n DEV 1 1 | grep "Average"`

			#puts "Parsing cpu statistics"
			#cpu_usage = cpu_stats.split[2].to_f + cpu_stats.split[3].to_f + cpu_stats.split[4].to_f
			#io_usage = cpu_stats.split[5]
			#cpu_idle = cpu_stats.split[7]
			if(!$workers.has_key?(ip))
				cpu_usage = 0 
				io_usage = 0
				total_traffic = 0
				#cpu_usage = range(0, 100) 
				#io_usage = range(0, 100)
				#total_traffic = range(0, 100)
			else
				cpu_usage = $workers[ip].cpu_usage 
				io_usage = $workers[ip].io_usage
				total_traffic = $workers[ip].total_traffic
			end
			#puts "Parsing network statistics"
			#total_traffic = 0
			#net_stats.each do |line|
			#	total_traffic += line.split[4].to_f + line.split[5].to_f
			#end	

			#timestamp = Time.now.utc.iso8601
			#message = "#{io_usage}$#{cpu_usage}$#{total_traffic}$#{timestamp}"		
			#message = "#{io_usage}$#{cpu_usage}$#{total_traffic}"		
			message = [io_usage, cpu_usage, total_traffic].pack('e*')
			#message << [cpu_usage].pack('g')
			#message << [total_traffic].pack('g')
			#puts message.unpack('e*')
			checksum = Digest::MD5.digest(message)
			#message << "##{checksum}"
			#puts checksum.unpack('H*') 
			message << checksum
			session.write message[0,40] 
			#puts message
			#puts checksum.unpack('H*')
  			#session.puts message
			#puts "log: sending goodbye"
			#session.puts "Server: Goodbye"
			simulate_system_tick()	
		end
	end
end

