# Copyright (c) 2009, Chris Wong <chris@chriswongstudio.com> All rights
# reserved.
#
# Redistribution and use in source and binary forms, with or without
# modification, are permitted provided that the following conditions are met:
#
# * Redistributions of source code must retain the above copyright notice,
#   this list of conditions and the following disclaimer.
# * Redistributions in binary form must reproduce the above copyright notice,
#   this list of conditions and the following disclaimer in the documentation
#   and/or other materials provided with the distribution.
# * Neither the name of Chris Wong Studio nor the names of its contributors
#   may be used to endorse or promote products derived from this software
#   without specific prior written permission.
#
# THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
# AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
# IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
# ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE
# LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
# CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
# SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
# INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
# CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
# ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
# POSSIBILITY OF SUCH DAMAGE.

require 'rwire'

module AMQ
  class Connection
    def self.connect(*args, &blk)
      Connection.new(*args, &blk)
    end

    def initialize(args={})
      host    = args[:host] || "localhost"
      vhost   = args[:vhost] || "/"
      user    = args[:username] || args[:user] || "guest"
      pass    = args[:password] || args[:passwd] || args[:pass] || "guest"
      client  = args[:client_name] || args[:client] || args[:name] || ARGV[0] || "unknown client"
      trace   = args[:trace] || args[:trace_level] || 0
      timeout = args[:timeout] || 5000    # Five second default timeout

      @conn = RWire::Connection.new(host, vhost, user, pass, client, trace, timeout)

      if block_given?
        result = yield self
        self.destroy
        return result
      else
        return self
      end
    end

    def new_session()
      s = Session.new(@conn.session_new(), self)
      if block_given?
        result = yield s
        s.destroy
        return result
      else
        return s
      end
    end

    def method_missing(meth, *args, &blk)
      if @conn.respond_to?(meth)
        @conn.send(meth, *args, &blk)
      else
        super.method_missing(meth, *args, &blk)
      end
    end
  end

  class Session
    def initialize(rwire_session, connection)
      @conn = connection
      @sess = rwire_session
    end

    def publish(args)
      args[:body] ||= ""
      args[:mandatory] ||= false
      args[:immediate] ||= false

      @sess.publish_body(args[:body], args[:exchange], args[:routing_key],
                         args[:mandatory], args[:immediate])
    end

    def publish_content(args)
      args[:body] ||= ""
      args[:mandatory] ||= false
      args[:immediate] ||= false

      c = RWire::Content.new
      c.body = args[:body]
      @sess.publish_content(c, args[:exchange], args[:routing_key], args[:mandatory], args[:immediate])
    ensure
      c.unlink
    end

    def declare_exchange(args)
      args[:exchange]    ||= args[:name] || nil
    	args[:type]        ||= "direct"
    	args[:passive]     ||= false
    	args[:durable]     ||= false
    	args[:undeletable] ||= false
    	args[:internal]    ||= false
    	@sess.declare_exchange(args[:exchange],
    	                       args[:type],
    	                       args[:passive],
    	                       args[:durable],
    	                       args[:undeletable],
    	                       args[:internal])
    end

    def declare_queue(args)
      args[:queue]       ||= args[:name] || nil
    	args[:passive]     ||= false
    	args[:durable]     ||= false
    	args[:exclusive]   ||= false
    	args[:auto_delete] ||= false
    	@sess.declare_queue(args[:queue],
    	                    args[:passive],
    	                    args[:durable],
    	                    args[:exclusive],
    	                    args[:auto_delete])

    end

    def bind_queue(args)
      args[:routing_key] ||= args[:queue]
      @sess.bind_queue(args[:queue], args[:exchange], args[:routing_key])
    end

    def consume(args)
      args[:no_local] = true unless args.has_key?(:no_local)
      args[:no_ack]   = true unless args.has_key?(:no_ack)
      args[:exclusuve] ||= false
    	args[:timeout]   ||= 0

      @sess.consume(args[:queue], args[:consumer_tag], args[:no_local],
                    args[:no_ack], args[:exclusuve])
      if block_given?
        consumer_tag = @sess.consumer_tag
        loop do
          if @sess.wait(args[:timeout]) != 0
            # session died
            puts "wait returns non zero"
            break
          end
          while @sess.basic_arrived_count > 0
            begin
              content = @sess.basic_arrived
              body = content.body
              # caller wants to stop if yield returns false
              break if !yield(body, content)
            ensure
              content.unlink if content
            end # begin
          end # while
        end # loop
        puts "call basic cancel"
        @sess.basic_cancel(consumer_tag)
      end

    end

    def method_missing(meth, *args, &blk)
      if @sess.respond_to?(meth)
        @sess.send(meth, *args, &blk)
      else
        super.method_missing(meth, *args, &blk)
      end
    end
  end

  class BasicContent
    def initialize(body, msg_id)
      @content            = RWire::Content.new
      @content.body       = body
      @content.message_id = msg_id
    end

    def method_missing(meth, *args, &blk)
      if @conn.respond_to?(meth)
        @conn.send(meth, *args, &blk)
      else
        super.method_missing(meth, *args, &blk)
      end
    end

  end
end
