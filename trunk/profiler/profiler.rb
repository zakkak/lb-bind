#!/usr/bin/ruby
require 'socket'
require 'digest/md5'

clientSession = TCPSocket.new( "localhost", 2113 )
puts "log: starting connection"
puts "log: sending request to worker"
#generate request message
message = "REQSTAT"
checksum = Digest::MD5.hexdigest(message)
message << ":#{checksum}"
#send request message
clientSession.puts "message"
while !(clientSession.closed?) &&
				(response = clientSession.gets)
	puts response
	#if serverMessage.include?("Goodbye")
 	#	puts "log: closing connection"
 	#	clientSession.close
	
	#should receive only one message, check cheksum (fail?)
	server_message = (response.chomp).split("$")
	io_usage = server_message.shift
	cpu_usage = server_message.shift
	total_traffic = server_message.shift
	timestamp = server_message.shift
	m_checksum = server_message.shift
	puts server_message	
	checksum = Digest::MD5.hexdigest("#{io_usage}$#{cpu_usage}$#{total_traffic}$#{timestamp}")
	if checksum.eql?(m_checksum)
		puts "MESSAGE OK"
	else 
		puts "MESSAGE NOT OK"	
	end
	puts "log: closing connection"
	clientSession.close	
end

