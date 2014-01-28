#!/usr/bin/env ruby

# Rugged "fetch" example - shows how to use the remote API
#
# Written by the Rugged contributors, based on the libgit2 examples.
#
# To the extent possible under law, the author(s) have dedicated all copyright
# and related and neighboring rights to this software to the public domain
# worldwide. This software is distributed without any warranty.
#
# You should have received a copy of the CC0 Public Domain Dedication along
# with this software. If not, see
# <http://creativecommons.org/publicdomain/zero/1.0/>.

$:.unshift File.expand_path("../../lib", __FILE__)

require 'optparse'
require 'ostruct'
require 'rugged'

def parse_options(args)
  options = OpenStruct.new
  options.repodir = "."

  opt_parser = OptionParser.new do |opts|
    opts.banner = "Usage blame.rb [options] [<commit range>] <path>"

    opts.on "--git-dir=<path>", "Set the path to the repository." do |dir|
      options.repodir = dir
    end

    opts.on "-h", "--help", "Show this message" do
      puts opts
      exit
    end
  end

  opt_parser.parse!(args)

  options.remote = args.shift || "origin"
  options.refspecs = args.length > 0 ? args : nil

  return options
end

options = parse_options(ARGV)

p options

repo = Rugged::Repository.new(options.repodir)

remote = begin
  begin
    Rugged::Remote.lookup(repo, options.remote)
  rescue
    Rugged::Remote.new(repo, options.remote)
  end
rescue
  $stderr.puts "#{options.remote.inspect} is not a valid remote or url."
  exit 1
end

remote.fetch(options.refspecs, {
  credentials: lambda { |url, username, allowed|
    return Rugged::Credentials::SshKey.new(username: username, privatekey: File.expand_path("~/.ssh/id_dsa"), passphrase: "viechie")
  },
  transfer_progress: lambda { |total_objects, indexed_objects, received_objects, local_objects, total_deltas, indexed_deltas, received_bytes|
    if received_objects < total_objects
      print "Receiving objects: #{((indexed_objects.to_f / total_objects.to_f) * 100).to_i}% (#{received_objects}/#{total_objects}), #{received_bytes} bytes\r"
    elsif indexed_deltas == 1
      puts "Receiving objects: 100% (#{received_objects}/#{total_objects}), #{(received_bytes.to_f / (1024 * 1024)).round(2)} MiB, done."
    end

    if total_deltas > 0
      if indexed_deltas < total_deltas
        print "Resolving deltas: #{((indexed_deltas.to_f / total_deltas.to_f) * 100).to_i}% (#{indexed_deltas}/#{total_deltas})\r"
      else
        puts "Resolving deltas: 100% (#{indexed_deltas}/#{total_deltas}), done."
      end
    end
  },
  update_tips: lambda { |ref, old_oid, new_oid|
    if old_oid == nil
      puts "* [new branch]      #{ref.sub("refs/remote/#{options.remote}")} -> #{ref}"
    end
  },
  progress: lambda { |message|
    print "remote: #{message}"
  }
})
