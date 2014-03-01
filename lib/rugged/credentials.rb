module Rugged
  module Credentials
    # A plain-text username and password credential object.
    class Plaintext
      def initialize(options)
        @username = options[:username]
        @password = options[:password]
      end
    end

    # A ssh key credential object that can optionally be passphrase-protected
    class SshKey
      def initialize(options)
        @username   = options[:username]
        @publickey  = options[:publickey]
        @privatekey = options[:privatekey]
        @passphrase = options[:passphrase]
      end
    end

    class SshKeyFromAgent
      def initialize(options)
        @username = options[:username]
      end
    end

    # A "default" credential usable for Negotiate mechanisms like NTLM or
    # Kerberos authentication
    class Default
    end
  end
end
