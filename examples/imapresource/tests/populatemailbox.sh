#!/bin/bash

sudo echo "sam user.doe.* cyrus c" | cyradm --auth PLAIN -u cyrus -w admin localhost
#Delete all mailboxes
sudo echo "dm user.doe.*" | cyradm --auth PLAIN -u cyrus -w admin localhost
#Create mailboxes
sudo echo "cm user.doe.test" | cyradm --auth PLAIN -u cyrus -w admin localhost
sudo echo "cm user.doe.Drafts" | cyradm --auth PLAIN -u cyrus -w admin localhost
sudo echo "cm user.doe.Trash" | cyradm --auth PLAIN -u cyrus -w admin localhost

#Set acls so we can create in INBOX
sudo echo "sam user.doe cyrus c" | cyradm --auth PLAIN -u cyrus -w admin localhost

#Subscribe to mailboxes
sudo echo "sub INBOX" | cyradm --auth PLAIN -u doe -w doe localhost
sudo echo "sub INBOX.test" | cyradm --auth PLAIN -u doe -w doe localhost
sudo echo "sub INBOX.Drafts" | cyradm --auth PLAIN -u doe -w doe localhost
sudo echo "sub INBOX.Trash" | cyradm --auth PLAIN -u doe -w doe localhost

#Create a bunch of test messages in the test folder
# for i in `seq 1 5000`;
# do
#     # sudo cp /src/sink/examples/imapresource/tests/data/1365777830.R28.localhost.localdomain\:2\,S /var/spool/imap/d/user/doe/test/$i.
# done
# Because this is way faster than a loop
FOLDERPATH=/var/spool/imap/d/user/doe/test
sudo tee </src/sink/examples/imapresource/tests/data/1365777830.R28.localhost.localdomain\:2\,S >/dev/null $FOLDERPATH/{1..1000}.
sudo tee </src/sink/examples/imapresource/tests/data/1365777830.R28.localhost.localdomain\:2\,S >/dev/null $FOLDERPATH/{1001..2000}.
sudo tee </src/sink/examples/imapresource/tests/data/1365777830.R28.localhost.localdomain\:2\,S >/dev/null $FOLDERPATH/{2001..3000}.
sudo tee </src/sink/examples/imapresource/tests/data/1365777830.R28.localhost.localdomain\:2\,S >/dev/null $FOLDERPATH/{3001..4000}.
sudo tee </src/sink/examples/imapresource/tests/data/1365777830.R28.localhost.localdomain\:2\,S >/dev/null $FOLDERPATH/{4001..5000}.
sudo tee </src/sink/examples/imapresource/tests/data/1365777830.R28.localhost.localdomain\:2\,S >/dev/null $FOLDERPATH/{5001..6000}.
sudo tee </src/sink/examples/imapresource/tests/data/1365777830.R28.localhost.localdomain\:2\,S >/dev/null $FOLDERPATH/{6001..7000}.
sudo tee </src/sink/examples/imapresource/tests/data/1365777830.R28.localhost.localdomain\:2\,S >/dev/null $FOLDERPATH/{7001..8000}.
sudo tee </src/sink/examples/imapresource/tests/data/1365777830.R28.localhost.localdomain\:2\,S >/dev/null $FOLDERPATH/{8001..9000}.
sudo tee </src/sink/examples/imapresource/tests/data/1365777830.R28.localhost.localdomain\:2\,S >/dev/null $FOLDERPATH/{9001..10000}.

sudo chown -R cyrus:mail $FOLDERPATH
sudo reconstruct "user.doe.test"
