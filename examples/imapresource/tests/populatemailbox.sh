#!/bin/bash

sudo echo "sam user.doe.* cyrus c" | cyradm --auth PLAIN -u cyrus -w admin localhost
sudo echo "dm user.doe.*" | cyradm --auth PLAIN -u cyrus -w admin localhost
sudo echo "cm user.doe.test" | cyradm --auth PLAIN -u cyrus -w admin localhost
sudo echo "cm user.doe.Drafts" | cyradm --auth PLAIN -u cyrus -w admin localhost
sudo echo "cm user.doe.Trash" | cyradm --auth PLAIN -u cyrus -w admin localhost
sudo echo "sam user.doe cyrus c" | cyradm --auth PLAIN -u cyrus -w admin localhost

# for i in `seq 1 5000`;
# do
#     # sudo cp /work/source/Sink/examples/imapresource/tests/data/1365777830.R28.localhost.localdomain\:2\,S /var/spool/imap/d/user/doe/test/$i.
# done
# Because this is way faster than a loop
sudo tee </work/source/Sink/examples/imapresource/tests/data/1365777830.R28.localhost.localdomain\:2\,S >/dev/null /var/spool/imap/d/user/doe/test/{1..1000}.
sudo tee </work/source/Sink/examples/imapresource/tests/data/1365777830.R28.localhost.localdomain\:2\,S >/dev/null /var/spool/imap/d/user/doe/test/{1001..2000}.
sudo tee </work/source/Sink/examples/imapresource/tests/data/1365777830.R28.localhost.localdomain\:2\,S >/dev/null /var/spool/imap/d/user/doe/test/{2001..3000}.
sudo tee </work/source/Sink/examples/imapresource/tests/data/1365777830.R28.localhost.localdomain\:2\,S >/dev/null /var/spool/imap/d/user/doe/test/{3001..4000}.
sudo tee </work/source/Sink/examples/imapresource/tests/data/1365777830.R28.localhost.localdomain\:2\,S >/dev/null /var/spool/imap/d/user/doe/test/{4001..5000}.
sudo tee </work/source/Sink/examples/imapresource/tests/data/1365777830.R28.localhost.localdomain\:2\,S >/dev/null /var/spool/imap/d/user/doe/test/{5001..6000}.
sudo tee </work/source/Sink/examples/imapresource/tests/data/1365777830.R28.localhost.localdomain\:2\,S >/dev/null /var/spool/imap/d/user/doe/test/{6001..7000}.
sudo tee </work/source/Sink/examples/imapresource/tests/data/1365777830.R28.localhost.localdomain\:2\,S >/dev/null /var/spool/imap/d/user/doe/test/{7001..8000}.
sudo tee </work/source/Sink/examples/imapresource/tests/data/1365777830.R28.localhost.localdomain\:2\,S >/dev/null /var/spool/imap/d/user/doe/test/{8001..9000}.
sudo tee </work/source/Sink/examples/imapresource/tests/data/1365777830.R28.localhost.localdomain\:2\,S >/dev/null /var/spool/imap/d/user/doe/test/{9001..10000}.

sudo chown -R cyrus:mail /var/spool/imap/d/user/doe/test
sudo /usr/lib/cyrus-imapd/reconstruct "user.doe.test"
