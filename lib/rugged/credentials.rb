module Rugged
  module Credentials
    class Plaintext
      def initialize(username, password)
        @username, @password = username, password
      end
    end

    class SshKey
      def initialize(username, publickey, privatekey, passphrase)
        @username, @publickey, @privatekey, @passphrase = username, publickey, privatekey, passphrase
      end
    end

    class SshCustom
      def initialize(username, publickey, sign_callback)
        @username, @publickey, @sign_callback = username, publickey, sign_callback
      end
    end

    class Default
    end
  end
end
