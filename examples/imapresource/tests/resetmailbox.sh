#!/bin/bash

sudo echo "sam user.doe.* cyrus c" | cyradm --auth PLAIN -u cyrus -w admin localhost
sudo echo "dm user.doe.*" | cyradm --auth PLAIN -u cyrus -w admin localhost
sudo echo "cm user.doe.test" | cyradm --auth PLAIN -u cyrus -w admin localhost
sudo echo "cm user.doe.Drafts" | cyradm --auth PLAIN -u cyrus -w admin localhost
sudo echo "sam user.doe cyrus c" | cyradm --auth PLAIN -u cyrus -w admin localhost
sudo cp /work/source/Sink/examples/imapresource/tests/data/1365777830.R28.localhost.localdomain\:2\,S /var/spool/imap/d/user/doe/test/1.
sudo chown cyrus:mail /var/spool/imap/d/user/doe/test/1.
sudo /usr/lib/cyrus-imapd/reconstruct "user.doe.test"
