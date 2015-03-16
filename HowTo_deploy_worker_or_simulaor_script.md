# Introduction #
How to download and execute worker.rb or simulator.rb scripts


# Requires #
  * deamonize ruby package
  * sysstat

# Download #
svn checkout http://lb-bind.googlecode.com/svn/trunk/bind-9.9.0 lb-bind

# Run scripts #
ruby worker.rb

ruby simulator.rb

# Run worker.rb as daemon #
ruby worker\_cntrl.rb start (or stop to stop process)