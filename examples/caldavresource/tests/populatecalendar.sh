#!/bin/bash

sudo echo "sam user.doe.* cyrus c;
dm user.doe.*;
sam user.doe cyrus c;
" | cyradm --auth PLAIN -u cyrus -w admin localhost

#Create a bunch of test messages in the test folder
# FIXME: this does not work

# FOLDERPATH=/var/spool/imap/d/user/doe/#calendars/Default
# SRCMESSAGE=/src/sink/examples/caldavresource/tests/data/cyrusevent
# sudo mkdir -p $FOLDERPATH
# sudo chown -R cyrus:mail /var/spool/imap/d/user/doe/#calendars
# sudo tee <$SRCMESSAGE >/dev/null $FOLDERPATH/{1..1000}.

# sudo chown -R cyrus:mail $FOLDERPATH
# sudo reconstruct "user.doe.#calendars.Default"
