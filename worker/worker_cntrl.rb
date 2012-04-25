#!/usr/bin/ruby
require 'daemons'

Daemons.run('worker.rb')
