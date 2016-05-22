#!/bin/bash
 
sudo echo "dm user.doe.test" | cyradm --auth PLAIN -u cyrus -w admin localhost
sudo echo "cm user.doe.test" | cyradm --auth PLAIN -u cyrus -w admin localhost
sudo echo "sam user.doe cyrus c" | cyradm --auth PLAIN -u cyrus -w admin localhost
sudo echo "sam user.doe.test cyrus c" | cyradm --auth PLAIN -u cyrus -w admin localhost
# sudo rm -R /var/spool/imap/d/user/doe/*
sudo cp /work/source/Sink/tests/data/maildir1/cur/1365777830.R28.localhost.localdomain\:2\,S /var/spool/imap/d/user/doe/test/1.
sudo chown cyrus:mail /var/spool/imap/d/user/doe/test/1.
sudo /usr/lib/cyrus-imapd/reconstruct "user.doe.test"
