#!/usr/bin/ruby
require 'socket'
require 'digest/md5'
require 'rubygems'
require 'daemons'
require 'thread'

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

#init workers
$workers = Hash.new
for i in 0..13 do
	key = "192.168.18.#{i}"
	new_load = Struct.new(:cpu_usage, :io_usage, :total_traffic, :liveness_period)
	loads = Array[new_load.new(0.to_f, 0.to_f, 0.to_f, 0.to_i)]
	workloads = Struct.new(:loads_num, :loads, :cpu_usage, :io_usage, :total_traffic, :lock)
	$workers[key] = workloads.new(1, loads, 0.to_f, 0.to_f, 0.to_f, Mutex.new)
end


def range (min, max)
    rand * (max-min) + min
end

#def simulate_system_tick
Thread.start do
	#puts "****** TICK ******"
	loop do
		sleep(1)
		$workers.each { | ip, val |
			val.lock.synchronize {
				if($workers[ip].loads_num > 0)
					$workers[ip].loads.each_index { | i |
						#puts "#{$workers[ip].loads[i].liveness_period}"
						$workers[ip].loads[i].liveness_period = $workers[ip].loads[i].liveness_period-1
							if($workers[ip].loads[i].liveness_period == 0)
								#remove the finished job
								#puts "log: Removing job #{i} from #{ip}"
								#puts "#{$workers[ip].loads_num}"
								if($workers[ip].loads_num == 1)
									#there is some issue and sometimes float1 - float2 where float1==float2 results in huge number...
									$workers[ip].cpu_usage = 0
									$workers[ip].io_usage = 0
									$workers[ip].total_traffic = 0
								else
									$workers[ip].cpu_usage = $workers[ip].cpu_usage - $workers[ip].loads[i].cpu_usage
									$workers[ip].io_usage = $workers[ip].io_usage - $workers[ip].loads[i].io_usage
									$workers[ip].total_traffic = $workers[ip].total_traffic - $workers[ip].loads[i].total_traffic
								end		
								$workers[ip].loads.delete_at(i)
								$workers[ip].loads_num = $workers[ip].loads_num-1
							end
					}
				end
			}
		}
	end
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
		workloads = Struct.new(:loads_num, :loads, :cpu_usage, :io_usage, :total_traffic, :lock)
		$workers[ip] = workloads.new(1, loads, cpu_usage.to_f, io_usage.to_f, total_traffic.to_f, Mutex.new)
		#load_num = $workers[ip].loads_num
	else
		new_load = Struct.new(:cpu_usage, :io_usage, :total_traffic, :liveness_period)
		$workers[ip].lock.synchronize {
			$workers[ip].loads_num = $workers[ip].loads_num+1 #can't get it working with ++
			load_num = $workers[ip].loads_num-1
			$workers[ip].loads[load_num] = new_load.new(cpu_usage.to_f, io_usage.to_f, total_traffic.to_f, liveness_period.to_i)
			#add new loads to total loads
			$workers[ip].cpu_usage = $workers[ip].cpu_usage + $workers[ip].loads[load_num].cpu_usage
			$workers[ip].io_usage = $workers[ip].io_usage + $workers[ip].loads[load_num].io_usage
			$workers[ip].total_traffic = $workers[ip].total_traffic + $workers[ip].loads[load_num].total_traffic
			if($workers[ip].cpu_usage > 255.00 or $workers[ip].io_usage > 255.00 or $workers[ip].total_traffic > 255.00) 
				$workers[ip].cpu_usage = 0.to_f;
				$workers[ip].cpu_usage = 0.to_f;
				$workers[ip].cpu_usage = 0.to_f;
			end
			#$workers[ip] = workloads.new(0, loads.new(cpu_usage, io_usage, total_traffic, liveness_period), cpu_usage, io_usage, total_traffic)
		}
	end
	#puts load_num
	#puts "log: worker load #{ip}"	
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
	`echo "remove all dumps"`
	`rm *.csv`
	loop do
		sleep(1)
		#puts "++++++++++++++++ LOOPING ++++++++++++++++++++"
		$workers.each { | ip, val |
		#puts "++++++++++++++++#{ip}++++++++++++++++++++"
			val.lock.synchronize {	
				if(val.cpu_usage <= 0 or val.io_usage <= 0 or val.total_traffic <= 0) 
					val.cpu_usage = 0;
					val.io_usage = 0;
					val.total_traffic = 0;
				end	
				if(!File.exists?("load_#{ip}.csv"))
					File.open("load_#{ip}.csv", 'w') do | f |			
						f << " " 
						f << "\t cpu usage "
						f << "\t io_usage "
						f << "\t total traffic "
						f << "\t total load\n"
						#also write first entry
						f << "#{Time.now.strftime("%X")} " 
						f << "\t #{val.cpu_usage} "
						f << "\t #{val.io_usage} "
						f << "\t #{val.total_traffic} "
						total_load = (val.cpu_usage*0.50)+(val.io_usage*0.25)+(val.total_traffic*0.25)
						f << "\t #{total_load}\n"
					end
				else
					File.open("load_#{ip}.csv", 'a') do | f |			
						#puts "writing..."					
						f << "#{Time.now.strftime("%X")} " 
						f << "\t #{val.cpu_usage} "
						f << "\t #{val.io_usage} "
						f << "\t #{val.total_traffic} "
						total_load = (val.cpu_usage*0.50)+(val.io_usage*0.25)+(val.total_traffic*0.25)
						f << "\t #{total_load}\n"
					end
				end
			}
		}
		#save all worker loads		
		if(!File.exists?("worker_loads.csv"))
			File.open("worker_loads.csv", 'w') do | f |
				$workers.each { | ip, val |
					val.lock.synchronize {			
						f << " " 
						f << "\t #{ip}'s load "
						#also write first entry
					}						
				}
				f << "\n#{Time.now.strftime("%X")} " 
				$workers.each { | ip, val |
					val.lock.synchronize {			
						total_load = (val.cpu_usage*0.50)+(val.io_usage*0.25)+(val.total_traffic*0.25)
						f << "\t #{total_load}"
					}						
				}					
			end
		else
			File.open("worker_loads.csv", 'a') do | f |
				f << "\n#{Time.now.strftime("%X")} " 
				$workers.each { | ip, val |
					val.lock.synchronize {			
						total_load = (val.cpu_usage*0.50)+(val.io_usage*0.25)+(val.total_traffic*0.25)
						f << "\t #{total_load}"
					}						
				}				
			end
		end
	end
end

#puts "Starting up worker simulator..."
Thread.start do
	bench_server = TCPServer.new(2114)
	while (session = bench_server.accept)
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
			#puts "log: message ok"
			proc_workload_message(data)
		else		
			#put "log: message check failed"
			session.close
		end
	end
end

server = TCPServer.new(2113)
loop do
	Thread.start server.accept do | session |
	  #puts "log: Connection from #{session.peeraddr[2]} at #{session.peeraddr[3]}"
	  #puts "log: got input from client1"

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
		#close session if digests do not match
		if(!(request.eql?("REQSTATS")) or req_digest != check_digest) 
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
				$workers[ip].lock.synchronize {
					cpu_usage = $workers[ip].cpu_usage 
					io_usage = $workers[ip].io_usage
					total_traffic = $workers[ip].total_traffic
				}
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
		end
	end
end

