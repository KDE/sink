#!/bin/bash

echo "cm user.doe" | cyradm --auth PLAIN -u cyrus -w admin localhost

sudo echo "sam user.doe.* cyrus c;
dm user.doe.*;
cm user.doe.test;
cm user.doe.Drafts;
cm user.doe.Trash;
sam user.doe cyrus c;
" | cyradm --auth PLAIN -u cyrus -w admin localhost

sudo echo "sam user.doe.* cyrus c;
subscribe INBOX.test;
subscribe INBOX.Drafts;
subscribe INBOX.Trash;
" | cyradm --auth PLAIN -u doe -w doe localhost

#Create a bunch of test messages in the test folder
# for i in `seq 1 5000`;
# do
#     # sudo cp /src/sink/examples/imapresource/tests/data/1365777830.R28.localhost.localdomain\:2\,S /var/spool/imap/d/user/doe/test/$i.
# done
# Because this is way faster than a loop
FOLDERPATH=/var/spool/imap/d/user/doe/test
sudo mkdir $FOLDERPATH
SRCMESSAGE=/src/sink/examples/imapresource/tests/data/1365777830.R28.localhost.localdomain\:2\,S
sudo tee <$SRCMESSAGE >/dev/null $FOLDERPATH/{1..1000}.
sudo tee <$SRCMESSAGE >/dev/null $FOLDERPATH/{1001..2000}.
sudo tee <$SRCMESSAGE >/dev/null $FOLDERPATH/{2001..3000}.
sudo tee <$SRCMESSAGE >/dev/null $FOLDERPATH/{3001..4000}.
sudo tee <$SRCMESSAGE >/dev/null $FOLDERPATH/{4001..5000}.
sudo tee <$SRCMESSAGE >/dev/null $FOLDERPATH/{5001..6000}.
sudo tee <$SRCMESSAGE >/dev/null $FOLDERPATH/{6001..7000}.
sudo tee <$SRCMESSAGE >/dev/null $FOLDERPATH/{7001..8000}.
sudo tee <$SRCMESSAGE >/dev/null $FOLDERPATH/{8001..9000}.
sudo tee <$SRCMESSAGE >/dev/null $FOLDERPATH/{9001..10000}.

sudo chown -R cyrus:mail $FOLDERPATH
sudo reconstruct "user.doe.test"
