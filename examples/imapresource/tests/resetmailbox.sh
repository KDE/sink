#!/bin/bash

sudo echo "sam user.doe.* cyrus c" | cyradm --auth PLAIN -u cyrus -w admin localhost
sudo echo "dm user.doe.*" | cyradm --auth PLAIN -u cyrus -w admin localhost
sudo echo "cm user.doe.test" | cyradm --auth PLAIN -u cyrus -w admin localhost
sudo echo "subscribe INBOX.test" | cyradm --auth PLAIN -u doe -w doe localhost
sudo echo "cm user.doe.Drafts" | cyradm --auth PLAIN -u cyrus -w admin localhost
sudo echo "subscribe INBOX.Drafts" | cyradm --auth PLAIN -u doe -w doe localhost
sudo echo "cm user.doe.Trash" | cyradm --auth PLAIN -u cyrus -w admin localhost
sudo echo "subscribe INBOX.Trash" | cyradm --auth PLAIN -u doe -w doe localhost
sudo echo "sam user.doe cyrus c" | cyradm --auth PLAIN -u cyrus -w admin localhost
FOLDERPATH=/var/spool/imap/d/user/doe/test
sudo cp /src/sink/examples/imapresource/tests/data/1365777830.R28.localhost.localdomain\:2\,S $FOLDERPATH/1.
sudo chown cyrus:mail $FOLDERPATH/1.
sudo reconstruct "user.doe.test"
